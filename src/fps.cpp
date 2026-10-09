// MHF-Z high-frame-rate mod (d3d9 proxy).
//
// The game simulates at a fixed ~30 Hz: logic and drawing for frame N, then a Sleep() in the frame limiter,
// then Present(N). We never touch game code. Instead we record every D3D9 command of frame N, and while the
// limiter sleeps we replay frame N with vertex-shader constants / transforms / UP-vertex positions interpolated
// between frame N-1 and N, presenting each result. The real Present(N) still happens at the same moment it
// would in vanilla, so simulation speed, server timing and input latency are unchanged.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <unordered_map>
#include <memory>
#include <unordered_set>
#include <algorithm>
#include "vsinfo.h"

#ifdef FPS_SAMPLER
#define PROF_NOINLINE __attribute__((noinline))  // profiling build: keep functions apart in the samples
#else
#define PROF_NOINLINE
#endif
#ifdef PAIRBENCH
static bool g_checkPredict;  // (pairbench -checkpredict: the text field's median against a full sort)
static bool g_debugJumps;    // (pairbench -jumps: print the rows ConstLerpMask switches instead of blending)
static bool g_anchorAll;     // (pairbench -anchorall: fit the origins' map in every camera context)
static uint32_t g_dbgDraw = ~0u, g_dbgPrev = ~0u;  // (-jumps: the matched draws whose constants are being looked at)
static bool g_drawInfoField;  // (-drawinfo: build the field even with field_match off)
static bool g_textOld;        // (-textold: the text matcher without the night17 changes, for comparisons)
static uint64_t g_predictCalls, g_predictDiff;
#endif

extern "C" void LoadRealD3D9();
extern "C" void* g_real[32];
extern "C" char g_d3d9Backend[MAX_PATH];    // (UTF-8, for the log)
extern "C" wchar_t g_d3d9BackendW[MAX_PATH];  // (what is loaded)
extern "C" HMODULE g_realD3D9;
extern "C" bool UseSystemD3D9();
static bool g_backendFellBack;  // d3d9_backend loaded but couldn't start: Windows' own is used (told once in game)

// ------------------------------------------------------------------------------------------------ config/log
struct Config {
    int enabled = 1;
    int mode = 2;              // 0 = passthrough, 1 = duplicate (replay N without lerp), 2 = interpolate
    int target_fps = 0;        // 0 = 97% of the monitor refresh rate
    int present_interval = -1; // -1 = keep the game's, 0 = immediate, 1 = vsync
    int interp_vs = 1, interp_xform = 1, interp_up = 1, interp_ps = 1;
    float snap_ratio = 1.0f;    // positional packet "jumped" if its largest change exceeds this fraction of its largest value;
                                // when 40% of the scene jumps at once, the tick is treated as a camera cut
    float smooth_ratio = 0.2f;  // non-positional register: lerp only if it changes by at most this fraction
    int slot_rows = 1;              // a light slot that changed lights (a position row switched): the draw's other rows of that
                                    // shader stage that changed by more than 2% (range, colours) switch with it
    int margin_us = 1500;      // keep this much slack before the limiter deadline
    int max_extra = 10;        // max interpolated frames per game tick
    int debug_marker = 0;      // draw a small square: green = interpolated frame
    int stats_interval = 10;   // seconds between stats lines in the log (0 = off)
    int hotkeys = 1;
    int debug_keys = 0;        // 1 = developer keys too (Ctrl+Shift+F7..F12, debug/mode/capture keys); 0 = only the on/off key
    int precise_tick = 0;      // 0 = keep the vanilla tick rate exactly (Sleep granularity), 1 = exact 33 ms ticks
    int debug_unlerped = 0;    // render non-interpolated draws magenta in interpolated frames
    int slerp_bones = 1;       // skinning palettes: quaternion slerp instead of linear matrix blend
    int debug_cam = 0;         // log camera-motion estimation
    int key_toggle = VK_PAUSE, key_debug = 0, key_mode = 0, key_capture = VK_DELETE;  // keys: names ("ctrl+shift+1", "delete") or virtual-key codes (0 = none)
    int overlay = 0;                // 1 fps counter, 2 detailed (frame times, graph); key_overlay cycles off / 1 / 2
    int key_overlay = VK_INSERT;
    int overlay_pos = 0;            // 0 top left, 1 top right, 2 bottom left, 3 bottom right, 4 top centre
    int overlay_scale = 0;          // 0 = by screen height (1 up to 1620 px, 2 at 4K)
    int capture_delay_ms = 3000;
    int retro = 0;                  // rolling recorder of the last frames; the capture key saves them (~1 GB of video memory)
    int retro_frames = 192;
    int retro_ticks = 16;           // with retro: the recordings of the last ticks too (saved as ticks.seq, for pairbench -seq;
                                    // a crowd tick is 4-7 MB of the game's 2 GB address space)
    int retro_trigger = 0;
    int lerp_nonpos = 2;            // interpolate smooth non-positional constants (texture projections, colours...); 2: every
                                    // one that does not step (whole numbers, atlas frames) or jump back (wrap-around, reset)
    int selfcheck = 0;              // predict real frame N from N-1 and N+1 and measure the error
    int selfcheck_interval_ms = 1000;
    int selfcheck_saves = 20;       // max image sets written per session
    int selfcheck_save_pct = 3;     // write images when more than this % of blocks are badly wrong
    int capture_trigger = 0, dump_trigger = 0;  // change the value in the ini to trigger remotely
    int trace_interval_s = 0;       // replay tracer: snapshot every draw of one real frame and of its replays
    int trace_saves = 6;            // max traces that write images + dump per session
    int trace_trigger = 0;
    int selfcheck_ablate = 0;       // run the one-mechanism-off comparison on every self-check (not only bad ones)
    int paircap_trigger = 0;        // change it while the game runs: save paircap_count pairing captures (tools\pairbench)
    int paircap_count = 6;
    int debug_pairhash = 0;         // log a hash of every tick's pairing result (to prove an optimisation changes nothing)
    int retro_autosave = 0;         // (tests: with retro, save once this many seconds after the start, as if the key was pressed)
    int text_ckeys = 1;             // screen text: glyphs known by their pixels in the glyph atlas, not by where in the atlas they
                                    // are (MHF moves letters to other atlas slots when new text needs room)
    int debug_textcands = 0;        // (1: the old candidate search, for comparisons)
    int debug_text = 0;             // the text matcher's choices logged for the first N ticks (debug)
    int debug_replayhash = 0;       // instead of the in-between frames: three at fixed points (0.37 twice, 0.71), their images hashed
                                    // (tools\replayhash.ps1: an optimisation of the replay must give the same images)
    // new settings go at the end: pairing captures store this struct as it is, and the bench copies the prefix
    int pair_threads = 0;           // threads for the pairing: 0 = automatic (physical cores - 2, at most 6), 1 = off
    int replay_filter = 1;          // in-between frames skip constant uploads that change no register (same GPU state)
    int gpu_queue = 1;              // in-between frames the GPU may still be drawing while the next is built (0 = wait each)
    int exc_log = 0;                // diagnostics: log every exception the process raises (first in line, passed on unchanged)
    int regroup = 2;                // passes that switch render target on every draw (MHF's shadow cascades) drawn one target
                                    // after the other: 0 = off, 1 = in-between frames, 2 = the game's own frames too
    int regroup_check = 0;          // diagnostics: every N seconds draw one frame both ways and log whether they differ
    int pair_async = 1;             // match frames on the worker threads while the deferred game frame executes (at the
                                    // limiter's Sleep): the two overlap instead of following each other
    int lock_copy = 1;              // a texture the game rewrites while deferred draws still sample it (MHF's UI text, 2-4
                                    // times a frame) is copied first and those draws sample the copy: the frame stays deferred
    int flush_endscene = 0;         // execute the deferred game frame at every EndScene (MHF has two scenes a frame)
    int precompute = 1;             // in-between frames: the interpolated constants computed first, on all threads
    int profile = 0;                // diagnostics: sample where the render thread spends its time (stats line "profile")
    int shadow_reuse = 0;           // in-between frames use the game frame's shadow maps instead of drawing them again
    int key_shadow = 0;             // toggles shadow_reuse
    int key_nonpos = 0x300 | '3';  // toggles lerp_nonpos 1/2 (Ctrl+Shift+3; keys: a name like ctrl+shift+3, or a virtual-key code)
    int pacing = 2;                 // in-between frames: 0 slots backwards from the deadline at the target interval, content for
                                    // when the replay starts; 1 planned by when they are shown, evenly from when the matching is
                                    // done to the real frame (smoother, fewer); 2 the slots of 0, content for when it is shown
    int late_slot = 0;              // an in-between slot just missed (up to half an interval) is still used (more frames in a crowd,
                                    // but two can land closer than one refresh apart)
    int carry_restore = 1;          // render targets carried from one tick into the next (sampled before drawn into): 1 put back
                                    // as the game frame left them after the in-between frames; 2 also each in-between frame
                                    // given the game frame's input (else it samples the one before's: a target that feeds
                                    // on itself - brightness adaptation, glow trails - runs 4-5 times per tick); 0 off
    int key_carry = 0x300 | '2';  // cycles carry_restore (Ctrl+Shift+2)
    int cam_paths = 1;              // in-between frames follow the camera's own motion (a turn stays a turn) instead of blending
                                    // each object's matrices linearly (which shrinks the view mid-way in a fast turn)
    int key_campaths = 0x300 | '1';  // toggles cam_paths (Ctrl+Shift+1)
    int field_match = 0;            // repeated meshes of a pass with no camera-motion estimate (MHF's per-object matrices rarely
                                    // agree on one) are also paired by where the uniquely matched objects around each instance
                                    // say it was, when that explains the group clearly better than nearest-position pairing
    int key_field = 0x300 | '4';    // toggles field_match (Ctrl+Shift+4)
    int text_runs = 1;              // screen-space text: whole strings (the same glyphs in the same order, moved rigidly) pair
                                    // as one, however far they moved
    int cam_fit = 1;                // camera estimate: when the candidates' matrices don't agree, vote on the objects each one explains
    int group_cam = 1;              // repeated objects paired poorly (no camera estimate, or a wrong one in a fast flick): other
                                    // predictions - another target's camera, the origins' map, last tick's, the group's own motion
    int latency_trim = 0;           // lower input latency at no cost in frames: after each real frame the game waits the time the
                                    // next tick's first in-between frame would have waited for its slot anyway (it reads its input
                                    // later; the tick rate and every frame's time stay as they were). Toggled by key_latency
    int key_latency = 0x300 | '5';  // toggles latency_trim (Ctrl+Shift+5)
    int input_latch = 0;            // quick taps the game would miss (pressed and released between two of its once-a-tick input
                                    // reads) are caught while it sleeps and handed to its next read; see InputSample
    int key_camprobe = 0x300 | VK_F6;  // developer key: camera probe (which game code writes the camera; docs/CAMERA.md)
    int camprobe_s = 6;             // how long the camera probe watches
    int camprobe_test = 0;          // testing: the probe watches a variable of the mod's own instead of the game's camera
    int camprobe_trigger = 0;       // changing it while the game runs starts the camera probe (like the key)
    int debug_lost_at = 0;          // testing: the Nth in-between Present reports "device lost" once (it must not stop the mod)
} C;

static FILE* g_log;
static char g_dir[MAX_PATH];
static LARGE_INTEGER g_qpf;
static int64_t g_t0;
static inline int64_t Qpc() { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x.QuadPart; }
static inline double Ms(int64_t d) { return d * 1000.0 / (double)g_qpf.QuadPart; }
// A millisecond clock and the 1 ms timer resolution without winmm.dll: the DLL must not load any library the real
// d3d9.dll doesn't (Rain's client crashed when winmm/winmmbase got loaded together with d3d9.dll).
static inline DWORD MsClock() { return (DWORD)(Qpc() / (g_qpf.QuadPart / 1000)); }
static void TimerResolution1ms() {
    typedef LONG(NTAPI * SetRes_t)(ULONG, BOOLEAN, PULONG);
    auto f = (SetRes_t)(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetTimerResolution");
    ULONG cur = 0;
    if (f) f(10000, TRUE, &cur);  // 100-ns units
}

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list a;
    va_start(a, fmt);
    fprintf(g_log, "[%10.3f] ", Ms(Qpc() - g_t0) / 1000.0);
    vfprintf(g_log, fmt, a);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(a);
}

// profile=1: a thread samples the render thread 2000 times a second - which part of the tick it is in (g_prPhase)
// and which module it is executing (this mod, the d3d9 implementation, the system, anything else: the game). About 1-2%
// overhead (each sample suspends the thread briefly).
static volatile LONG g_prPhase;  // 0 game, 1 game frame executed, 2 in-between frame, 3 matching (waited), 4 rest of ours
static volatile LONG g_prCount[5][4];
static uint32_t g_prHist[2][1 << 16];  // game frame executed / in-between frames: samples per 16 bytes of this mod's code
static uintptr_t g_prRange[3][2];  // mod, d3d9 implementation, system (ntdll/kernel32/kernelbase/win32u)
static DWORD g_prTid;
static void ModuleRange(HMODULE m, uintptr_t* r) {
    r[0] = r[1] = 0;
    if (!m) return;
    auto* nt = (IMAGE_NT_HEADERS*)((uintptr_t)m + ((IMAGE_DOS_HEADER*)m)->e_lfanew);
    r[0] = (uintptr_t)m;
    r[1] = (uintptr_t)m + nt->OptionalHeader.SizeOfImage;
}
static DWORD WINAPI ProfThread(void*) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_prTid);
    if (!th) return 0;
    uintptr_t sys[4][2];
    const char* names[] = {"ntdll.dll", "kernel32.dll", "KernelBase.dll", "win32u.dll"};
    for (int i = 0; i < 4; i++) ModuleRange(GetModuleHandleA(names[i]), sys[i]);
    for (;;) {
        Sleep(0);
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        do { YieldProcessor(); QueryPerformanceCounter(&b); } while ((b.QuadPart - a.QuadPart) * 2000 < g_qpf.QuadPart);
        if (!C.profile) continue;
        if (SuspendThread(th) == (DWORD)-1) continue;
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL;
        int ph = (int)g_prPhase;
        if (GetThreadContext(th, &c)) {
            uintptr_t ip = c.Eip;
            int m = 3;
            if (ip >= g_prRange[0][0] && ip < g_prRange[0][1]) m = 0;
            else if (ip >= g_prRange[1][0] && ip < g_prRange[1][1]) m = 1;
            else
                for (int i = 0; i < 4; i++)
                    if (ip >= sys[i][0] && ip < sys[i][1]) m = 2;
            if (ph >= 0 && ph < 5) InterlockedIncrement(&g_prCount[ph][m]);
            if (m == 0 && (ph == 1 || ph == 2)) g_prHist[ph - 1][((ip - g_prRange[0][0]) >> 4) & 0xFFFF]++;
        }
        ResumeThread(th);
    }
}
static void ProfStart(HMODULE d3d9) {  // at device creation
    static bool started;
    if (started || !C.profile) return;
    started = true;
    MEMORY_BASIC_INFORMATION mb;
    if (VirtualQuery((void*)&ProfStart, &mb, sizeof mb)) ModuleRange((HMODULE)mb.AllocationBase, g_prRange[0]);
    ModuleRange(d3d9, g_prRange[1]);
    g_prTid = GetCurrentThreadId();
    HANDLE h = CreateThread(nullptr, 64 * 1024, ProfThread, nullptr, 0, nullptr);
    if (h) { SetThreadPriority(h, THREAD_PRIORITY_HIGHEST); CloseHandle(h); }
}
// Watchdog: if the render thread makes no progress for 3 s (no Present, no Sleep), log where it is - its instruction
// pointer and two callers (offsets into this mod, or the module they are in), the last D3D call and the wait it is in -
// once per episode. Costs nothing while things run.
static volatile int64_t g_heartbeat;
static DWORD g_tid;  // the render thread (the one that created the device)
static const char* g_lastApi = "none";  // last hooked call seen; tells us what precedes a vtable reset
static const char* volatile g_where = "";
static void WdDescribe(uintptr_t a, char* out, size_t n) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (a && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, name, MAX_PATH);
        const char* leaf = strrchr(name, '\\');
        snprintf(out, n, "%s+%#lx", leaf ? leaf + 1 : name, (unsigned long)(a - (uintptr_t)m));
    } else {
        snprintf(out, n, "%#lx", (unsigned long)a);
    }
}
// The game's memory (a 32-bit process, not large-address-aware: 2 GB, shared with d3d9 / DXVK and this mod): committed,
// address space in use, the largest free block - measured on the watchdog thread (a walk of the address space takes a
// few ms), logged with the statistics. When the largest free block gets small, the mod's optional memory (the rolling
// recorder's tick recordings) is given back, so the game's own allocations keep succeeding.
static volatile double g_memCommit, g_memUsed, g_memFree, g_memLargest, g_memMs;
static volatile int64_t g_memAt;
static void MemoryScan() {
    struct PMC { DWORD cb, faults; SIZE_T peakWs, ws, qpp, qp, qnpp, qnp, pagefile, peakPagefile, priv; };
    typedef BOOL(WINAPI * GPMI)(HANDLE, PMC*, DWORD);
    static GPMI gpmi = (GPMI)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
    int64_t q0 = Qpc();
    PMC pm = {};
    pm.cb = sizeof pm;
    double commit = gpmi && gpmi(GetCurrentProcess(), &pm, sizeof pm) ? pm.priv / 1048576.0 : 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t a = (uintptr_t)si.lpMinimumApplicationAddress, end = (uintptr_t)si.lpMaximumApplicationAddress;
    size_t freeTotal = 0, largest = 0, used = 0;
    MEMORY_BASIC_INFORMATION mi;
    while (a < end && VirtualQuery((void*)a, &mi, sizeof mi)) {
        if (mi.State == MEM_FREE) freeTotal += mi.RegionSize, largest = std::max(largest, (size_t)mi.RegionSize);
        else used += mi.RegionSize;
        uintptr_t next = (uintptr_t)mi.BaseAddress + mi.RegionSize;
        if (next <= a) break;
        a = next;
    }
    g_memCommit = commit;
    g_memUsed = used / 1048576.0;
    g_memFree = freeTotal / 1048576.0;
    g_memLargest = largest / 1048576.0;
    g_memMs = (double)(Qpc() - q0) * 1000.0 / (double)g_qpf.QuadPart;
    g_memAt = Qpc();
}
static DWORD WINAPI WatchdogThread(void*) {
    DWORD tid = 0;  // (a later device can live on another thread: Rain's launcher creates one first)
    HANDLE th = nullptr;
    auto range = [](HMODULE m, uintptr_t& lo, uintptr_t& hi) {
        lo = hi = 0;
        if (!m) return;
        auto dos = (IMAGE_DOS_HEADER*)m;
        auto nt = (IMAGE_NT_HEADERS*)((char*)m + dos->e_lfanew);
        lo = (uintptr_t)m;
        hi = lo + nt->OptionalHeader.SizeOfImage;
    };
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&WatchdogThread, &self);
    uintptr_t modLo, modHi, exeLo, exeHi, beLo = 0, beHi = 0;
    range(self, modLo, modHi);
    range(GetModuleHandleA(nullptr), exeLo, exeHi);
    int64_t reported = 0;
    for (int round = 0;; round++) {
        Sleep(500);
        if (round % 10 == 0) MemoryScan();  // (every 5 s)
        if (g_tid != tid) {
            if (th) CloseHandle(th);
            tid = g_tid;
            th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
        }
        int64_t hb = g_heartbeat;
        if (!th || !hb || hb == reported) continue;
        double stuck = (double)(Qpc() - hb) * 1000.0 / (double)g_qpf.QuadPart;
        if (stuck < 3000.0) continue;
        reported = hb;
        // the thread's instruction pointer, and the return addresses on its stack that point into this mod, the game or the
        // Direct3D backend (DXVK: its frames show where it waits) - a heuristic walk: stale values and pointers to data
        // can appear, the first ones are the useful part
        if (!beLo && g_realD3D9) range(g_realD3D9, beLo, beHi);  // (taken before suspending: no loader lock while it's stopped)
        uintptr_t ip = 0, rets[12];
        int nr = 0;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(th, &c)) {
                ip = c.Eip;
                MEMORY_BASIC_INFORMATION mi = {};
                if (VirtualQuery((void*)c.Esp, &mi, sizeof mi) && mi.State == MEM_COMMIT) {
                    uintptr_t end = std::min<uintptr_t>((uintptr_t)mi.BaseAddress + mi.RegionSize, (uintptr_t)c.Esp + 32768);
                    for (uintptr_t s = c.Esp & ~(uintptr_t)3; s + 4 <= end && nr < 12; s += 4) {
                        uintptr_t v = *(uintptr_t*)s;
                        if ((v >= modLo && v < modHi) || (v >= exeLo && v < exeHi) || (v >= beLo && v < beHi)) rets[nr++] = v;
                    }
                }
            }
            ResumeThread(th);
        }
        char a[160], list[12 * 48] = "";
        WdDescribe(ip, a, sizeof a);
        for (int i = 0; i < nr; i++) {
            char one[160];
            WdDescribe(rets[i], one, sizeof one);
            if (strlen(list) + strlen(one) + 3 < sizeof list) strcat(strcat(list, i ? ", " : ""), one);
        }
        Log("WATCHDOG: render thread without progress for %.1f s - at %s, last D3D call %s, phase %d, waiting in: %s", stuck / 1000.0, a,
            g_lastApi ? g_lastApi : "?", (int)g_prPhase, g_where && *g_where ? g_where : "-");
        Log("  return addresses on its stack: %s", nr ? list : "none in the mod or the game");
    }
}
static void WatchdogStart() {
    static bool started;
    if (started) return;
    started = true;
    HANDLE h = CreateThread(nullptr, 64 * 1024, WatchdogThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}
struct WhereScope {  // names the wait the render thread is in (watchdog)
    const char* old;
    explicit WhereScope(const char* w) : old(g_where) { g_where = w; }
    ~WhereScope() { g_where = old; }
};
struct PhaseScope {  // sets the profiling phase for a scope
    LONG old;
    explicit PhaseScope(LONG p) : old(g_prPhase) { g_prPhase = p; }
    ~PhaseScope() { g_prPhase = old; }
};

static void LoadConfig() {
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\mhfz-fps.ini", g_dir);
    auto I = [&](const char* k, int d) { return (int)GetPrivateProfileIntA("fps", k, d, ini); };
    // a key: a virtual-key code, or a name with modifiers - "ctrl+shift+1", "delete", "f5", "insert", "alt+x" (vk | 0x100
    // ctrl | 0x200 shift | 0x400 alt); 0 or "none" for no key
    auto K = [&](const char* k, int d) {
        char v[64] = "";
        GetPrivateProfileStringA("fps", k, "", v, sizeof v, ini);
        if (!v[0]) return d;
        if (v[0] >= '0' && v[0] <= '9' && !v[1]) return (int)v[0];  // one digit: that number key ("5")
        if (v[0] >= '0' && v[0] <= '9' && v[1] >= '0' && v[1] <= '9') return atoi(v);  // a virtual-key code ("46")
        int mods = 0, vk = 0;
        for (char* tok = strtok(v, "+ "); tok; tok = strtok(nullptr, "+ ")) {
            for (char* c = tok; *c; c++) *c = (char)tolower((unsigned char)*c);
            if (!strcmp(tok, "ctrl") || !strcmp(tok, "control")) mods |= 0x100;
            else if (!strcmp(tok, "shift")) mods |= 0x200;
            else if (!strcmp(tok, "alt")) mods |= 0x400;
            else if (!strcmp(tok, "none")) return 0;
            else if (!tok[1] && ((tok[0] >= '0' && tok[0] <= '9') || (tok[0] >= 'a' && tok[0] <= 'z'))) vk = toupper((unsigned char)tok[0]);
            else if (tok[0] == 'f' && atoi(tok + 1) >= 1 && atoi(tok + 1) <= 24) vk = VK_F1 + atoi(tok + 1) - 1;
            else {
                static const struct { const char* n; int vk; } names[] = {
                    {"delete", VK_DELETE}, {"del", VK_DELETE}, {"insert", VK_INSERT}, {"ins", VK_INSERT}, {"home", VK_HOME}, {"end", VK_END},
                    {"pageup", VK_PRIOR}, {"pgup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"pgdn", VK_NEXT}, {"pause", VK_PAUSE},
                    {"scrolllock", VK_SCROLL}, {"printscreen", VK_SNAPSHOT}, {"backspace", VK_BACK}, {"tab", VK_TAB},
                    {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"space", VK_SPACE}, {"enter", VK_RETURN},
                    {"escape", VK_ESCAPE}, {"esc", VK_ESCAPE}, {"capslock", VK_CAPITAL}, {"numlock", VK_NUMLOCK}, {"mouse4", VK_XBUTTON1},
                    {"mouse5", VK_XBUTTON2}, {"mmb", VK_MBUTTON}, {"multiply", VK_MULTIPLY}, {"add", VK_ADD}, {"subtract", VK_SUBTRACT},
                    {"decimal", VK_DECIMAL}, {"divide", VK_DIVIDE}, {"semicolon", VK_OEM_1}, {"plus", VK_OEM_PLUS}, {"comma", VK_OEM_COMMA},
                    {"minus", VK_OEM_MINUS}, {"period", VK_OEM_PERIOD}, {"slash", VK_OEM_2}, {"backquote", VK_OEM_3}, {"lbracket", VK_OEM_4},
                    {"backslash", VK_OEM_5}, {"rbracket", VK_OEM_6}, {"quote", VK_OEM_7},
                    {"num0", VK_NUMPAD0}, {"num1", VK_NUMPAD1}, {"num2", VK_NUMPAD2}, {"num3", VK_NUMPAD3}, {"num4", VK_NUMPAD4},
                    {"num5", VK_NUMPAD5}, {"num6", VK_NUMPAD6}, {"num7", VK_NUMPAD7}, {"num8", VK_NUMPAD8}, {"num9", VK_NUMPAD9}};
                for (auto& nm : names) if (!strcmp(tok, nm.n)) vk = nm.vk;
                if (!vk) vk = atoi(tok);
            }
        }
        return vk ? (vk | mods) : d;
    };
    C.enabled = I("enabled", C.enabled);
    C.mode = I("mode", C.mode);
    C.target_fps = I("target_fps", C.target_fps);
    C.present_interval = I("present_interval", C.present_interval);
    C.interp_vs = I("interp_vs", C.interp_vs);
    C.interp_xform = I("interp_xform", C.interp_xform);
    C.interp_up = I("interp_up", C.interp_up);
    C.interp_ps = I("interp_ps", C.interp_ps);
    C.snap_ratio = I("snap_ratio_pct", (int)(C.snap_ratio * 100)) / 100.0f;
    C.smooth_ratio = I("smooth_ratio_pct", (int)(C.smooth_ratio * 100)) / 100.0f;
    C.slot_rows = I("slot_rows", C.slot_rows);
    C.margin_us = I("margin_us", C.margin_us);
    C.max_extra = I("max_extra", C.max_extra);
    C.debug_marker = I("debug_marker", C.debug_marker);
    C.stats_interval = I("stats_interval", C.stats_interval);
    C.hotkeys = I("hotkeys", C.hotkeys);
    C.debug_keys = I("debug_keys", C.debug_keys);
    C.debug_unlerped = I("debug_unlerped", C.debug_unlerped);
    C.key_toggle = K("key_toggle", C.key_toggle);
    C.overlay = std::min(2, std::max(0, I("overlay", C.overlay)));
    C.key_overlay = K("key_overlay", C.key_overlay);
    C.overlay_pos = I("overlay_pos", C.overlay_pos);
    C.overlay_scale = std::min(8, std::max(0, I("overlay_scale", C.overlay_scale)));
    C.key_debug = K("key_debug", C.key_debug);
    C.key_mode = K("key_mode", C.key_mode);
    C.key_capture = K("key_capture", C.key_capture);
    C.capture_delay_ms = I("capture_delay_ms", C.capture_delay_ms);
    C.retro = I("retro", C.retro);
    C.retro_frames = I("retro_frames", C.retro_frames);
    C.retro_ticks = I("retro_ticks", C.retro_ticks);
    C.retro_trigger = I("retro_trigger", C.retro_trigger);
    C.precise_tick = I("precise_tick", C.precise_tick);
    C.lerp_nonpos = I("lerp_nonpos", C.lerp_nonpos);
    C.slerp_bones = I("slerp_bones", C.slerp_bones);
    C.debug_cam = I("debug_cam", C.debug_cam);
    C.selfcheck = I("selfcheck", C.selfcheck);
    C.selfcheck_interval_ms = I("selfcheck_interval_ms", C.selfcheck_interval_ms);
    C.selfcheck_saves = I("selfcheck_saves", C.selfcheck_saves);
    C.selfcheck_save_pct = I("selfcheck_save_pct", C.selfcheck_save_pct);
    C.capture_trigger = I("capture_trigger", C.capture_trigger);
    C.dump_trigger = I("dump_trigger", C.dump_trigger);
    C.trace_interval_s = I("trace_interval_s", C.trace_interval_s);
    C.trace_saves = I("trace_saves", C.trace_saves);
    C.trace_trigger = I("trace_trigger", C.trace_trigger);
    C.selfcheck_ablate = I("selfcheck_ablate", C.selfcheck_ablate);
    C.debug_pairhash = I("debug_pairhash", C.debug_pairhash);
    C.debug_replayhash = I("debug_replayhash", C.debug_replayhash);
    C.debug_text = I("debug_text", C.debug_text);
    C.debug_textcands = I("debug_textcands", C.debug_textcands);
    C.text_ckeys = I("text_ckeys", C.text_ckeys);
    C.retro_autosave = I("retro_autosave", C.retro_autosave);
    C.paircap_trigger = I("paircap_trigger", C.paircap_trigger);
    C.pair_threads = I("pair_threads", C.pair_threads);
    C.replay_filter = I("replay_filter", C.replay_filter);
    C.gpu_queue = std::min(1, std::max(0, I("gpu_queue", C.gpu_queue)));
    C.exc_log = I("exc_log", C.exc_log);
    C.regroup = std::min(2, std::max(0, I("regroup", C.regroup)));
    C.regroup_check = I("regroup_check", C.regroup_check);
    C.pair_async = I("pair_async", C.pair_async);
    C.lock_copy = I("lock_copy", C.lock_copy);
    C.flush_endscene = I("flush_endscene", C.flush_endscene);
    C.precompute = I("precompute", C.precompute);
    C.profile = I("profile", C.profile);
    C.shadow_reuse = I("shadow_reuse", C.shadow_reuse);
    C.key_shadow = K("key_shadow", C.key_shadow);
    C.key_nonpos = K("key_nonpos", C.key_nonpos);
    C.cam_paths = I("cam_paths", C.cam_paths);
    C.cam_fit = I("cam_fit", C.cam_fit);
    C.group_cam = I("group_cam", C.group_cam);
    C.latency_trim = I("latency_trim", C.latency_trim);
    C.key_latency = K("key_latency", C.key_latency);
    C.input_latch = I("input_latch", C.input_latch);
    C.key_camprobe = K("key_camprobe", C.key_camprobe);
    C.camprobe_s = std::clamp(I("camprobe_s", C.camprobe_s), 1, 60);
    C.camprobe_test = I("camprobe_test", C.camprobe_test);
    C.camprobe_trigger = I("camprobe_trigger", C.camprobe_trigger);
    C.debug_lost_at = I("debug_lost_at", C.debug_lost_at);
    C.carry_restore = I("carry_restore", C.carry_restore);
    C.late_slot = I("late_slot", C.late_slot);
    C.pacing = I("pacing", C.pacing);
    C.key_campaths = K("key_campaths", C.key_campaths);
    C.field_match = I("field_match", C.field_match);
    C.key_field = K("key_field", C.key_field);
    C.key_carry = K("key_carry", C.key_carry);
    // (keys set by an earlier version, which many keyboards don't have: End, Home, Page Down -> Ctrl+Shift+1 / 3 / 2)
    if (C.key_campaths == VK_END) C.key_campaths = 0x300 | '1';
    if (C.key_nonpos == VK_HOME) C.key_nonpos = 0x300 | '3';
    if (C.key_carry == VK_NEXT) C.key_carry = 0x300 | '2';
    C.text_runs = I("text_runs", C.text_runs);
    C.paircap_count = I("paircap_count", C.paircap_count);
}

// d3d9_backend (read once at load): another Direct3D 9 implementation under the mod instead of Windows' own, e.g.
// DXVK (Direct3D 9 on Vulkan: far less driver work per draw call, which is what limits busy scenes). A path relative
// to this DLL's folder, or absolute. DXVK logs nothing unless dxvk_log=1 (then next to mhfz-fps.log), and reads a
// dxvk.conf from its own folder.
static void SetupBackend() {
    char ini[MAX_PATH], v[MAX_PATH] = "";
    snprintf(ini, sizeof ini, "%s\\mhfz-fps.ini", g_dir);
    GetPrivateProfileStringA("fps", "d3d9_backend", "", v, MAX_PATH, ini);
    if (!v[0]) return;
    // the settings app and setup write UTF-8 - the default install folder holds the Windows user name, which may have
    // an ø or an é - and a file saved in the old ANSI code page still works; the path is used as Unicode from here on
    wchar_t wv[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v, -1, wv, MAX_PATH) && !MultiByteToWideChar(CP_ACP, 0, v, -1, wv, MAX_PATH)) return;
    if ((wv[0] && wv[1] == L':') || (wv[0] == L'\\' && wv[1] == L'\\')) lstrcpynW(g_d3d9BackendW, wv, MAX_PATH);
    else {
        wchar_t dir[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void*)&SetupBackend, &self);
        GetModuleFileNameW(self, dir, MAX_PATH);
        if (wchar_t* s = wcsrchr(dir, L'\\')) *s = 0;
        _snwprintf(g_d3d9BackendW, MAX_PATH, L"%ls\\%ls", dir, wv);
        g_d3d9BackendW[MAX_PATH - 1] = 0;
    }
    WideCharToMultiByte(CP_UTF8, 0, g_d3d9BackendW, -1, g_d3d9Backend, MAX_PATH, nullptr, nullptr);  // (for the log)
    if (GetFileAttributesW(g_d3d9BackendW) == INVALID_FILE_ATTRIBUTES) {
        Log("d3d9_backend %s not found - using Windows' own d3d9.dll", g_d3d9Backend);
        g_d3d9Backend[0] = 0;
        g_d3d9BackendW[0] = 0;
        return;
    }
    if (GetPrivateProfileIntA("fps", "dxvk_log", 0, ini)) {
        SetEnvironmentVariableA("DXVK_LOG_LEVEL", "info");
        SetEnvironmentVariableA("DXVK_LOG_PATH", g_dir);
    } else {
        SetEnvironmentVariableA("DXVK_LOG_LEVEL", "none");
        SetEnvironmentVariableA("DXVK_LOG_PATH", "none");
    }
    wchar_t conf[MAX_PATH];
    lstrcpynW(conf, g_d3d9BackendW, MAX_PATH);
    if (wchar_t* s = wcsrchr(conf, L'\\')) {
        lstrcpynW(s + 1, L"dxvk.conf", MAX_PATH - (int)(s + 1 - conf));
        if (GetFileAttributesW(conf) != INVALID_FILE_ATTRIBUTES) SetEnvironmentVariableW(L"DXVK_CONFIG_FILE", conf);
    }
}

// Live reload: the ini is re-read whenever it changes on disk, so settings can be flipped (and captures /
// dumps triggered via capture_trigger / dump_trigger) while the game runs.
static FILETIME g_iniTime;
static int64_t g_iniChecked;
static bool IniChanged() {
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\mhfz-fps.ini", g_dir);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(ini, GetFileExInfoStandard, &fa)) return false;
    bool changed = CompareFileTime(&fa.ftLastWriteTime, &g_iniTime) != 0;
    g_iniTime = fa.ftLastWriteTime;
    return changed;
}

// ------------------------------------------------------------------------------------------------ hook plumbing
static void* HookVT(void** vt, int idx, void* fn) {
    void* old = vt[idx];
    if (old == fn) return nullptr;
    DWORD p;
    VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &p);
    vt[idx] = fn;
    VirtualProtect(&vt[idx], sizeof(void*), p, &p);
    return old;
}

static void InputSample();
typedef IDirect3DDevice9 Dev;
#define HOOKDECL(ret, name, ...)                              \
    typedef ret(WINAPI* name##_t)(Dev * d, ##__VA_ARGS__);    \
    static name##_t o_##name;                                 \
    static ret WINAPI hk_##name(Dev* d, ##__VA_ARGS__)
#define HOOK(ret, name, ...) HOOKDECL(ret, name, ##__VA_ARGS__); static ret WINAPI hk_##name(Dev* d, ##__VA_ARGS__)
#define HOOKDEF(ret, name, ...) static ret WINAPI hk_##name(Dev* d, ##__VA_ARGS__)

static Dev* g_dev;
static HWND g_hwnd;
static bool g_lost, g_inInterp, g_sbRecording, g_dumpNext;
static char g_dumpTo[MAX_PATH];  // where the next dump goes (empty: mhfz-fps-dump.txt)
static D3DPRESENT_PARAMETERS g_pp;
static double g_refresh = 60.0;

// stats
static struct {
    int64_t tLast;
    uint64_t presents, ticks, sleeps, interpFrames, missed, foreign, sbApply, sbCapture, sbCreate, sbBegin,
        updates, rtData, matched, unmatched, pairs, snaps, draws, cmdBytes, lateExit, upSnaps, unpairedPos, postSleepDraws, cuts,
        gameLate, backoffTicks, gpuTailN, synth, rejected, crossing, verifyRejected, screenRefined, screenDropped, synthBillboard,
        steadyLerps, screenSynth, livePairs, minimizedTicks, constSent, constSkipped, stateSent, stateSkipped, gpuRealN, gpuInterpN,
        rgWins, rgIn, rgOut, rgFlushes, rgFrames, rgLWins, rgLIn, rgLOut, rgLocal, replayCmds, rgBuilds, cmdCount,
        pjUsed, pjStale, rgReuse, srTicks, srFrozen, srSnaps, srOff[16], tlFlushes, noTime;
    double rgBuildMs, pjMs, pjWaitMs, endFlushMs, rgAnaMs, srMs;
    uint64_t rgEnd[6];    // why windows of the in-between frames' analysis ended (see RgEvent)
    uint64_t rgWhy[216];  // what made the deferred game frame execute early (see WhyName)
    double rgWhyMs[216];  // and how long it ran there
    double pvMs;          // in-between frames: precomputing the constants
    uint64_t carryInputs, atlasSnaps, ckeyGlyphs, slotJumps, slotRows, fieldGroups;
    uint64_t textLayout, textNoLayout;
    uint64_t textRuns, textRunQuads, textRunMoved, textGuesses, textGeo, textLeft, camCtx, camSolved, camPath, camPathPairs, carryRestores, camShared, camJoined, camFit, groupsRescued, groupsAgain, camAnchor;
    uint64_t piN, piFast;          // presents: intervals measured, closer than one refresh
    double piSum, piSum2, piMax;
    uint64_t lockCopies, lockCopyFails;
    double gpuRealMs, gpuInterpMs;
    double replayMs, presentMs, tickMs, logicMs, pairMs, gpuTailMs;
    double lateMs;        // in-between frames that ended past the game's deadline: by how much in all
    uint64_t gpuWaitCut;  // waits for the GPU cut short at the deadline
    double ltHeldMs;      // latency_trim: holds after the real frame
    uint64_t ltTicks;
    uint64_t inputLatched, inputSamples;
    uint64_t lostReports, lostResumes;  // Presents that said "device lost"; times the in-between frames came back without a Reset  // input_latch: taps handed to the game's next read; device reads while it slept
    double phaseMs[16];  // BuildPairs phases (see kPhase)
    uint64_t replayN, tickN;
    uint64_t ops[64];
} St;
// Where BuildPairs spends its time (stats line "pair phases"): each Phase(k) charges the time since the last mark.
static const char* kPhase[] = {"setup", "passes+text", "live", "group", "refine", "verify", "pair", "cam+synth", "screen", "sort",
                               "r.groups", "r.consist", "r.setup", "g.ones", "g.multi"};
static int64_t g_phaseT;
static inline void Phase(int k) {
    int64_t now = Qpc();
    St.phaseMs[k] += Ms(now - g_phaseT);
    g_phaseT = now;
}

static bool g_afterSleep;  // between the limiter Sleep and the real Present

// ------------------------------------------------------------------------------------------------ recording
enum : uint16_t {
    OP_NONE, OP_RS, OP_SS, OP_TSS, OP_TEX, OP_VS, OP_PS, OP_DECL, OP_FVF, OP_VSCF, OP_VSCI, OP_VSCB, OP_PSCF,
    OP_PSCI, OP_PSCB, OP_SSRC, OP_SSFREQ, OP_IDX, OP_RT, OP_DS, OP_VP, OP_SCISSOR, OP_XFORM, OP_MULXFORM, OP_MAT,
    OP_LIGHT, OP_LENABLE, OP_CLIP, OP_CLEAR, OP_DP, OP_DIP, OP_DPUP, OP_DIPUP, OP_STRETCH, OP_COLORFILL, OP_BEGIN,
    OP_END, OP_SWVP, OP_SBAPPLY, OP_SBCAPTURE, OP_NPATCH, OP_TEXSUB, OP_MAX
};
static const char* kOp[OP_MAX] = {"none", "RS", "SS", "TSS", "TEX", "VS", "PS", "DECL", "FVF", "VSCF", "VSCI",
    "VSCB", "PSCF", "PSCI", "PSCB", "SSRC", "SSFREQ", "IDX", "RT", "DS", "VP", "SCISSOR", "XFORM", "MULXFORM", "MAT",
    "LIGHT", "LENABLE", "CLIP", "CLEAR", "DP", "DIP", "DPUP", "DIPUP", "STRETCH", "COLORFILL", "BEGIN", "END", "SWVP",
    "SBAPPLY", "SBCAPTURE", "NPATCH", "TEXSUB"};

struct Cmd { uint16_t op, flags; uint32_t size; };
struct P2 { DWORD a, b; };
struct P3 { DWORD a, b, c; };
struct PPtr { DWORD a; IUnknown* p; };
struct PConst { UINT start, count, ord; };  // ord: index of this VSCF packet within the frame
struct PSsrc { UINT stream; IDirect3DVertexBuffer9* vb; UINT off, stride; };
struct PXform { DWORD state; D3DMATRIX m; };
struct PLight { DWORD idx; D3DLIGHT9 l; };
struct PClip { DWORD idx; float p[4]; };
struct PClear { DWORD count, flags; D3DCOLOR color; float z; DWORD stencil; };
struct PDP { D3DPRIMITIVETYPE type; UINT start, count; };
struct PDIP { D3DPRIMITIVETYPE type; INT base; UINT minIdx, numV, startIdx, primCount; };
struct PDPUP { D3DPRIMITIVETYPE type; UINT primCount, stride, vbytes; int16_t posOff; uint8_t posN, rhw; };
struct PDIPUP { D3DPRIMITIVETYPE type; UINT minIdx, numV, primCount; D3DFORMAT fmt; UINT stride, ibytes, vbytes; int16_t posOff; uint8_t posN, rhw; };
struct PStretch { IDirect3DSurface9 *src, *dst; RECT sr, dr; BOOL hasSr, hasDr; D3DTEXTUREFILTERTYPE f; };
struct PFill { IDirect3DSurface9* s; RECT r; BOOL hasR; D3DCOLOR c; };
// Where the game rewrote a texture that deferred draws before it sample (LockCopy): those draws bind the copy made
// just before; from here on what is bound to the copy binds `next` (the copy of the next rewrite, or the texture).
struct PTexSub { IDirect3DTexture9 *tex, *clone, *next; };

static const uint32_t NONE = 0xFFFFFFFFu;

// ------------------------------------------------------------------------------------------------ render-pass regrouping
// MHF draws every shadow caster into its three shadow cascades in turn (target A, B, C, A, B, C..., one draw each)
// and every character into two depth targets the same way: 1200-4800 render target switches a frame. Each switch
// drains the GPU (8-19 us at these sizes, Windows' d3d9 and DXVK alike), so a busy town took 25-30 ms of GPU time
// for about 1 ms of drawing. Drawn one target after the other, the same draws cost almost nothing (test\rtswitch).
// A window of such interleaved passes is regrouped only where that cannot change the image: its targets share no
// surface, none of its draws samples a texture the window renders to, and nothing but states, draws and clears
// happens inside it (copies, state blocks and BeginScene/EndScene end a window). Each target keeps the order of its
// draws, and every draw sees exactly the state it had (RunWindow).
struct Bind {
    void* s[5];  // render targets 0-3, depth-stencil
    bool operator==(const Bind& o) const { return memcmp(s, o.s, sizeof s) == 0; }
    bool operator!=(const Bind& o) const { return !(*this == o); }
    bool Disjoint(const Bind& o) const {
        for (void* a : s)
            if (a)
                for (void* b : o.s)
                    if (a == b) return false;
        return true;
    }
};
struct RWin {
    uint32_t beg, end;    // commands [beg, end): from just after the event before the window to just after its last
    Bind start;           // what is bound at beg
    Bind b[4];            // its targets, in order of first use
    uint32_t lastEv[4];   // just after the last draw or clear of each target
    int nb;
    uint32_t passes, events;  // target switches between its draws and clears as recorded, and their number
    // per pass, the commands it runs (BuildPassLists; offset, | 1 = a render target 0 switch whose viewport and
    // scissor reset is needed), and how the window ends
    mutable bool built = false, s0Reset = false, idxReset = false;
    mutable bool s0ResetAll = false, idxResetAll = false;  // (when none of its draws run)
    mutable int8_t shadowCand = -1;   // which of its targets look like shadow cascades (ShadowTargets)
    mutable Bind endBind{};
    mutable std::vector<uint32_t> list[4];
};
// Streaming analysis of a frame's recording (RgAnalyze): what is bound, which textures, which pixel shader.
struct Regroup {
    uint32_t off = 0;      // next command to analyse
    Bind cur{};
    void* tex[21] = {};
    void* ps = nullptr;
    uint32_t lastEnd = 0;  // just after the last event or barrier: where a new window begins
    Bind lastBind{};
    bool open = false, dead = false;
    bool live = false;     // the deferred game frame's analysis (else the in-between frames')
    RWin w{};
    Bind prevEv{};
    std::vector<void*> owned, sampled;  // textures the open window renders to / samples
    std::vector<RWin> wins;             // closed windows worth regrouping
    void Reset() {
        off = lastEnd = 0;
        cur = lastBind = prevEv = Bind{};
        memset(tex, 0, sizeof tex);
        ps = nullptr;
        open = dead = false;
        owned.clear();
        sampled.clear();
        wins.clear();
    }
};

struct DrawRec {
    uint32_t off, seg;  // the draw command, and where its segment (commands since the previous draw) starts
    uint64_t key;       // strict identity: geometry + shader + render target
    uint64_t key2;      // loose identity: geometry only (fallback when the strict key finds nothing)
    void* vs;           // vertex shader bound at the draw
    uint32_t sig;       // last VSCF command in the segment (identifies which instance of a mesh this is)
    void* rt;           // render target 0 at the draw (the pass)
};
// A glyph atlas as a screen quad sampled it (a CPU copy of level 0; shared by the frames that used this version)
struct AtlasSnap { uint32_t w = 0, h = 0, serial = 0; std::vector<uint32_t> px; };
static volatile LONG g_texLockGen;  // (counts locks of glyph atlases: a snapshot older than the last one may be stale)
static void* volatile g_atlasPtr[8];  // (the glyph atlases seen: written by the render thread, read by any lock)
static volatile LONG g_atlasN;
static std::shared_ptr<const AtlasSnap> AtlasSnapshot(IDirect3DTexture9* tx);
struct GlyphSrc { uint32_t draw; uint16_t snap; int16_t uvOff; uint64_t base; };
struct Frame {
    std::vector<uint8_t> buf;
    std::vector<IUnknown*> refs;
    std::vector<DrawRec> draws;
    IDirect3DStateBlock9* sb = nullptr;
    IDirect3DSurface9* rt[4] = {};
    IDirect3DSurface9* ds = nullptr;
    void* startVS = nullptr;
    void* startPS = nullptr;
    bool valid = false;
    uint32_t seg = 0, nvscf = 0, lastSig = NONE;
    uint32_t segVscf[16];  // VSCF commands in the current segment (newest last)
    int nSegVscf = 0;
    IDirect3DSurface9* img = nullptr;  // self-check: copy of this frame as really presented (kept across Clear)
    bool hasImg = false;
    int64_t presentAt = 0;             // QPC of its real Present
    uint32_t serial = 0;               // which recorded frame this is (for last tick's pairing history)
    Regroup rg;   // the deferred game frame's: windows also end wherever it had to be executed early
    Regroup rgR;  // the in-between frames': only the recording's own barriers
    bool rgLive = false;  // the in-between frames use rg (it has no other barriers)
    void* startTex[21] = {};  // textures bound when the frame started
    std::vector<uint32_t> texCmds;  // its SetTexture commands (LockCopy)
    std::vector<uint32_t> subs;     // its OP_TEXSUB markers
    std::vector<void*> clones;      // the copies they use (kept from reuse while the frame is)
    std::vector<std::shared_ptr<const AtlasSnap>> snaps;  // the glyph atlas versions its screen quads sampled
    std::vector<GlyphSrc> glyphs;   // its screen quads that sampled one (TextKeys)
    std::vector<uint64_t> ckeys;    // per draw: a glyph by its pixels (0: none), made by the matching
    bool ckeysDone = false;
    const Regroup& RgReplay() const { return rgLive ? rg : rgR; }
    Frame() { rg.live = true; }
    void Clear() {
        for (auto* u : refs) u->Release();
        refs.clear();
        buf.clear();
        draws.clear();
        for (auto*& r : rt) if (r) { r->Release(); r = nullptr; }
        if (ds) { ds->Release(); ds = nullptr; }
        valid = false;
        hasImg = false;
        seg = 0;
        nvscf = 0;
        lastSig = NONE;
        nSegVscf = 0;
        rg.Reset();
        rgR.Reset();
        rgLive = false;
        texCmds.clear();
        subs.clear();
        clones.clear();
        snaps.clear();
        glyphs.clear();
        ckeys.clear();
        ckeysDone = false;
    }
};
static Frame g_fr[3];  // ring: g_cur = recording, PrevIdx(1) = last frame, PrevIdx(2) = the one before
static int g_cur;
static inline int PrevIdx(int back) { return (g_cur + 3 - back) % 3; }
static uint32_t g_lastOff;

// shadow of the bits of device state that identify a draw
static struct {
    void *vs, *ps, *decl, *vb0, *ib, *tex0, *rt0;
    DWORD fvf;
    UINT stride0, off0;
    bool useFvf;
} S;

// The game's own frame, deferred (regroup=2): from the start of a frame the recording hooks only record and return
// D3D_OK; the recording is executed - interleaved passes regrouped - when anything else touches the device
// (FlushDeferred: any other device method, a resource lock, a query, Sleep, EndScene, Present).
extern "C" {
volatile uint8_t g_defer;
}
enum { kWhyEndScene = 200, kWhyPresent, kWhySleep, kWhyCopy, kWhyUpdate, kWhySB, kWhyReset, kWhyTexLock, kWhyCount };
static void FlushDeferred(int why);
static void LiveAfterFlush();
static inline int SamplerSlot(DWORD s);
// MHF reads states back around most draws (GetRenderState, GetViewport, GetTransform: ~4700 reads a frame in a
// crowd). Those are answered from what the deferred calls set (g_pendGen: since the last flush); a state no deferred
// call touched is still what the device holds.
static uint32_t g_pendGen = 1;
static uint32_t g_rsGen[256], g_vpGen, g_xfGen[512];
static DWORD g_rsVal[256];
static D3DVIEWPORT9 g_vpVal;
static D3DMATRIX g_xfVal[512];
static uint8_t g_xfMul[512];  // a deferred MultiplyTransform: the value is not known without the device
// the same for textures, render targets, depth-stencil and sampler states (GetTexture alone: ~20 reads a frame)
static uint32_t g_texGen[21], g_rtGen[4], g_dsGen, g_ssGen[21][14];
static void *g_texVal[21], *g_rtVal[4], *g_dsVal;
static DWORD g_ssVal[21][14];
// A texture lock only has to execute the deferred frame first when a deferred draw may sample that texture: one
// bound at the last flush or bound since (g_pendTex), and only if a draw has been deferred since (g_pendDraws).
static std::vector<void*> g_pendTex;
static uint32_t g_pendDraws;
static bool g_pendTexAll;  // (what was bound at the last flush is not known: any texture counts)
// matching in the background (PairJobStart at EndScene): anything else the render thread does first waits for it
static volatile LONG g_pjBusy;
static void PairJoin();

static inline bool Rec(Dev* d) {
    if (d != g_dev || g_inInterp) return false;
    if (GetCurrentThreadId() != g_tid) { St.foreign++; return false; }
    return !g_sbRecording && g_fr[g_cur].valid;
}

static uint8_t* Push(uint16_t op, uint32_t payload) {
    Frame& f = g_fr[g_cur];
    uint32_t sz = (uint32_t)((sizeof(Cmd) + payload + 3) & ~3u);
    size_t off = f.buf.size();
    if (f.buf.capacity() < off + sz) f.buf.reserve(std::max<size_t>(f.buf.capacity() * 2, off + sz + (1 << 20)));
    f.buf.resize(off + sz);
    Cmd* c = (Cmd*)&f.buf[off];
    c->op = op;
    c->flags = 0;
    c->size = sz;
    g_lastOff = (uint32_t)off;
    St.ops[op & 63]++;
    St.cmdCount++;
    return (uint8_t*)(c + 1);
}
static inline void Ref(IUnknown* u) {
    if (u) { u->AddRef(); g_fr[g_cur].refs.push_back(u); }
}
static inline uint64_t Fnv(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; i++) { h ^= (v >> (i * 8)) & 0xff; h *= 1099511628211ull; }
    return h;
}
static uint64_t BaseKey(uint32_t kind) {
    uint64_t h = 1469598103934665603ull;
    h = Fnv(h, kind);
    h = Fnv(h, (uintptr_t)S.vs);
    h = Fnv(h, (uintptr_t)S.ps);
    h = Fnv(h, S.useFvf ? S.fvf : (uintptr_t)S.decl);
    h = Fnv(h, (uintptr_t)S.tex0);
    h = Fnv(h, (uintptr_t)S.rt0);
    return h;
}
// strict key for buffer-based draws: no texture / pixel shader (animated textures and material swaps must not
// break matching)
static uint64_t GeoKey(uint32_t kind) {
    uint64_t h = 1469598103934665603ull;
    h = Fnv(h, kind);
    h = Fnv(h, (uintptr_t)S.vs);
    h = Fnv(h, S.useFvf ? S.fvf : (uintptr_t)S.decl);
    h = Fnv(h, (uintptr_t)S.rt0);
    return h;
}
static const VSInfo* GetVSInfo(void* vs);
static void AddDraw(uint64_t key, uint64_t key2) {
    Frame& f = g_fr[g_cur];
    // instance signature: the newest transform-sized constant packet of this segment that this draw's vertex
    // shader uses for its position
    uint32_t sig = NONE;
    const VSInfo* vi = S.vs ? GetVSInfo(S.vs) : nullptr;
    for (int i = f.nSegVscf - 1; i >= 0 && sig == NONE; i--) {
        auto* q = (const PConst*)((const Cmd*)(f.buf.data() + f.segVscf[i]) + 1);
        if (q->count < 3) continue;
        if (!vi || vi->all) { sig = f.segVscf[i]; break; }
        for (UINT r = q->start; r < q->start + q->count; r++)
            if (vi->pos.test((int)r)) { sig = f.segVscf[i]; break; }
    }
    f.draws.push_back({g_lastOff, f.seg, key, key2, S.vs, sig, S.rt0});
    if (g_defer) g_pendDraws++;
    f.seg = (uint32_t)f.buf.size();
    f.lastSig = NONE;
    f.nSegVscf = 0;
    if (g_afterSleep) St.postSleepDraws++;
}

// Vertex layout of the current FVF/declaration: where the position is, and which bytes identify a
// sprite (texcoords, normals...; not position or colour) for UP-draw content matching.
struct Layout {
    int16_t posOff = -1;
    int16_t uvOff = -1;  // texture coordinates 0 (two floats), if any
    uint8_t posN = 0;
    bool rhw = false;
    uint8_t nr = 0;
    struct { uint16_t off, len; } r[12];
    void keep(int off, int len) { if (nr < 12 && len > 0) r[nr++] = {(uint16_t)off, (uint16_t)len}; }
};
static std::unordered_map<uint64_t, Layout> g_layouts;
static int DeclTypeSize(int t) {
    switch (t) {
    case D3DDECLTYPE_FLOAT1: return 4;
    case D3DDECLTYPE_FLOAT2: return 8;
    case D3DDECLTYPE_FLOAT3: return 12;
    case D3DDECLTYPE_FLOAT4: return 16;
    case D3DDECLTYPE_SHORT4: case D3DDECLTYPE_SHORT4N: case D3DDECLTYPE_USHORT4N: case D3DDECLTYPE_FLOAT16_4: return 8;
    default: return 4;
    }
}
static const Layout& CurLayout() {
    uint64_t k = S.useFvf ? ((uint64_t)S.fvf | (1ull << 40)) : (uint64_t)(uintptr_t)S.decl;
    auto it = g_layouts.find(k);
    if (it != g_layouts.end()) return it->second;
    Layout L;
    if (S.useFvf) {
        DWORD f = S.fvf, pm = f & D3DFVF_POSITION_MASK;
        int off = 0;
        if (pm == D3DFVF_XYZ) off = 12;
        else if (pm == D3DFVF_XYZRHW) { off = 16; L.rhw = true; }
        else if (pm == D3DFVF_XYZW) off = 16;
        else if (pm >= D3DFVF_XYZB1 && pm <= D3DFVF_XYZB5) off = 12 + 4 * (int)((pm - D3DFVF_XYZB1) / 2 + 1);
        if (off) { L.posOff = 0; L.posN = 3; }
        if (f & D3DFVF_NORMAL) { L.keep(off, 12); off += 12; }
        if (f & D3DFVF_PSIZE) { L.keep(off, 4); off += 4; }
        if (f & D3DFVF_DIFFUSE) off += 4;
        if (f & D3DFVF_SPECULAR) off += 4;
        int ntex = (int)((f & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
        for (int t = 0; t < ntex; t++) {
            int fmt = (int)((f >> (16 + t * 2)) & 3);
            int sz = fmt == 0 ? 8 : fmt == 1 ? 12 : fmt == 2 ? 16 : 4;
            if (t == 0 && fmt == 0) L.uvOff = (int16_t)off;
            L.keep(off, sz);
            off += sz;
        }
    } else if (S.decl) {
        D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1];
        UINT cnt = MAXD3DDECLLENGTH + 1;
        if (SUCCEEDED(((IDirect3DVertexDeclaration9*)S.decl)->GetDeclaration(el, &cnt))) {
            for (UINT i = 0; i < cnt && el[i].Stream != 0xFF; i++) {
                if (el[i].Stream != 0) continue;
                if ((el[i].Usage == D3DDECLUSAGE_POSITION || el[i].Usage == D3DDECLUSAGE_POSITIONT) && el[i].UsageIndex == 0 &&
                    (el[i].Type == D3DDECLTYPE_FLOAT3 || el[i].Type == D3DDECLTYPE_FLOAT4)) {
                    L.posOff = (int16_t)el[i].Offset;
                    L.posN = 3;
                    L.rhw = el[i].Usage == D3DDECLUSAGE_POSITIONT;
                } else if (el[i].Usage != D3DDECLUSAGE_COLOR) {
                    if (el[i].Usage == D3DDECLUSAGE_TEXCOORD && el[i].UsageIndex == 0 && el[i].Type == D3DDECLTYPE_FLOAT2) L.uvOff = (int16_t)el[i].Offset;
                    L.keep(el[i].Offset, DeclTypeSize(el[i].Type));
                }
            }
        }
    }
    return g_layouts[k] = L;
}
// hash of the identifying (non-position, non-colour) bytes of all vertices
static uint32_t ContentHash(const Layout& L, const uint8_t* v, UINT nverts, UINT stride) {
    uint32_t h = 2166136261u ^ nverts;
    for (UINT i = 0; i < nverts; i++, v += stride) {
        for (int k = 0; k < L.nr; k++) {
            int end = std::min<int>(L.r[k].off + L.r[k].len, (int)stride);
            for (int o = L.r[k].off; o + 4 <= end; o += 4) {
                uint32_t w;
                memcpy(&w, v + o, 4);
                h = (h ^ w) * 16777619u;
            }
        }
    }
    return h;
}
static UINT VertsFor(D3DPRIMITIVETYPE t, UINT n) {
    switch (t) {
    case D3DPT_POINTLIST: return n;
    case D3DPT_LINELIST: return n * 2;
    case D3DPT_LINESTRIP: return n + 1;
    case D3DPT_TRIANGLELIST: return n * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return n + 2;
    default: return 0;
    }
}

// ------------------------------------------------------------------------------------------------ device hooks
HOOKDECL(HRESULT, Present, const RECT* s, const RECT* dr, HWND w, const RGNDATA* rg);
HOOKDECL(HRESULT, Reset, D3DPRESENT_PARAMETERS* pp);
HOOKDECL(HRESULT, EndScene);
static void PairJobStart();
static void NoteLost(const char* whose);
// An overlay that hooks the device vtable after us keeps our hook as "its original". When CheckHooks then puts our
// hook back on top, o_Present is the overlay's and the two would call each other until the stack overflows. A hook
// that is re-entered like that goes straight to the runtime's function (recorded at the first hook) instead.
static Present_t r_Present;
static Reset_t r_Reset;
static EndScene_t r_EndScene;
static int g_inPresent, g_inReset, g_inEndScene;
static bool CheckHooks(const char* where);
static void HookDeviceVT(Dev* dev);
static void HookSB(IDirect3DStateBlock9* sb);

// Crash note: a fault inside this DLL, or anywhere while an in-between frame is being drawn, is written to the log
// (players can send mhfz-fps.log). Nothing is handled here; the game's own handling continues as before. Vectored
// handlers also see exceptions that are handled later (first chance), hence only these two cases and only 8 notes.
static uintptr_t g_modBase, g_modEnd, g_codeBeg, g_codeEnd;
// exc_log=1: every exception of the process, logged before any other handler sees it (and then passed on
// untouched) - which kinds a game's own protection raises and handles, and where (at most 200 lines)
static void ModOf(uintptr_t a, char* out, size_t n) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* leaf = strrchr(path, '\\');
        const char* name = leaf ? leaf + 1 : path;
        // (the backend - DXVK or Windows' own - is a d3d9.dll too: marked, so tools\wdsym.py only resolves the mod's)
        bool backend = (uintptr_t)m != g_modBase && !_stricmp(name, "d3d9.dll");
        snprintf(out, n, "%s%s+%#lx", name, backend ? "[backend]" : "", (unsigned long)(a - (uintptr_t)m));
    } else snprintf(out, n, "%p", (void*)a);
}
static LONG CALLBACK ExcLog(EXCEPTION_POINTERS* e) {
    static volatile LONG lines;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0x40010006 || code == 0x4001000A) return EXCEPTION_CONTINUE_SEARCH;  // thread names, debug prints
    if (InterlockedIncrement(&lines) > 200) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t ip = (uintptr_t)e->ExceptionRecord->ExceptionAddress;
    char where[160], target[160] = "", calls[400] = "";
    ModOf(ip, where, sizeof where);
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == STATUS_GUARD_PAGE_VIOLATION) && e->ExceptionRecord->NumberParameters > 1) {
        char tm[140];
        ModOf((uintptr_t)e->ExceptionRecord->ExceptionInformation[1], tm, sizeof tm);
        ULONG_PTR kind = e->ExceptionRecord->ExceptionInformation[0];
        snprintf(target, sizeof target, " %s %s", kind == 0 ? "read" : kind == 1 ? "write" : "exec", tm);
    }
    // the first few return addresses into loaded modules on the stack (a hint at the call path)
    uintptr_t* sp = (uintptr_t*)e->ContextRecord->Esp;
    uintptr_t top = (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
    int found = 0;
    size_t len = 0;
    for (int k = 0; k < 256 && (uintptr_t)(sp + k + 1) <= top && found < 6; k++) {
        HMODULE m = nullptr;
        if (sp[k] < 0x10000 || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)sp[k], &m) || !m) continue;
        char mo[140];
        ModOf(sp[k], mo, sizeof mo);
        len += snprintf(calls + len, sizeof calls - len, " %s", mo);
        found++;
    }
    Log("exception %08lx at %s%s tid %lu%s | stack:%s", code, where, target, GetCurrentThreadId(),
        GetCurrentThreadId() == g_tid ? " (render)" : "", calls);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK CrashNote(EXCEPTION_POINTERS* e) {
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t ip = (uintptr_t)e->ExceptionRecord->ExceptionAddress;
    bool ours = ip >= g_modBase && ip < g_modEnd;
    // a fault in the C runtime, d3d9 or the driver while the mod's code is on the stack (a return address into the
    // mod among the first 1024 stack words of the faulting thread)
    uintptr_t caller = 0;
    if (!ours) {
        uintptr_t* sp = (uintptr_t*)e->ContextRecord->Esp;
        uintptr_t top = (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
        for (int k = 0; k < 1024 && (uintptr_t)(sp + k + 1) <= top; k++)
            if (sp[k] >= g_codeBeg && sp[k] < g_codeEnd) { caller = sp[k]; break; }
    }
    static volatile LONG notes;
    if ((ours || caller || g_inInterp) && InterlockedIncrement(&notes) <= 8) {
        char mod[MAX_PATH] = "?";
        HMODULE m = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ip, &m) && m)
            GetModuleFileNameA(m, mod, MAX_PATH);
        const char* leaf = strrchr(mod, '\\');
        char where[64];
        if (ours) snprintf(where, sizeof where, "in the mod");
        else if (caller) snprintf(where, sizeof where, "called from the mod at +%#lx", (unsigned long)(caller - g_modBase));
        else snprintf(where, sizeof where, "while drawing an in-between frame");
        Log("EXCEPTION %08lx at %s+%#lx (%s, %s%s), last api %s, address %p (may be handled by the game if it continues)",
            code, leaf ? leaf + 1 : mod, (unsigned long)(ip - (uintptr_t)m), where,
            GetCurrentThreadId() == g_tid ? "render thread" : "other thread", g_inInterp ? ", in-between frame" : "", g_lastApi,
            code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters > 1 ? (void*)e->ExceptionRecord->ExceptionInformation[1] : nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static uint64_t g_presentCalls;
HOOK(HRESULT, BeginScene) {
    g_lastApi = "BeginScene";
    if (Rec(d)) { Push(OP_BEGIN, 0); if (g_defer) return D3D_OK; }
    return o_BeginScene(d);
}
HOOKDEF(HRESULT, EndScene) {
    if (g_inEndScene && r_EndScene) return r_EndScene(d);
    g_inEndScene++;
    g_lastApi = "EndScene";
    HRESULT hr;
    if (g_defer && Rec(d)) {  // deferred like the rest (MHF draws a second scene after the first)
        Push(OP_END, 0);
        if (C.flush_endscene) FlushDeferred(kWhyEndScene);
        hr = D3D_OK;
    } else {
        FlushDeferred(kWhyEndScene);
        if (Rec(d)) { Push(OP_END, 0); LiveAfterFlush(); }
        hr = o_EndScene(d);
    }
    g_inEndScene--;
    return hr;
}
HOOK(HRESULT, Clear, DWORD n, const D3DRECT* r, DWORD fl, D3DCOLOR c, float z, DWORD st) {
    g_lastApi = "Clear";
    if (Rec(d)) {
        DWORD cnt = r ? n : 0;
        auto* p = (PClear*)Push(OP_CLEAR, sizeof(PClear) + cnt * sizeof(D3DRECT));
        *p = {cnt, fl, c, z, st};
        if (cnt) memcpy(p + 1, r, cnt * sizeof(D3DRECT));
        if (g_defer) return D3D_OK;
    }
    return o_Clear(d, n, r, fl, c, z, st);
}
HOOK(HRESULT, SetTransform, D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) {
    if (Rec(d) && m) {
        auto* p = (PXform*)Push(OP_XFORM, sizeof(PXform));
        p->state = s;
        p->m = *m;
        if (g_defer) {
            if ((unsigned)s < 512) { g_xfVal[s] = *m; g_xfMul[s] = 0; g_xfGen[s] = g_pendGen; }
            return D3D_OK;
        }
    }
    return o_SetTransform(d, s, m);
}
HOOK(HRESULT, MultiplyTransform, D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) {
    if (Rec(d) && m) {
        auto* p = (PXform*)Push(OP_MULXFORM, sizeof(PXform));
        p->state = s;
        p->m = *m;
        if (g_defer) {
            if ((unsigned)s < 512) { g_xfMul[s] = 1; g_xfGen[s] = g_pendGen; }
            return D3D_OK;
        }
    }
    return o_MultiplyTransform(d, s, m);
}
HOOK(HRESULT, SetViewport, const D3DVIEWPORT9* v) {
    if (Rec(d) && v) {
        *(D3DVIEWPORT9*)Push(OP_VP, sizeof(D3DVIEWPORT9)) = *v;
        if (g_defer) { g_vpVal = *v; g_vpGen = g_pendGen; return D3D_OK; }
    }
    return o_SetViewport(d, v);
}
HOOK(HRESULT, SetMaterial, const D3DMATERIAL9* m) {
    if (Rec(d) && m) { *(D3DMATERIAL9*)Push(OP_MAT, sizeof(D3DMATERIAL9)) = *m; if (g_defer) return D3D_OK; }
    return o_SetMaterial(d, m);
}
HOOK(HRESULT, SetLight, DWORD i, const D3DLIGHT9* l) {
    if (Rec(d) && l) { auto* p = (PLight*)Push(OP_LIGHT, sizeof(PLight)); p->idx = i; p->l = *l; if (g_defer) return D3D_OK; }
    return o_SetLight(d, i, l);
}
HOOK(HRESULT, LightEnable, DWORD i, BOOL e) {
    if (Rec(d)) { *(P2*)Push(OP_LENABLE, sizeof(P2)) = {i, (DWORD)e}; if (g_defer) return D3D_OK; }
    return o_LightEnable(d, i, e);
}
HOOK(HRESULT, SetClipPlane, DWORD i, const float* pl) {
    if (Rec(d) && pl) { auto* p = (PClip*)Push(OP_CLIP, sizeof(PClip)); p->idx = i; memcpy(p->p, pl, 16); if (g_defer) return D3D_OK; }
    return o_SetClipPlane(d, i, pl);
}
HOOK(HRESULT, SetRenderState, D3DRENDERSTATETYPE s, DWORD v) {
    if (Rec(d)) {
        *(P2*)Push(OP_RS, sizeof(P2)) = {(DWORD)s, v};
        if (g_defer) {
            if ((unsigned)s < 256) { g_rsVal[s] = v; g_rsGen[s] = g_pendGen; }
            return D3D_OK;
        }
    }
    return o_SetRenderState(d, s, v);
}
HOOK(HRESULT, GetRenderState, D3DRENDERSTATETYPE s, DWORD* v) {
    if (g_defer && d == g_dev && v && (unsigned)s < 256 && g_rsGen[s] == g_pendGen && GetCurrentThreadId() == g_tid) {
        *v = g_rsVal[s];
        St.rgLocal++;
        return D3D_OK;
    }
    return o_GetRenderState(d, s, v);
}
HOOK(HRESULT, GetViewport, D3DVIEWPORT9* v) {
    if (g_defer && d == g_dev && v && g_vpGen == g_pendGen && GetCurrentThreadId() == g_tid) {
        *v = g_vpVal;
        St.rgLocal++;
        return D3D_OK;
    }
    return o_GetViewport(d, v);
}
HOOK(HRESULT, GetTransform, D3DTRANSFORMSTATETYPE s, D3DMATRIX* m) {
    if (g_defer && d == g_dev && m && (unsigned)s < 512 && g_xfGen[s] == g_pendGen && GetCurrentThreadId() == g_tid) {
        if (!g_xfMul[s]) {
            *m = g_xfVal[s];
            St.rgLocal++;
            return D3D_OK;
        }
        FlushDeferred(45);
    }
    return o_GetTransform(d, s, m);
}
static inline bool LocalRead(Dev* d) { return g_defer && d == g_dev && GetCurrentThreadId() == g_tid; }
HOOK(HRESULT, GetTexture, DWORD st, IDirect3DBaseTexture9** t) {
    int k = SamplerSlot(st);
    if (LocalRead(d) && t && k >= 0 && g_texGen[k] == g_pendGen) {
        *t = (IDirect3DBaseTexture9*)g_texVal[k];
        if (*t) (*t)->AddRef();
        St.rgLocal++;
        return D3D_OK;
    }
    return o_GetTexture(d, st, t);
}
HOOK(HRESULT, GetRenderTarget, DWORD i, IDirect3DSurface9** s) {
    if (LocalRead(d) && s && i < 4 && g_rtGen[i] == g_pendGen) {
        *s = (IDirect3DSurface9*)g_rtVal[i];
        St.rgLocal++;
        if (!*s) return D3DERR_NOTFOUND;
        (*s)->AddRef();
        return D3D_OK;
    }
    return o_GetRenderTarget(d, i, s);
}
HOOK(HRESULT, GetDepthStencilSurface, IDirect3DSurface9** s) {
    if (LocalRead(d) && s && g_dsGen == g_pendGen) {
        *s = (IDirect3DSurface9*)g_dsVal;
        St.rgLocal++;
        if (!*s) return D3DERR_NOTFOUND;
        (*s)->AddRef();
        return D3D_OK;
    }
    return o_GetDepthStencilSurface(d, s);
}
HOOK(HRESULT, GetSamplerState, DWORD s, D3DSAMPLERSTATETYPE ty, DWORD* v) {
    int k = SamplerSlot(s);
    if (LocalRead(d) && v && k >= 0 && (unsigned)ty < 14 && g_ssGen[k][ty] == g_pendGen) {
        *v = g_ssVal[k][ty];
        St.rgLocal++;
        return D3D_OK;
    }
    return o_GetSamplerState(d, s, ty, v);
}
HOOK(HRESULT, CreateStateBlock, D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** sb) {
    FlushDeferred(kWhySB);
    if (d == g_dev && !g_inInterp) St.sbCreate++;
    HRESULT hr = o_CreateStateBlock(d, t, sb);
    if (SUCCEEDED(hr) && sb && *sb) HookSB(*sb);
    return hr;
}
// The D3D9 runtime implements state-block recording by rewriting the device's per-instance vtable, and
// EndStateBlock copies its pristine vtable back, wiping our hooks. So: hook EndStateBlock inside the recording
// vtable, and re-apply all device hooks right after the real EndStateBlock.
static void* g_endSBTarget;
HOOKDECL(HRESULT, EndStateBlock, IDirect3DStateBlock9** sb);
HOOK(HRESULT, BeginStateBlock) {
    FlushDeferred(kWhySB);
    if (d == g_dev) { g_sbRecording = true; St.sbBegin++; }
    HRESULT hr = o_BeginStateBlock(d);
    void** vt = *(void***)d;
    if (vt[61] != (void*)&hk_EndStateBlock) {
        g_endSBTarget = vt[61];
        DWORD p;
        VirtualProtect(&vt[61], sizeof(void*), PAGE_EXECUTE_READWRITE, &p);
        vt[61] = (void*)&hk_EndStateBlock;
        VirtualProtect(&vt[61], sizeof(void*), p, &p);
    }
    return hr;
}
HOOKDEF(HRESULT, EndStateBlock, IDirect3DStateBlock9** sb) {
    auto fn = g_endSBTarget ? (EndStateBlock_t)g_endSBTarget : o_EndStateBlock;
    g_endSBTarget = nullptr;
    HRESULT hr = fn(d, sb);
    if (d == g_dev) {
        g_sbRecording = false;
        HookDeviceVT(d);
    }
    if (SUCCEEDED(hr) && sb && *sb) HookSB(*sb);
    return hr;
}
HOOK(HRESULT, SetTexture, DWORD st, IDirect3DBaseTexture9* t) {
    if (d == g_dev && st == 0 && !g_inInterp) S.tex0 = t;
    if (Rec(d)) {
        *(PPtr*)Push(OP_TEX, sizeof(PPtr)) = {st, t};
        g_fr[g_cur].texCmds.push_back(g_lastOff);
        Ref(t);
        if (g_defer) {
            int k = SamplerSlot(st);
            if (k >= 0) { g_texVal[k] = t; g_texGen[k] = g_pendGen; }
            if (t && (g_pendTex.empty() || g_pendTex.back() != (void*)t)) g_pendTex.push_back(t);
            return D3D_OK;
        }
    }
    return o_SetTexture(d, st, t);
}
HOOK(HRESULT, SetTextureStageState, DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v) {
    if (Rec(d)) { *(P3*)Push(OP_TSS, sizeof(P3)) = {st, (DWORD)t, v}; if (g_defer) return D3D_OK; }
    return o_SetTextureStageState(d, st, t, v);
}
HOOK(HRESULT, SetSamplerState, DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) {
    if (Rec(d)) {
        *(P3*)Push(OP_SS, sizeof(P3)) = {s, (DWORD)t, v};
        if (g_defer) {
            int k = SamplerSlot(s);
            if (k >= 0 && (unsigned)t < 14) { g_ssVal[k][t] = v; g_ssGen[k][t] = g_pendGen; }
            return D3D_OK;
        }
    }
    return o_SetSamplerState(d, s, t, v);
}
HOOK(HRESULT, SetScissorRect, const RECT* r) {
    if (Rec(d) && r) { *(RECT*)Push(OP_SCISSOR, sizeof(RECT)) = *r; if (g_defer) return D3D_OK; }
    return o_SetScissorRect(d, r);
}
HOOK(HRESULT, SetSoftwareVertexProcessing, BOOL b) {
    if (Rec(d)) { *(P2*)Push(OP_SWVP, sizeof(P2)) = {(DWORD)b, 0}; if (g_defer) return D3D_OK; }
    return o_SetSoftwareVertexProcessing(d, b);
}
HOOK(HRESULT, SetNPatchMode, float s) {
    if (Rec(d)) { P2 v{0, 0}; memcpy(&v.a, &s, 4); *(P2*)Push(OP_NPATCH, sizeof(P2)) = v; if (g_defer) return D3D_OK; }
    return o_SetNPatchMode(d, s);
}
// replay tracer (debug): 2 = snapshotting every draw of the real frame being recorded; side 1/2 = replays
static int g_trState, g_trSide = -1;
static int g_variant;  // live A/B diagnostic variant (see ApplyVariant)
// self-check ablation (debug): 1 = no camera-estimated packets, 2 = no screen-group fixes, 4 = no camera check
static int g_ablate;
static void TraceAfterDraw(int side);
static void TraceReleaseAll();
HOOK(HRESULT, DrawPrimitive, D3DPRIMITIVETYPE t, UINT s, UINT n) {
    bool rec = Rec(d);
    if (rec) {
        *(PDP*)Push(OP_DP, sizeof(PDP)) = {t, s, n};
        uint64_t g = Fnv(Fnv(Fnv(Fnv(Fnv(1469598103934665603ull, (uintptr_t)S.vb0), S.off0), t), s), n);
        AddDraw(Fnv(GeoKey(1), g), g);
        if (g_defer) return D3D_OK;
    }
    HRESULT hr = o_DrawPrimitive(d, t, s, n);
    if (rec && g_trState == 2) TraceAfterDraw(0);
    return hr;
}
HOOK(HRESULT, DrawIndexedPrimitive, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT si, UINT pc) {
    g_lastApi = "DrawIndexedPrimitive";
    bool rec = Rec(d);
    if (rec && C.input_latch) InputSample();  // (the game's own drawing: input_latch also samples here)
    if (rec) {
        *(PDIP*)Push(OP_DIP, sizeof(PDIP)) = {t, b, mi, nv, si, pc};
        uint64_t g = 1469598103934665603ull;
        g = Fnv(g, (uintptr_t)S.vb0); g = Fnv(g, S.off0); g = Fnv(g, (uintptr_t)S.ib); g = Fnv(g, (uint32_t)b);
        g = Fnv(g, si); g = Fnv(g, pc); g = Fnv(g, t);
        AddDraw(Fnv(GeoKey(2), g), g);
        if (g_defer) return D3D_OK;
    }
    HRESULT hr = o_DrawIndexedPrimitive(d, t, b, mi, nv, si, pc);
    if (rec && g_trState == 2) TraceAfterDraw(0);
    return hr;
}
HOOK(HRESULT, DrawPrimitiveUP, D3DPRIMITIVETYPE t, UINT pc, const void* data, UINT stride) {
    g_lastApi = "DrawPrimitiveUP";
    bool rec = Rec(d) && data;
    if (rec && C.input_latch) InputSample();
    if (rec) {
        UINT nv = VertsFor(t, pc);
        UINT vb = nv * stride;
        const Layout& L = CurLayout();
        auto* p = (PDPUP*)Push(OP_DPUP, sizeof(PDPUP) + vb);
        *p = {t, pc, stride, vb, L.posOff, L.posN, (uint8_t)L.rhw};
        memcpy(p + 1, data, vb);
        uint64_t k = BaseKey(3);
        k = Fnv(k, t); k = Fnv(k, pc); k = Fnv(k, stride);
        k = Fnv(k, ContentHash(L, (const uint8_t*)data, nv, stride));
        AddDraw(k, k);
        // a screen quad sampling a glyph atlas: which version of it (TextKeys reads the glyph's pixels)
        if (C.text_ckeys && L.rhw && L.uvOff >= 0 && S.tex0 && (nv == 4 || nv == 6) && stride >= (UINT)L.uvOff + 8 &&
            ((IDirect3DBaseTexture9*)S.tex0)->GetType() == D3DRTYPE_TEXTURE) {
            if (auto snap = AtlasSnapshot((IDirect3DTexture9*)S.tex0)) {
                Frame& f = g_fr[g_cur];
                if (f.snaps.empty() || f.snaps.back() != snap) f.snaps.push_back(snap);
                if (f.snaps.size() <= 0xFFFF) f.glyphs.push_back({(uint32_t)f.draws.size() - 1, (uint16_t)(f.snaps.size() - 1), L.uvOff, BaseKey(3)});
            }
        }
    }
    if (d == g_dev && !g_inInterp) S.vb0 = nullptr;
    if (rec && g_defer) return D3D_OK;
    HRESULT hr = o_DrawPrimitiveUP(d, t, pc, data, stride);
    if (rec && g_trState == 2) TraceAfterDraw(0);
    return hr;
}
HOOK(HRESULT, DrawIndexedPrimitiveUP, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void* idx, D3DFORMAT fmt, const void* vtx, UINT stride) {
    bool rec = Rec(d) && idx && vtx;
    if (rec) {
        UINT ib = VertsFor(t, pc) * (fmt == D3DFMT_INDEX32 ? 4 : 2);
        UINT ibA = (ib + 3) & ~3u;
        UINT vb = (mi + nv) * stride;
        const Layout& L = CurLayout();
        auto* p = (PDIPUP*)Push(OP_DIPUP, sizeof(PDIPUP) + ibA + vb);
        *p = {t, mi, nv, pc, fmt, stride, ib, vb, L.posOff, L.posN, (uint8_t)L.rhw};
        memcpy(p + 1, idx, ib);
        memcpy((uint8_t*)(p + 1) + ibA, vtx, vb);
        uint64_t k = BaseKey(4);
        k = Fnv(k, t); k = Fnv(k, pc); k = Fnv(k, nv); k = Fnv(k, stride);
        k = Fnv(k, ContentHash(L, (const uint8_t*)vtx + mi * stride, nv, stride));
        AddDraw(k, k);
    }
    if (d == g_dev && !g_inInterp) { S.vb0 = nullptr; S.ib = nullptr; }
    if (rec && g_defer) return D3D_OK;
    HRESULT hr = o_DrawIndexedPrimitiveUP(d, t, mi, nv, pc, idx, fmt, vtx, stride);
    if (rec && g_trState == 2) TraceAfterDraw(0);
    return hr;
}
HOOK(HRESULT, SetVertexDeclaration, IDirect3DVertexDeclaration9* v) {
    if (d == g_dev && !g_inInterp) { S.decl = v; S.useFvf = false; }
    if (Rec(d)) { *(PPtr*)Push(OP_DECL, sizeof(PPtr)) = {0, v}; Ref(v); if (g_defer) return D3D_OK; }
    return o_SetVertexDeclaration(d, v);
}
HOOK(HRESULT, SetFVF, DWORD f) {
    if (d == g_dev && !g_inInterp) { S.fvf = f; S.useFvf = true; }
    if (Rec(d)) { *(P2*)Push(OP_FVF, sizeof(P2)) = {f, 0}; if (g_defer) return D3D_OK; }
    return o_SetFVF(d, f);
}
HOOK(HRESULT, SetVertexShader, IDirect3DVertexShader9* v) {
    if (d == g_dev && !g_inInterp) S.vs = v;
    if (Rec(d)) { *(PPtr*)Push(OP_VS, sizeof(PPtr)) = {0, v}; Ref(v); if (g_defer) return D3D_OK; }
    return o_SetVertexShader(d, v);
}
static std::unordered_map<void*, VSInfo> g_vsInfo;
static std::unordered_map<void*, VsOutInfo> g_vsOut;     // shadow map reuse: where shader outputs and lookups come from
static std::unordered_map<void*, PsLookInfo> g_psLook;
// While worker threads run, shader lookups must not change the tables or call into the runtime: they only read,
// and a shader not in the table is noted (g_infoMiss) so the caller can redo that work on the render thread.
static const int kMaxThreads = 8;  // pairing threads at most (render thread included)
static volatile bool g_infoRO;
static volatile LONG g_infoMiss;
static const VSInfo g_noVS;
HOOK(HRESULT, CreateVertexShader, const DWORD* code, IDirect3DVertexShader9** out) {
    PairJoin();
    HRESULT hr = o_CreateVertexShader(d, code, out);
    if (SUCCEEDED(hr) && out && *out) {
        VSInfo vi;
        AnalyzeVS(code, vi);
        g_vsInfo[*out] = vi;
        AnalyzeVsOut(code, g_vsOut[*out]);
    }
    return hr;
}
static const VSInfo* GetVSInfo(void* vs) {
    if (!vs) return nullptr;
    auto it = g_vsInfo.find(vs);
    if (it != g_vsInfo.end()) return &it->second;
#ifdef PAIRBENCH
    return &g_noVS;  // (not in the capture: the default analysis, "everything")
#endif
    if (g_infoRO) { InterlockedExchange(&g_infoMiss, 1); return &g_noVS; }
    if (GetCurrentThreadId() == g_tid) PairJoin();  // (the table is about to change: the background matching reads it)
    // created before our hook: analyse its bytecode now
    UINT sz = 0;
    VSInfo vi;
    auto* sh = (IDirect3DVertexShader9*)vs;
    if (SUCCEEDED(sh->GetFunction(nullptr, &sz)) && sz) {
        std::vector<DWORD> code(sz / 4 + 1);
        if (SUCCEEDED(sh->GetFunction(code.data(), &sz))) AnalyzeVS(code.data(), vi);
    }
    return &(g_vsInfo[vs] = vi);
}

// Pixel shaders: which constants they read, analysed from the bytecode on first use. A shader pointer can be
// reused after a release, so the bytecode size is kept and checked.
struct PSEntry { UINT size; PSInfo info; };
static std::unordered_map<void*, PSEntry> g_psInfo;
// Shaders created through our hook are analysed right there (CreatePixelShader, also when an address is reused), so
// a lookup is just the table; only shaders created before the hook still ask the runtime for their bytecode (that
// GetFunction call on every lookup cost real time in busy scenes).
static std::unordered_set<void*> g_psHooked;
static const PSInfo* GetPSInfo(void* ps) {
    if (!ps) return nullptr;
#ifdef PAIRBENCH
    auto pit = g_psInfo.find(ps);
    return pit != g_psInfo.end() ? &pit->second.info : nullptr;
#endif
    if (g_psHooked.count(ps)) { auto hit = g_psInfo.find(ps); if (hit != g_psInfo.end()) return &hit->second.info; }
    if (g_infoRO) { InterlockedExchange(&g_infoMiss, 1); return nullptr; }
    if (GetCurrentThreadId() == g_tid) PairJoin();
    auto* sh = (IDirect3DPixelShader9*)ps;
    UINT sz = 0;
    if (FAILED(sh->GetFunction(nullptr, &sz)) || !sz) return nullptr;
    auto it = g_psInfo.find(ps);
    if (it != g_psInfo.end() && it->second.size == sz) return &it->second.info;
    std::vector<DWORD> code(sz / 4 + 1);
    PSEntry e{sz, PSInfo()};
    if (SUCCEEDED(sh->GetFunction(code.data(), &sz))) AnalyzePS(code.data(), e.info);
    return &(g_psInfo[ps] = e).info;
}

static UINT ShaderBytes(const DWORD* code) {  // up to and including the end token
    UINT n = 1;
    while (n < (1u << 20) && code[n] != 0x0000FFFF) {
        DWORD tok = code[n];
        if ((tok & 0xFFFF) == 0xFFFE) n += 1 + ((tok >> 16) & 0x7FFF);  // comment block
        else n++;
    }
    return (n + 1) * 4;
}
// Which samplers a pixel shader reads (bit s = sampler s), for the regrouping: from its dcl instructions (ps_2_0
// and up); all of them for ps_1_x, and all 8 texture stages for the fixed-function pipeline.
static std::unordered_map<void*, uint16_t> g_psSamp;
static uint16_t PsSamplerMask(const DWORD* code) {
    if (!code || (code[0] & 0xFFFF0000) != 0xFFFF0000 || ((code[0] >> 8) & 0xFF) < 2) return 0xFFFF;
    uint16_t m = 0;
    const DWORD* p = code + 1;
    for (int guard = 0; guard < 100000; guard++) {
        DWORD tok = *p;
        if (tok == 0x0000FFFF) return m;
        DWORD op = tok & 0xFFFF;
        if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }  // comment
        int len = (int)((tok >> 24) & 0x0F);
        if (op == 31 && len >= 2 && VsRegType(p[2]) == 10) m |= (uint16_t)(1u << (p[2] & 0xF));  // dcl_* s#
        p += 1 + len;
    }
    return 0xFFFF;
}
template <class T, class Sh>
static const T* ShaderLookup(std::unordered_map<void*, T>& m, void* sh, void (*an)(const DWORD*, T&)) {
    if (!sh) return nullptr;
    auto it = m.find(sh);
    if (it != m.end()) return &it->second;
    T v;
    UINT sz = 0;
    if (SUCCEEDED(((Sh*)sh)->GetFunction(nullptr, &sz)) && sz) {
        std::vector<DWORD> code(sz / 4 + 1);
        if (SUCCEEDED(((Sh*)sh)->GetFunction(code.data(), &sz))) an(code.data(), v);
    }
    return &(m[sh] = v);
}
static uint16_t PsSamplers(void* ps) {
    if (!ps) return 0xFF;
    auto it = g_psSamp.find(ps);
    if (it != g_psSamp.end()) return it->second;
    uint16_t m = 0xFFFF;
    UINT sz = 0;
    auto* sh = (IDirect3DPixelShader9*)ps;
    if (SUCCEEDED(sh->GetFunction(nullptr, &sz)) && sz) {
        std::vector<DWORD> code(sz / 4 + 1);
        if (SUCCEEDED(sh->GetFunction(code.data(), &sz))) m = PsSamplerMask(code.data());
    }
    return g_psSamp[ps] = m;
}
HOOK(HRESULT, CreatePixelShader, const DWORD* code, IDirect3DPixelShader9** out) {
    PairJoin();
    HRESULT hr = o_CreatePixelShader(d, code, out);
    if (SUCCEEDED(hr) && out && *out && code) {
        PSEntry e{ShaderBytes(code), PSInfo()};
        AnalyzePS(code, e.info);
        g_psInfo[*out] = e;
        g_psHooked.insert(*out);
        g_psSamp[*out] = PsSamplerMask(code);
        AnalyzePsLook(code, g_psLook[*out]);
    }
    return hr;
}
HOOK(HRESULT, SetPixelShader, IDirect3DPixelShader9* v) {
    if (d == g_dev && !g_inInterp) S.ps = v;
    if (Rec(d)) { *(PPtr*)Push(OP_PS, sizeof(PPtr)) = {0, v}; Ref(v); if (g_defer) return D3D_OK; }
    return o_SetPixelShader(d, v);
}
#define CONST_HOOK(Name, OP, T, unit)                                                     \
    HOOK(HRESULT, Name, UINT s, const T* v, UINT n) {                                     \
        if (Rec(d) && v) {                                                                \
            auto* p = (PConst*)Push(OP, sizeof(PConst) + n * unit);                       \
            p->start = s;                                                                 \
            p->count = n;                                                                 \
            p->ord = 0;                                                                   \
            memcpy(p + 1, v, n * unit);                                                   \
            if (OP == OP_VSCF) {                                                          \
                Frame& f = g_fr[g_cur];                                                   \
                p->ord = f.nvscf++;                                                       \
                f.lastSig = g_lastOff;                                                    \
                if (f.nSegVscf == 16) {                                                   \
                    memmove(f.segVscf, f.segVscf + 1, 15 * sizeof(uint32_t));             \
                    f.nSegVscf = 15;                                                      \
                }                                                                         \
                f.segVscf[f.nSegVscf++] = g_lastOff;                                      \
            }                                                                             \
            if (g_defer) return D3D_OK;                                                   \
        }                                                                                 \
        return o_##Name(d, s, v, n);                                                      \
    }
CONST_HOOK(SetVertexShaderConstantF, OP_VSCF, float, 16)
CONST_HOOK(SetVertexShaderConstantI, OP_VSCI, int, 16)
CONST_HOOK(SetVertexShaderConstantB, OP_VSCB, BOOL, 4)
CONST_HOOK(SetPixelShaderConstantF, OP_PSCF, float, 16)
CONST_HOOK(SetPixelShaderConstantI, OP_PSCI, int, 16)
CONST_HOOK(SetPixelShaderConstantB, OP_PSCB, BOOL, 4)
HOOK(HRESULT, SetStreamSource, UINT st, IDirect3DVertexBuffer9* vb, UINT off, UINT stride) {
    if (d == g_dev && st == 0 && !g_inInterp) { S.vb0 = vb; S.off0 = off; S.stride0 = stride; }
    if (Rec(d)) { *(PSsrc*)Push(OP_SSRC, sizeof(PSsrc)) = {st, vb, off, stride}; Ref(vb); if (g_defer) return D3D_OK; }
    return o_SetStreamSource(d, st, vb, off, stride);
}
HOOK(HRESULT, SetStreamSourceFreq, UINT st, UINT v) {
    if (Rec(d)) { *(P2*)Push(OP_SSFREQ, sizeof(P2)) = {st, v}; if (g_defer) return D3D_OK; }
    return o_SetStreamSourceFreq(d, st, v);
}
HOOK(HRESULT, SetIndices, IDirect3DIndexBuffer9* ib) {
    if (d == g_dev && !g_inInterp) S.ib = ib;
    if (Rec(d)) { *(PPtr*)Push(OP_IDX, sizeof(PPtr)) = {0, ib}; Ref(ib); if (g_defer) return D3D_OK; }
    return o_SetIndices(d, ib);
}
HOOK(HRESULT, SetRenderTarget, DWORD i, IDirect3DSurface9* s) {
    g_lastApi = "SetRenderTarget";
    if (d == g_dev && i == 0 && !g_inInterp) S.rt0 = s;
    if (Rec(d)) {
        *(PPtr*)Push(OP_RT, sizeof(PPtr)) = {i, s};
        Ref(s);
        if (g_defer) {
            D3DSURFACE_DESC sd;  // (render target 0 resets the viewport to all of it)
            if (i == 0 && s && SUCCEEDED(s->GetDesc(&sd))) { g_vpVal = {0, 0, sd.Width, sd.Height, 0.0f, 1.0f}; g_vpGen = g_pendGen; }
            if (i < 4 && (i > 0 || s)) { g_rtVal[i] = s; g_rtGen[i] = g_pendGen; }
            return D3D_OK;
        }
    }
    return o_SetRenderTarget(d, i, s);
}
HOOK(HRESULT, SetDepthStencilSurface, IDirect3DSurface9* s) {
    if (Rec(d)) {
        *(PPtr*)Push(OP_DS, sizeof(PPtr)) = {0, s};
        Ref(s);
        if (g_defer) { g_dsVal = s; g_dsGen = g_pendGen; return D3D_OK; }
    }
    return o_SetDepthStencilSurface(d, s);
}
HOOK(HRESULT, StretchRect, IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* ds, const RECT* dr, D3DTEXTUREFILTERTYPE f) {
    FlushDeferred(kWhyCopy);  // (copies run as called: they end a window anyway)
    if (Rec(d)) {
        auto* p = (PStretch*)Push(OP_STRETCH, sizeof(PStretch));
        *p = {s, ds, {}, {}, sr != nullptr, dr != nullptr, f};
        if (sr) p->sr = *sr;
        if (dr) p->dr = *dr;
        Ref(s);
        Ref(ds);
        LiveAfterFlush();
    }
    return o_StretchRect(d, s, sr, ds, dr, f);
}
HOOK(HRESULT, ColorFill, IDirect3DSurface9* s, const RECT* r, D3DCOLOR c) {
    FlushDeferred(kWhyCopy);
    if (Rec(d)) {
        auto* p = (PFill*)Push(OP_COLORFILL, sizeof(PFill));
        *p = {s, {}, r != nullptr, c};
        if (r) p->r = *r;
        Ref(s);
        LiveAfterFlush();
    }
    return o_ColorFill(d, s, r, c);
}
HOOK(HRESULT, UpdateSurface, IDirect3DSurface9* a, const RECT* b, IDirect3DSurface9* c, const POINT* e) {
    FlushDeferred(kWhyUpdate);
    if (d == g_dev && !g_inInterp) St.updates++;
    return o_UpdateSurface(d, a, b, c, e);
}
HOOK(HRESULT, UpdateTexture, IDirect3DBaseTexture9* a, IDirect3DBaseTexture9* b) {
    FlushDeferred(kWhyUpdate);
    if (d == g_dev && !g_inInterp) St.updates++;
    return o_UpdateTexture(d, a, b);
}
HOOK(HRESULT, GetRenderTargetData, IDirect3DSurface9* a, IDirect3DSurface9* b) {
    FlushDeferred(kWhyUpdate);
    if (d == g_dev && !g_inInterp) St.rtData++;
    return o_GetRenderTargetData(d, a, b);
}

// state block methods (the game may use them through D3DX effects)
typedef HRESULT(WINAPI* SB_t)(IDirect3DStateBlock9*);
static SB_t o_SBCapture, o_SBApply;
static HRESULT WINAPI hk_SBCapture(IDirect3DStateBlock9* sb) {
    if (!g_inInterp && GetCurrentThreadId() == g_tid) {
        FlushDeferred(kWhySB);
        St.sbCapture++;
        if (g_dev && Rec(g_dev)) { *(PPtr*)Push(OP_SBCAPTURE, sizeof(PPtr)) = {0, sb}; Ref(sb); LiveAfterFlush(); }
    }
    return o_SBCapture(sb);
}
static HRESULT WINAPI hk_SBApply(IDirect3DStateBlock9* sb) {
    if (!g_inInterp && GetCurrentThreadId() == g_tid) {
        FlushDeferred(kWhySB);
        St.sbApply++;
        if (g_dev && Rec(g_dev)) { *(PPtr*)Push(OP_SBAPPLY, sizeof(PPtr)) = {0, sb}; Ref(sb); LiveAfterFlush(); }
    }
    return o_SBApply(sb);
}

// ------------------------------------------------------------------------------------------------ pairing
// mi: index into g_pairMask for constant packets; synth: when not NONE, the "previous" values come from
// g_synth (estimated) instead of the previous frame's command buffer
struct Pair {
    uint32_t cur, prev, mi, synth;
    int16_t boneBase = -1;  // VSCF: registers from here on are 3-register bone matrices (slerped, not lerped)
    uint32_t upSynth = NONE;  // UP draw without a counterpart: offset of a synthetic previous command in g_upSynth
    uint64_t camKey = 0;      // a positional 4x4: its camera context (in-between frames follow the camera's path)
};
static std::vector<Pair> g_pairs;
static uint64_t g_pairsGen = 1;  // a new matching (BuildPairsImpl)
static std::vector<uint8_t> g_upSynth;  // synthetic previous UP commands (screen quads moved by their neighbours)
struct ScreenSynth { uint32_t draw; float dx, dy; };
static std::vector<ScreenSynth> g_screenSynth;  // unpaired screen quads and the displacement of their neighbours
static std::vector<float> g_synth;
static uint32_t g_tkRejected, g_tkSynth;  // per-tick counters for the rolling recorder's statistics
struct TickStat { uint32_t tick, draws, unmatched, rejected, synth, pairs; bool cut; };
static std::vector<TickStat> g_tickStats;
static size_t g_tickStatPos;
static std::vector<Mask256> g_pairMask;  // per constant pair: which registers (relative to start) get lerped
static std::unordered_map<uint64_t, std::vector<int>> g_prevIdx;
static std::unordered_map<uint64_t, int> g_occ;
static std::vector<char> g_prevUsed;
static std::vector<Mask256> g_pmask;  // per VSCF packet of frame N: registers (absolute) that feed a vertex position

// The runs of consecutive set bits in one 64-register word of a mask: f(first register, end) for each, in order.
template <class F> static inline void ForRuns(uint64_t bits, int w, F f) {
    while (bits) {
        int s = __builtin_ctzll(bits);
        uint64_t x = ~(bits >> s);
        int len = x ? __builtin_ctzll(x) : 64;
        bits = s + len >= 64 ? 0 : bits & ~(((1ull << len) - 1) << s);  // (s is the lowest bit set: the run ends the word, or is cleared)
        f(w * 64 + s, w * 64 + s + len);
    }
}

// Liveness pass: a register of a VSCF packet is positional if a later draw's vertex shader reads it for its
// position output before the register is overwritten.
PROF_NOINLINE static void ComputePosMasks(const Frame& N) {
    g_pmask.assign(N.nvscf, Mask256());
    int32_t owner[256];
    for (auto& o : owner) o = -1;
    struct Pend { UINT start, count, ord; };
    Pend pend[64];
    int npend = 0;
    bool full = true;
    void* vs = N.startVS;
    const VSInfo* vi = GetVSInfo(vs);
    const uint8_t* b = N.buf.data();
    for (size_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(b + off);
        switch (c->op) {
        case OP_VS: {
            void* nv = ((const PPtr*)(c + 1))->p;
            if (nv != vs) { vs = nv; vi = GetVSInfo(vs); full = true; }
            break;
        }
        case OP_VSCF: {
            auto* q = (const PConst*)(c + 1);
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) owner[r] = (int32_t)q->ord;
            if (npend < 64) pend[npend++] = {q->start, q->count, q->ord};
            else full = true;
            break;
        }
        case OP_SBAPPLY: full = true; break;
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP:
            if (vs) {
                if (full) {
                    // (registers in a row with the same upload: set together)
                    for (int w = 0; w < 4; w++)
                        ForRuns((!vi || vi->all) ? ~0ull : vi->pos.b[w], w, [&](int r, int e) {
                            while (r < e) {
                                int32_t ow = owner[r];
                                int r0 = r++;
                                while (r < e && owner[r] == ow) r++;
                                if (ow < 0) continue;
                                int n = r - r0;
                                g_pmask[ow].b[r0 >> 6] |= (n >= 64 ? ~0ull : (1ull << n) - 1) << (r0 & 63);
                            }
                        });
                } else {
                    for (int i = 0; i < npend; i++)
                        for (UINT r = pend[i].start; r < pend[i].start + pend[i].count && r < 256; r++)
                            if (owner[r] == (int32_t)pend[i].ord && (!vi || vi->all || vi->pos.test(r))) g_pmask[pend[i].ord].set(r);
                }
            }
            full = false;
            npend = 0;
            break;
        }
        off += c->size;
    }
}

// For every draw: the VSCF commands (offsets) whose registers its vertex shader reads for the position,
// i.e. the uploads in effect at that draw, wherever in the frame they happened. Pairing positional constants
// through these (draw N.i reads packet A, its matched draw P.j reads packet B => A pairs with B) is robust to
// the game uploading a shared matrix before a different draw than last frame (culling changes which mesh comes
// first), which left whole passes on the un-interpolated camera and tore seams open.
struct Owners {
    std::vector<uint32_t> flat;
    std::vector<uint32_t> begin;
    std::vector<uint8_t> count;
    // (CollectOwners: per entry of flat, the upload's slot and ordinal - what FirstMat4 and co. look at, without reading
    // the command)
    struct E { uint32_t start, count, ord; };
    std::vector<E> info;
    // (CollectLive: every constant upload of the frame, in order, with the draws it is in effect for - [first, end):
    // from the next draw until no register holds it any more - and its stage, 0 vertex / 1 pixel shader)
    struct Life { uint32_t off, first, end; uint8_t stage; };
    std::vector<Life> life;
    std::vector<uint16_t> refs;  // (CollectLive's scratch: no thread_local - its destructors can hang a DLL's process exit)
};
static Owners g_ownN, g_ownP;
static uint32_t g_ownNSerial;  // frame g_ownN / g_liveN were collected for
PROF_NOINLINE static void CollectOwners(const Frame& F, Owners& o) {
    o.flat.clear();
    o.info.clear();
    o.begin.assign(F.draws.size(), 0);
    o.count.assign(F.draws.size(), 0);
    uint32_t owner[256];
    for (auto& w : owner) w = NONE;
    void* vs = F.startVS;
    const VSInfo* vi = GetVSInfo(vs);
    bool dirty = true;
    uint32_t lastBegin = 0;
    uint8_t lastCount = 0;
    size_t di = 0;
    const uint8_t* b = F.buf.data();
    for (size_t off = 0; off < F.buf.size();) {
        const Cmd* c = (const Cmd*)(b + off);
        switch (c->op) {
        case OP_VS: {
            void* nv = ((const PPtr*)(c + 1))->p;
            if (nv != vs) { vs = nv; vi = GetVSInfo(vs); dirty = true; }
            break;
        }
        case OP_VSCF: {
            auto* q = (const PConst*)(c + 1);
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) owner[r] = (uint32_t)off;
            dirty = true;
            break;
        }
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP:
            if (di < F.draws.size()) {
                if (dirty) {
                    lastBegin = (uint32_t)o.flat.size();
                    lastCount = 0;
                    if (vs) {
                        for (int w = 0; w < 4; w++)
                            ForRuns((!vi || vi->all) ? ~0ull : vi->pos.b[w], w, [&](int r, int e) {
                                while (r < e) {
                                    uint32_t ow = owner[r++];
                                    while (r < e && owner[r] == ow) r++;  // (a packet's registers come in a row)
                                    if (ow == NONE) continue;
                                    if (o.flat.size() > lastBegin && o.flat.back() == ow) continue;
                                    bool have = false;
                                    for (uint32_t k = lastBegin; k < o.flat.size(); k++) if (o.flat[k] == ow) { have = true; break; }
                                    if (!have && lastCount < 16) {
                                        auto* q = (const PConst*)((const Cmd*)(b + ow) + 1);
                                        o.flat.push_back(ow);
                                        o.info.push_back({q->start, q->count, q->ord});
                                        lastCount++;
                                    }
                                }
                            });
                    }
                    dirty = false;
                }
                o.begin[di] = lastBegin;
                o.count[di] = lastCount;
                di++;
            }
            break;
        }
        off += c->size;
    }
}

// For every draw: the other constant uploads it reads - vertex-shader registers that don't feed the position, and
// pixel-shader registers - i.e. the uploads in effect at that draw. Lighting, fog and shadow-lookup values are
// often uploaded once and read by hundreds of later draws, or uploaded for objects the game then skips drawing.
// Pairing them through the draws that read them (as positional data already is) instead of by command order keeps
// a shadow lookup consistent with the interpolated shadow map, and a character's light with its own.
static const PSInfo* GetPSInfo(void* ps);
static Owners g_liveN, g_liveP;
PROF_NOINLINE static void CollectLive(const Frame& F, Owners& o) {
    o.flat.clear();
    o.life.clear();
    std::vector<uint16_t>& refs = o.refs;  // per upload: the registers holding it
    refs.clear();
    uint32_t held[2][256];  // per register: the upload (index into life) holding it
    for (auto& s : held) for (auto& x : s) x = NONE;
    o.begin.assign(F.draws.size(), 0);
    o.count.assign(F.draws.size(), 0);
    uint32_t vown[256], pown[256];
    for (int r = 0; r < 256; r++) vown[r] = pown[r] = NONE;
    void* vs = F.startVS;
    void* ps = F.startPS;
    const VSInfo* vi = GetVSInfo(vs);
    const PSInfo* pi = GetPSInfo(ps);
    bool dirty = true;
    uint32_t lastBegin = 0;
    uint8_t lastCount = 0;
    size_t di = 0;
    const uint8_t* b = F.buf.data();
    auto add = [&](uint32_t ow) {
        if (ow == NONE || lastCount >= 48) return;
        if (o.flat.size() > lastBegin && o.flat.back() == ow) return;  // (a packet's registers come in a row)
        for (uint32_t k = lastBegin; k < o.flat.size(); k++) if (o.flat[k] == ow) return;
        o.flat.push_back(ow);
        lastCount++;
    };
    for (size_t off = 0; off < F.buf.size();) {
        const Cmd* c = (const Cmd*)(b + off);
        switch (c->op) {
        case OP_VS: {
            void* nv = ((const PPtr*)(c + 1))->p;
            if (nv != vs) { vs = nv; vi = GetVSInfo(vs); dirty = true; }
            break;
        }
        case OP_PS: {
            void* np = ((const PPtr*)(c + 1))->p;
            if (np != ps) { ps = np; pi = GetPSInfo(ps); dirty = true; }
            break;
        }
        case OP_VSCF:
        case OP_PSCF: {
            auto* q = (const PConst*)(c + 1);
            int k = c->op == OP_PSCF;
            uint32_t* own = k ? pown : vown;
            uint32_t u = (uint32_t)o.life.size();
            o.life.push_back({(uint32_t)off, (uint32_t)di, NONE, (uint8_t)k});
            refs.push_back(0);
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) {
                own[r] = (uint32_t)off;
                uint32_t h = held[k][r];
                if (h != NONE && --refs[h] == 0) o.life[h].end = (uint32_t)di;
                held[k][r] = u;
                refs[u]++;
            }
            dirty = true;
            break;
        }
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP:
            if (di < F.draws.size()) {
                if (dirty) {
                    lastBegin = (uint32_t)o.flat.size();
                    lastCount = 0;
                    // (registers in a row with the same upload: added once - add() would skip the rest)
                    auto addRun = [&](const uint32_t* own, int r, int e) {
                        while (r < e) {
                            uint32_t ow = own[r++];
                            while (r < e && own[r] == ow) r++;
                            add(ow);
                        }
                    };
                    if (vs && vi && !vi->all)
                        for (int w = 0; w < 4; w++) ForRuns(vi->reads.b[w] & ~vi->pos.b[w], w, [&](int r, int e) { addRun(vown, r, e); });
                    if (ps && pi && !pi->all)
                        for (int w = 0; w < 4; w++) ForRuns(pi->reads.b[w], w, [&](int r, int e) { addRun(pown, r, e); });
                    dirty = false;
                }
                o.begin[di] = lastBegin;
                o.count[di] = lastCount;
                di++;
            }
            break;
        }
        off += c->size;
    }
    for (Owners::Life& l : o.life) if (l.end == NONE) l.end = (uint32_t)di;
}

static bool Interpolable(uint16_t op) {
    return (op == OP_VSCF && C.interp_vs) || (op == OP_PSCF && C.interp_ps) || (op == OP_XFORM && C.interp_xform) ||
           ((op == OP_DPUP || op == OP_DIPUP) && C.interp_up);
}
static bool SameSlot(const Cmd* a, const Cmd* b) {
    if (a->op != b->op || a->size != b->size) return false;
    const uint8_t* pa = (const uint8_t*)(a + 1);
    const uint8_t* pb = (const uint8_t*)(b + 1);
    switch (a->op) {
    case OP_VSCF:
    case OP_PSCF: return ((const PConst*)pa)->start == ((const PConst*)pb)->start && ((const PConst*)pa)->count == ((const PConst*)pb)->count;
    case OP_XFORM: return ((const PXform*)pa)->state == ((const PXform*)pb)->state;
    case OP_DPUP: {
        auto *x = (const PDPUP*)pa, *y = (const PDPUP*)pb;
        return x->posOff >= 0 && x->posOff == y->posOff && x->stride == y->stride && x->vbytes == y->vbytes;
    }
    case OP_DIPUP: {
        auto *x = (const PDIPUP*)pa, *y = (const PDIPUP*)pb;
        return x->posOff >= 0 && x->posOff == y->posOff && x->stride == y->stride && x->vbytes == y->vbytes &&
               x->ibytes == y->ibytes && memcmp(x + 1, y + 1, x->ibytes) == 0;
    }
    }
    return false;
}
// SameSlot as a number, for the constant and transform commands (the ones looked up by slot): two such commands are
// SameSlot exactly when their keys are equal (same op, start and count / transform state; the size follows).
// 0 = not a keyed command.
static inline uint64_t SlotKey(const Cmd* c) {
    if (c->op == OP_VSCF || c->op == OP_PSCF) {
        auto* q = (const PConst*)(c + 1);
        return ((uint64_t)c->op << 56) | ((uint64_t)q->start << 28) | (uint64_t)(q->count & 0x0FFFFFFF);
    }
    if (c->op == OP_XFORM) return ((uint64_t)OP_XFORM << 56) | ((const PXform*)(c + 1))->state;
    return 0;
}
// A small open-addressing map slot key -> command offset, emptied in O(1) (generation counter). Holds up to 512
// keys; callers fall back to the plain scan when a segment has more.
struct SlotMap {
    static const int kCap = 1024;
    uint64_t key[kCap];
    uint32_t val[kCap], gen[kCap] = {};
    uint32_t cur = 1;
    int n = 0;
    void Clear() { if (++cur == 0) { memset(gen, 0, sizeof gen); cur = 1; } n = 0; }
    static int Hash(uint64_t k) { return (int)((k * 0x9E3779B97F4A7C15ull) >> 54); }
    uint32_t* Find(uint64_t k) {
        for (int h = Hash(k);; h = (h + 1) & (kCap - 1)) {
            if (gen[h] != cur) return nullptr;
            if (key[h] == k) return &val[h];
        }
    }
    bool Put(uint64_t k, uint32_t v, bool overwrite) {  // false: full
        for (int h = Hash(k);; h = (h + 1) & (kCap - 1)) {
            if (gen[h] != cur) {
                if (n >= kCap / 2) return false;
                gen[h] = cur; key[h] = k; val[h] = v; n++;
                return true;
            }
            if (key[h] == k) { if (overwrite) val[h] = v; return true; }
        }
    }
};


// (exact floor tests without the C runtime's floorf, which showed up in profiles: |x| < 1e6 fits an int)
static inline bool IsInt(float x) { return std::fabs(x) < 1e6f && x == (float)(int)x; }
static uint32_t g_posPkts, g_bigPkts;  // scene-cut detection: positional packets, and those that jumped

// Decide, per register of a paired constant packet, whether to interpolate it.
//  - registers that feed a vertex position: always, unless the packet as a whole jumps (scale-aware test,
//    relative to the largest value in the packet, so distant objects' clip-space matrices are not snapped)
//  - everything else (texture projections, scrolling, colours, light positions): only when the change is
//    smooth; discrete steps (flipbook frames, toggles, wrap-arounds) snap. Smooth = it moves like it moved the
//    tick before (constant velocity: scrolling fog, a matrix of a moving object, however small the values), or,
//    without that history, a change small against the value. Values on 1/64 steps (atlas frames) never lerp.
static inline bool Quantized(float x) { float s = x * 64.0f; return std::fabs(x) < 1e6f && s == (float)(int)s; }
// A uint32 -> uint32 table, open addressing at most half full (keys never NONE): Set overwrites like map[key] = val.
struct U32Table {
    std::vector<uint32_t> k, v;
    uint32_t mask = 0, n = 0;
    int shift = 32;
    void Reset(size_t most) {  // (room for this many keys)
        size_t cap = 1024;
        while (cap < most * 2) cap <<= 1;
        k.assign(cap, NONE);
        v.resize(cap);
        mask = (uint32_t)cap - 1, n = 0, shift = 32 - __builtin_ctz((uint32_t)cap);
    }
    uint32_t Slot(uint32_t key) const { return (key * 2654435761u) >> shift; }
    void Set(uint32_t key, uint32_t val) {
        if (key == NONE || k.empty()) return;
        for (uint32_t h = Slot(key);; h = (h + 1) & mask) {
            if (k[h] == NONE) { k[h] = key, v[h] = val, n++; return; }
            if (k[h] == key) { v[h] = val; return; }
        }
    }
    const uint32_t* Find(uint32_t key) const {
        if (k.empty()) return nullptr;
        for (uint32_t h = Slot(key);; h = (h + 1) & mask) {
            if (k[h] == key) return &v[h];
            if (k[h] == NONE) return nullptr;
        }
    }
    size_t size() const { return n; }
    void clear() { k.clear(), v.clear(), n = 0; }
};
// Last tick's constant pairing (packet offset in its current frame -> offset in its previous frame), so this tick
// can see how a value moved one tick earlier.
static U32Table g_lastPairs;
static uint32_t g_lastPairsCur, g_lastPairsPrev;  // serials of the two frames it was built for
static const Frame* g_histFrame;                  // the frame before this pairing's previous frame, when known
static const float* HistoryFor(uint32_t prevOff) {
    if (!g_histFrame) return nullptr;
    const uint32_t* it = g_lastPairs.Find(prevOff);
    if (!it || *it >= g_histFrame->buf.size()) return nullptr;
    return (const float*)((const PConst*)((const Cmd*)(g_histFrame->buf.data() + *it) + 1) + 1);
}
// What one ConstLerpMask call adds to the statistics (kept apart so parallel work can commit them in order).
struct LerpStats { uint32_t pos = 0, big = 0, steady = 0, jump = 0; };
static bool ConstLerpMask(const Cmd* c, const Cmd* pc, Mask256& out, const float* hist, LerpStats& ls) {
    auto* q = (const PConst*)(c + 1);
    const float* a = (const float*)((const PConst*)(pc + 1) + 1);
    const float* b = (const float*)(q + 1);
    UINT n = std::min<UINT>(q->count, 256);
    // byte-identical (materials, idle objects): no register changed, and every caller ignores the mask then
    if (memcmp(a, b, n * 16) == 0) return false;
    const Mask256* pm = c->op == OP_VSCF ? &g_pmask[q->ord] : nullptr;
    out.clear();
    float pMax = 1.0f, pDelta = 0;
    bool anyPos = false, changed = false;
    for (UINT r = 0; r < n; r++) {
        const float* x = a + r * 4;
        const float* y = b + r * 4;
        bool finite = true, disc = false;
        float mag = 0, dl = 0;
        for (int k = 0; k < 4; k++) {
            if (!std::isfinite(x[k]) || !std::isfinite(y[k])) { finite = false; break; }
            mag = std::max(mag, std::max(std::fabs(x[k]), std::fabs(y[k])));
            dl = std::max(dl, std::fabs(y[k] - x[k]));
            if (x[k] != y[k] && IsInt(x[k]) && IsInt(y[k])) disc = true;
        }
        if (!finite) continue;
        if (dl == 0) {  // unchanged: lerping is a no-op, but keep the bit so 3-row bones stay whole for slerp
            out.set(r);
            continue;
        }
        changed = true;
        if (pm && pm->test(q->start + r)) {
            anyPos = true;
            pMax = std::max(pMax, mag);
            pDelta = std::max(pDelta, dl);
            out.set(r);
        } else if (C.lerp_nonpos && !disc) {
            bool quant = true, steady = hist != nullptr, back = false;
            for (int k = 0; k < 4; k++) {
                if (x[k] == y[k]) continue;
                quant = quant && Quantized(x[k]) && Quantized(y[k]);
                if (hist) {
                    float v1 = y[k] - x[k], v0 = x[k] - hist[r * 4 + k];
                    if (steady)
                        steady = std::isfinite(v0) && v0 * v1 > 0 && std::fabs(v1 - v0) <= 0.35f * std::max(std::fabs(v1), std::fabs(v0));
                    // moved one way, now back by more than twice that: a wrap-around (scrolling) or a reset
                    back |= std::isfinite(v0) && v0 * v1 < 0 && std::fabs(v1) > 2.0f * std::fabs(v0);
                }
            }
            // a position (w 1) that moved more than 0.6 of its distance from the origin in one tick (in view space a turn of
            // 35 degrees in a tick, 1000 a second) is another thing in that slot:
            // a light set re-sorted as the player runs (blending slides a light through the character: a flash)
            bool jump = false;
            if (std::fabs(x[3] - 1.0f) < 1e-3f && std::fabs(y[3] - 1.0f) < 1e-3f) {
                float la = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]), lb = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
                float dx = y[0] - x[0], dy = y[1] - x[1], dz = y[2] - x[2];
                float mv = std::sqrt(dx * dx + dy * dy + dz * dz);
                jump = std::max(la, lb) > 1e-3f && mv > 0.6f * std::max(la, lb);  // (a turn of 35 degrees in a tick)
                // (or two slots traded their contents: this one now holds what another one held - the set re-sorted)
                for (UINT s = 0; s < n && !jump; s++)
                    jump = s != r && y[0] == a[s * 4] && y[1] == a[s * 4 + 1] && y[2] == a[s * 4 + 2] && std::fabs(y[0]) + std::fabs(y[1]) + std::fabs(y[2]) > 1e-3f;
                // (or, after a tick of hardly moving, a move of more than a tenth of its distance: a far light changing - a
                // re-sort that the turn test above can't tell from a camera turn)
                if (!jump && hist) {
                    const float* h = hist + r * 4;
                    float ex = x[0] - h[0], ey = x[1] - h[1], ez = x[2] - h[2], before = std::sqrt(ex * ex + ey * ey + ez * ez);
                    jump = std::isfinite(before) && std::fabs(h[3] - 1.0f) < 1e-3f && mv > 0.1f * std::max(la, lb) && mv > 8.0f * before;
                }
                if (jump) ls.jump++;
#ifdef PAIRBENCH
                if (jump && g_debugJumps)
                    printf("  draw %u<-%u ", g_dbgDraw, g_dbgPrev), printf("jump %s c%u: (%.4g %.4g %.4g %.4g) -> (%.4g %.4g %.4g %.4g) moved %.4g, length %.4g / %.4g%s\n", c->op == OP_VSCF ? "VS" : "PS", q->start + r, x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3],
                           std::sqrt((y[0] - x[0]) * (y[0] - x[0]) + (y[1] - x[1]) * (y[1] - x[1]) + (y[2] - x[2]) * (y[2] - x[2])), std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]),
                           std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]), !hist ? "" : steady ? " (history: STEADY)" : " (history: not steady)");
#endif
            }
            // a light's range row (MHF's carry a sentinel, |v| > 1e20, in one component) that changes: another light in that
            // slot - a lamp's range never animates (no such row blended in any capture; a lamp of another kind entering
            // the slot changed 361 -> 401). (The sentinel also makes any change look small against the row's size.)
            if (!jump) {
                bool sentinel = false;
                float sdl = 0, smag = 0;
                for (int k = 0; k < 4; k++) {
                    if (!(std::fabs(x[k]) < 1e20f) && !(std::fabs(y[k]) < 1e20f)) { sentinel = true; continue; }
                    sdl = std::max(sdl, std::fabs(y[k] - x[k]));
                    smag = std::max(smag, std::max(std::fabs(x[k]), std::fabs(y[k])));
                }
                if (sentinel && sdl > 0.02f * smag) jump = true, ls.jump++;
            }
            // 2: a view-dependent value (fog, light vectors, shadow lookups) changes a lot in a fast camera turn; the
            // geometry it goes with is blended, so it is too - unless it steps or jumps back
            if (!quant && !jump && (steady || dl <= C.smooth_ratio * mag || (C.lerp_nonpos >= 2 && !back))) {
                out.set(r);
                if (steady && dl > C.smooth_ratio * mag) ls.steady++;
            }
        } else if (C.lerp_nonpos && disc) {
            // (a step, switched anyway - but a range row stepping (361 -> 401: whole numbers) is a light set re-sorted
            // too, and the light's other rows go with it: see SlotRows)
            bool sentinel = false;
            float sdl = 0, smag = 0;
            for (int k = 0; k < 4; k++) {
                if (!(std::fabs(x[k]) < 1e20f) && !(std::fabs(y[k]) < 1e20f)) { sentinel = true; continue; }
                sdl = std::max(sdl, std::fabs(y[k] - x[k]));
                smag = std::max(smag, std::max(std::fabs(x[k]), std::fabs(y[k])));
            }
            if (sentinel && sdl > 0.02f * smag) ls.jump++;
        }
    }
    // Positional data is never snapped per packet: a half-interpolated object (some passes or parts lerped,
    // others not) flickers and breaks depth-equal passes. Big jumps only feed the scene-cut detector.
    if (anyPos) {
        ls.pos++;
        if (pDelta > C.snap_ratio * pMax) ls.big++;
    }
    return changed;
}
static bool XformSnaps(const Cmd* c, const Cmd* p) {
    auto* x = (const PXform*)(c + 1);
    const float* a = &((const PXform*)(p + 1))->m._11;
    const float* b = &x->m._11;
    bool tex = x->state >= D3DTS_TEXTURE0 && x->state <= D3DTS_TEXTURE7;
    float mx = 1.0f, dl = 0;
    for (int i = 0; i < 16; i++) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return true;
        mx = std::max(mx, std::max(std::fabs(a[i]), std::fabs(b[i])));
        dl = std::max(dl, std::fabs(b[i] - a[i]));
    }
    return tex && dl > C.smooth_ratio * mx;
}
// CPU-built vertex data (sprites, grass, text, glows): interpolate only if every triangle moves plausibly.
// Batches are often rebuilt in a different order each frame (distance sorting); pairing vertex k with
// vertex k then smears sprites into long streaks. World space: no vertex may move more than its triangle's
// size. Screen space (RHW): each triangle must move rigidly, and not more than 15% of the screen.
static bool UpConsistent(const uint8_t* cur, const uint8_t* prev, UINT stride, int off, bool rhw, D3DPRIMITIVETYPE type,
                         UINT primCount, const uint8_t* idx, D3DFORMAT fmt, UINT nverts, bool anyDist = false) {
    if (type != D3DPT_TRIANGLELIST && type != D3DPT_TRIANGLESTRIP && type != D3DPT_TRIANGLEFAN) return false;
    const int dims = rhw ? 2 : 3;
    const float W = (float)std::max<UINT>(g_pp.BackBufferWidth, 640);
    auto vi = [&](UINT k) -> UINT {
        if (!idx) return k;
        return fmt == D3DFMT_INDEX32 ? ((const uint32_t*)idx)[k] : ((const uint16_t*)idx)[k];
    };
    for (UINT t = 0; t < primCount; t++) {
        UINT k0 = type == D3DPT_TRIANGLELIST ? 3 * t : type == D3DPT_TRIANGLEFAN ? 0 : t;
        UINT k1 = type == D3DPT_TRIANGLELIST ? 3 * t + 1 : t + 1;
        UINT k2 = type == D3DPT_TRIANGLELIST ? 3 * t + 2 : t + 2;
        UINT v[3] = {vi(k0), vi(k1), vi(k2)};
        float d[3][3], p[3][3];
        float maxDisp = 0;
        for (int j = 0; j < 3; j++) {
            if (v[j] >= nverts) return false;
            const float* a = (const float*)(prev + v[j] * stride + off);
            const float* b = (const float*)(cur + v[j] * stride + off);
            for (int i = 0; i < dims; i++) {
                if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return false;
                p[j][i] = b[i];
                d[j][i] = b[i] - a[i];
                maxDisp = std::max(maxDisp, std::fabs(d[j][i]));
            }
        }
        if (maxDisp == 0) continue;
        float ext = 0, rig = 0;
        for (int i = 0; i < dims; i++) {
            ext = std::max({ext, std::fabs(p[0][i] - p[1][i]), std::fabs(p[0][i] - p[2][i]), std::fabs(p[1][i] - p[2][i])});
            rig = std::max({rig, std::fabs(d[0][i] - d[1][i]), std::fabs(d[0][i] - d[2][i]), std::fabs(d[1][i] - d[2][i])});
        }
        if (rhw) {
            if (rig > 0.5f * ext + 1.0f || (!anyDist && maxDisp > 0.15f * W)) return false;  // (anyDist: a whole string matched)
        } else if (maxDisp > ext) {
            return false;
        }
    }
    return true;
}

static std::vector<char> g_drawFlag;  // per draw of frame N: 1 = its position is not interpolated (debug view)
static std::vector<char> g_runMatch;  // per draw of frame N: 1 paired as part of a whole string (MatchTextRuns), 2 moves with
                                      // its string (no counterpart: it came in at the screen edge, or its text changed)
static std::vector<float> g_runSynth; // (2: where it was, relative to where it is - its string's displacement)

// ---- pairing the constants of matched draws, in two steps
// Step A (parallel, chunks of draws): each matched draw works out what it would pair - owners, live uploads, its
// segment - into records, without touching shared state. Step B (in draw order, cheap): the records are committed
// exactly as the one-thread loop did it: the first owner of a shared upload decides, masks are numbered in order,
// camera candidates and statistics are added in order. Same result as the loop, on several threads.
enum : uint8_t { R_OWNER, R_CONST, R_PLAIN };
enum : uint8_t { F_LERP = 1, F_CROSS = 2, F_BONEJUMP = 4, F_LIVE = 8, F_INV = 16, F_DEFER = 32 };
struct PRec {
    uint32_t draw, oc, found;
    int32_t mask;     // into the chunk's masks (-1 none)
    uint8_t type, flags;
    int16_t boneBase;
    LerpStats ls;
    uint64_t camHash;
};
struct PairChunk {
    std::vector<PRec> recs;
    std::vector<Mask256> masks;
    uint64_t snaps = 0, upSnaps = 0;
    void Clear() { recs.clear(); masks.clear(); snaps = upSnaps = 0; }
};
struct PairScratch {
    SlotMap slotLast, slotLater, slotLive;
    std::vector<uint8_t> later;
    std::vector<uint32_t> offs;
};
static PairScratch g_pscr[kMaxThreads];

// Which matched draw reads each live upload first (the one that decides how it pairs): frame N command offset / 16
// -> draw index, generation-stamped so nothing has to be cleared per tick.
struct FrE { uint32_t gen, val; };  // (together: one cache miss per lookup)
static std::vector<FrE> g_frE;
static uint32_t g_frCur;
// (constant uploads - the only commands looked up - are 20 bytes or more: two never share an offset / 16)
static inline uint32_t FirstReader(uint32_t off) {
    uint32_t k = off >> 4;
    return k < g_frE.size() && g_frE[k].gen == g_frCur ? g_frE[k].val : NONE;
}

// The first matched draw owning each positional packet (by ordinal): usually the one that pairs it, so only it works
// out the lerp mask in step A (a camera matrix can be owned by hundreds of draws); a later owner only gets there
// when every earlier one found no counterpart, and then step B works it out.
static std::vector<uint32_t> g_ownFirst;

static const float* HistoryFor(uint32_t prevOff);
static uint64_t WitnessHash(const float* prev, const float* cur);
static bool InvertibleRegs(const float* v);
static bool CrossesScreen(const float* mp, const float* mc);

// The lerp part of an owner record: mask, skinning base, camera-candidate hash and invertibility.
static void OwnerLerp(const Frame& N, const Frame& P, PRec& r, std::vector<Mask256>& masks) {
    const Cmd* c = (const Cmd*)(N.buf.data() + r.oc);
    auto* q = (const PConst*)(c + 1);
    const Cmd* pc = (const Cmd*)(P.buf.data() + r.found);
    const float* fp = (const float*)((const PConst*)(pc + 1) + 1);
    const float* cp = (const float*)(q + 1);
    Mask256 m;
#ifdef PAIRBENCH
    g_dbgDraw = r.draw, g_dbgPrev = ~0u;  // (an owner: its counterpart draw is not at hand here)
#endif
    if (!ConstLerpMask(c, pc, m, HistoryFor(r.found), r.ls)) return;
    r.flags |= F_LERP;
    r.mask = (int32_t)masks.size();
    masks.push_back(m);
    if (const VSInfo* vi = GetVSInfo(N.draws[r.draw].vs))  // skinning palette: 3 registers per bone
        if (vi->relMax - vi->relBase == 2 && (int)(q->start + q->count) > vi->relBase && q->count >= 3) r.boneBase = (int16_t)vi->relBase;
    if (q->count == 4) {
        r.camHash = WitnessHash(fp, cp);
        if (InvertibleRegs(cp)) r.flags |= F_INV;
    }
}
PROF_NOINLINE static void OwnersA(const Frame& N, uint32_t i, const Frame& P, uint32_t j, PairChunk& out) {
    const uint8_t* bn = N.buf.data();
    const uint8_t* bp = P.buf.data();
    for (uint32_t k = 0; k < g_ownN.count[i]; k++) {
        uint32_t oc = g_ownN.flat[g_ownN.begin[i] + k];
        const Cmd* c = (const Cmd*)(bn + oc);
        auto* q = (const PConst*)(c + 1);
        PRec r{i, oc, NONE, -1, R_OWNER, 0, -1, {}, 0};
        for (uint32_t m = 0; m < g_ownP.count[j]; m++) {
            const Owners::E& pe = g_ownP.info[g_ownP.begin[j] + m];
            if (pe.start == q->start && pe.count == q->count) { r.found = g_ownP.flat[g_ownP.begin[j] + m]; break; }
        }
        if (r.found != NONE) {
            const float* fp = (const float*)((const PConst*)((const Cmd*)(bp + r.found) + 1) + 1);
            const float* cp = (const float*)(q + 1);
            if (q->count == 4) {
                if (CrossesScreen(fp, cp)) r.flags |= F_CROSS;
            } else if (q->count >= 3 && q->count % 3 == 0) {
                double dx = fp[3] - cp[3], dy = fp[7] - cp[7], dz = fp[11] - cp[11];
                if (dx * dx + dy * dy + dz * dz > 150.0 * 150.0) r.flags |= F_BONEJUMP;
            }
            if (!(r.flags & (F_CROSS | F_BONEJUMP)) && C.interp_vs) {
                if (g_ownFirst[q->ord] == i) OwnerLerp(N, P, r, out.masks);
                else r.flags |= F_DEFER;
            }
        }
        out.recs.push_back(r);
    }
}

// Non-positional constants read by matched draws: each live upload pairs with the upload its counterpart read for
// the same slot (its first matched reader decides). Uploads first read by a matched draw are left alone by SegmentA.
PROF_NOINLINE static void LiveA(const Frame& N, uint32_t i, const Frame& P, uint32_t j, PairChunk& out, PairScratch& sc) {
    const uint8_t* bn = N.buf.data();
    const uint8_t* bp = P.buf.data();
    bool built = false;  // sc.slotLive holds the counterpart's live uploads (at most 48: always fits)
    for (uint32_t k = 0; k < g_liveN.count[i]; k++) {
        uint32_t oc = g_liveN.flat[g_liveN.begin[i] + k];
        if (FirstReader(oc) != i) continue;  // an earlier reader decides
        const Cmd* c = (const Cmd*)(bn + oc);
        if (!Interpolable(c->op)) continue;
        if (c->op == OP_VSCF && g_pmask[((const PConst*)(c + 1))->ord].any()) continue;  // OwnersA's job
        uint32_t found = NONE;
        if (!built) {
            built = true;
            sc.slotLive.Clear();
            for (uint32_t m = 0; m < g_liveP.count[j]; m++) {
                uint32_t op = g_liveP.flat[g_liveP.begin[j] + m];
                if (uint64_t kp = SlotKey((const Cmd*)(bp + op))) sc.slotLive.Put(kp, op, false);
            }
        }
        if (uint64_t kc = SlotKey(c)) {
            if (uint32_t* v = sc.slotLive.Find(kc)) found = *v;
        }
        if (found == NONE) continue;  // the counterpart read nothing in that slot: keep the current value
        PRec r{i, oc, found, -1, R_CONST, F_LIVE, -1, {}, 0};
        Mask256 mk;
#ifdef PAIRBENCH
        g_dbgDraw = i, g_dbgPrev = j;
#endif
        if (ConstLerpMask(c, (const Cmd*)(bp + found), mk, HistoryFor(found), r.ls)) {
            r.flags |= F_LERP;
            r.mask = (int32_t)out.masks.size();
            out.masks.push_back(mk);
        }
        out.recs.push_back(r);
    }
}

PROF_NOINLINE static void SegmentA(const Frame& N, uint32_t i, const Frame& P, uint32_t j, PairChunk& out, PairScratch& sc) {
    const DrawRec& dn = N.draws[i];
    const DrawRec& dp = P.draws[j];
    const uint8_t* bn = N.buf.data();
    const uint8_t* bp = P.buf.data();
    uint32_t qEnd = dp.off + ((const Cmd*)(bp + dp.off))->size;
    uint32_t end = dn.off + ((const Cmd*)(bn + dn.off))->size;
    // which uploads are overwritten later in this segment, and the counterpart segment's last upload per slot
    // (what the scans below would find), each from one pass when first needed; a segment with too many slots
    // keeps the scans
    bool tables = true, built = false;
    auto build = [&]() {
        built = true;
        sc.offs.clear();
        for (uint32_t o = dn.seg; o < end;) { sc.offs.push_back(o); o += ((const Cmd*)(bn + o))->size; }
        sc.later.assign(sc.offs.size(), 0);
        sc.slotLater.Clear();
        for (size_t k = sc.offs.size(); k-- > 0 && tables;) {
            uint64_t key = SlotKey((const Cmd*)(bn + sc.offs[k]));
            if (!key) continue;
            if (sc.slotLater.Find(key)) sc.later[k] = 1;
            else tables = sc.slotLater.Put(key, 1, false);
        }
        sc.slotLast.Clear();
        for (uint32_t s = dp.seg; s < qEnd && tables;) {
            const Cmd* pc = (const Cmd*)(bp + s);
            if (uint64_t key = SlotKey(pc)) tables = sc.slotLast.Put(key, s, true);
            s += pc->size;
        }
    };
    size_t idx = 0;
    for (uint32_t o = dn.seg; o < end; idx++) {
        const Cmd* c = (const Cmd*)(bn + o);
        o += c->size;
        if (!Interpolable(c->op)) continue;
        bool positional = c->op != OP_VSCF || g_pmask[((const PConst*)(c + 1))->ord].any();
        if (c->op == OP_VSCF && positional) continue;  // paired through the draws that read it (OwnersA)
        if ((c->op == OP_VSCF || c->op == OP_PSCF) && FirstReader((uint32_t)((const uint8_t*)c - bn)) <= i) continue;  // LiveA
        // Only what is live at the draw matters: the game sets up several objects, skips drawing the ones that turn
        // out invisible, and their uploads pile up before the next real draw, in a number and order that change
        // every tick. An upload overwritten later in the segment is irrelevant (not paired); a live one pairs with
        // the counterpart's last upload of the same slot. (Walking both segments in order instead paired a clerk's
        // lighting with a skipped object's, blending her brighter in the in-between frames.)
        uint32_t found = NONE;
        if (c->op == OP_DPUP || c->op == OP_DIPUP) {
            if ((const uint8_t*)c - bn == (ptrdiff_t)dn.off && SameSlot(c, (const Cmd*)(bp + dp.off))) found = dp.off;
        } else if ((built || (build(), true)) && tables) {
            if (sc.later[idx]) continue;  // overwritten later in the segment
            if (uint32_t* v = sc.slotLast.Find(SlotKey(c))) found = *v;
        } else {
            bool overwritten = false;
            for (uint32_t k = o; k < end && !overwritten;) {
                const Cmd* x = (const Cmd*)(bn + k);
                k += x->size;
                overwritten = SameSlot(c, x);
            }
            if (overwritten) continue;
            for (uint32_t s = dp.seg; s < qEnd;) {
                const Cmd* pc = (const Cmd*)(bp + s);
                if (SameSlot(c, pc)) found = s;
                s += pc->size;
            }
        }
        if (found == NONE) continue;
        // (the scans' result is a command of the same slot: what the old "first match from there" loop paired with)
        const Cmd* pc = (const Cmd*)(bp + found);
        uint32_t s = found;
        uint32_t cur = (uint32_t)((const uint8_t*)c - bn);
        if (c->op == OP_VSCF || c->op == OP_PSCF) {
            PRec r{i, cur, s, -1, R_CONST, 0, -1, {}, 0};
            Mask256 m;
#ifdef PAIRBENCH
            g_dbgDraw = i, g_dbgPrev = j;
#endif
            if (ConstLerpMask(c, pc, m, HistoryFor(s), r.ls)) {
                r.flags |= F_LERP;
                r.mask = (int32_t)out.masks.size();
                out.masks.push_back(m);
            }
            out.recs.push_back(r);
            continue;
        } else if (c->op == OP_XFORM) {
            if (XformSnaps(c, pc)) { out.snaps++; continue; }
        } else if (c->op == OP_DPUP) {
            auto* x = (const PDPUP*)(c + 1);
            if (!UpConsistent((const uint8_t*)(x + 1), (const uint8_t*)((const PDPUP*)(pc + 1) + 1), x->stride, x->posOff,
                              x->rhw, x->type, x->primCount, nullptr, D3DFMT_UNKNOWN, x->vbytes / std::max<UINT>(x->stride, 1),
                              i < g_runMatch.size() && g_runMatch[i])) {
                out.upSnaps++;
                g_drawFlag[i] = 1;
                continue;
            }
        } else if (c->op == OP_DIPUP) {
            auto* x = (const PDIPUP*)(c + 1);
            UINT ibA = (x->ibytes + 3) & ~3u;
            if (!UpConsistent((const uint8_t*)(x + 1) + ibA, (const uint8_t*)((const PDIPUP*)(pc + 1) + 1) + ibA, x->stride,
                              x->posOff, x->rhw, x->type, x->primCount, (const uint8_t*)(x + 1), x->fmt,
                              x->vbytes / std::max<UINT>(x->stride, 1), i < g_runMatch.size() && g_runMatch[i])) {
                out.upSnaps++;
                g_drawFlag[i] = 1;
                continue;
            }
        }
        out.recs.push_back(PRec{i, cur, s, -1, R_PLAIN, 0, -1, {}, 0});
    }
}

// A light set re-sorted: ConstLerpMask switched a row of an upload (another light in that slot now). The light's other
// rows - its range and colours, maybe uploaded elsewhere and shared - describe the new light too: of every upload in
// effect (same shader stage) at a draw that reads such an upload, the blended light-like rows (colours and intensities,
// range rows) that changed by more than 2% of their size switch with it. Matrices, positions (their own test) and rows
// that hardly move keep blending. (The new light's position
// with the old light's range and colour blended in lit the player brightly for one or two in-between frames.)
static std::vector<uint32_t> g_jumped;  // uploads of frame N (offsets) with a switched slot, from step B
// Lights traded slots: a pixel-shader position row (w 1) now holds, to the bit, a position that a light row held last
// tick - a lamp that was in another slot (or another object's slot). Skinned objects get their lights in world space,
// where a lamp keeps its bits from frame to frame, and the turn test in ConstLerpMask can't see such a move: from the
// world's origin, two lamps 2,000 apart are a small change. (Two lamps that traded slots as the player ran between them
// were both blended to the halfway point - right where the player was: a flash for one in-between frame.)
struct SlotPos {
    uint32_t x, y, z;
    bool operator<(const SlotPos& o) const { return x != o.x ? x < o.x : y != o.y ? y < o.y : z < o.z; }
    bool operator==(const SlotPos& o) const { return x == o.x && y == o.y && z == o.z; }
};
PROF_NOINLINE static void SlotSwaps(const Frame& N, const Frame& P) {
    static std::vector<SlotPos> old;
    struct Cand { SlotPos at; uint32_t pair, row; };
    static std::vector<Cand> cand;
    old.clear(), cand.clear();
    auto key = [](const float* v) { SlotPos k; memcpy(&k, v, 12); return k; };
    for (uint32_t i = 0; i < g_pairs.size(); i++) {
        const Pair& p = g_pairs[i];
        if (p.mi == NONE || p.prev == NONE || p.synth != NONE) continue;
        const Cmd* c = (const Cmd*)(N.buf.data() + p.cur);
        if (c->op != OP_PSCF) continue;
        auto* q = (const PConst*)(c + 1);
        const float* y = (const float*)(q + 1);
        const float* x = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + p.prev) + 1) + 1);
        const Mask256& m = g_pairMask[p.mi];
        for (UINT r = 0; r < q->count && r < 256; r++) {
            const float *a = x + r * 4, *b = y + r * 4;
            if (!(std::fabs(a[3] - 1.0f) < 1e-3f) || !(std::fabs(b[3] - 1.0f) < 1e-3f)) continue;
            if (std::fabs(a[0]) + std::fabs(a[1]) + std::fabs(a[2]) > 1e-3f) old.push_back(key(a));
            // (colour-sized rows - a fade ending at white - aren't positions)
            if (m.test((int)r) && memcmp(a, b, 12) && std::max(std::fabs(b[0]), std::max(std::fabs(b[1]), std::fabs(b[2]))) > 16.0f) cand.push_back({key(b), i, r});
        }
    }
    if (cand.empty()) return;
    std::sort(old.begin(), old.end());
    uint32_t lastPair = NONE;
    for (const Cand& k : cand) {
        if (!std::binary_search(old.begin(), old.end(), k.at)) continue;
        const Pair& p = g_pairs[k.pair];
        g_pairMask[p.mi].reset((int)k.row);
        St.slotJumps++;
        if (k.pair != lastPair) g_jumped.push_back(p.cur), lastPair = k.pair;
#ifdef PAIRBENCH
        if (g_debugJumps) {
            auto* q = (const PConst*)((const Cmd*)(N.buf.data() + p.cur) + 1);
            const float* b = (const float*)(q + 1) + k.row * 4;
            printf("  traded PS c%u: now (%.4g %.4g %.4g), another light's last position\n", q->start + k.row, b[0], b[1], b[2]);
        }
#endif
    }
}
PROF_NOINLINE static void SlotRows(const Frame& N, const Frame& P) {
    if (g_jumped.empty()) return;
    std::sort(g_jumped.begin(), g_jumped.end());
    // the draws each switched upload is in effect for (CollectLive's lifetimes), per stage, merged; an upload whose own
    // lifetime meets them is in effect at a draw together with one
    const std::vector<Owners::Life>& life = g_liveN.life;
    auto lifeOf = [&](uint32_t off) -> const Owners::Life* {
        auto it = std::lower_bound(life.begin(), life.end(), off, [](const Owners::Life& l, uint32_t v) { return l.off < v; });
        return it != life.end() && it->off == off ? &*it : nullptr;
    };
    static std::vector<std::pair<uint32_t, uint32_t>> span[2];
    span[0].clear(), span[1].clear();
    for (uint32_t o : g_jumped)
        if (const Owners::Life* l = lifeOf(o))
            if (l->first < l->end) span[l->stage].push_back({l->first, l->end});
    for (auto& sp : span) {
        std::sort(sp.begin(), sp.end());
        size_t w = 0;
        for (size_t k = 0; k < sp.size(); k++)
            if (w && sp[k].first <= sp[w - 1].second) sp[w - 1].second = std::max(sp[w - 1].second, sp[k].second);
            else sp[w++] = sp[k];
        sp.resize(w);
    }
    const uint8_t* b = N.buf.data();
    // (marked uploads, by offset / 16 - uploads are 20 bytes or more - generation-stamped)
    static std::vector<uint32_t> stamp;
    static uint32_t gen;
    if (++gen == 0) { std::fill(stamp.begin(), stamp.end(), 0); gen = 1; }
    if (stamp.size() < N.buf.size() / 16 + 1) stamp.resize(N.buf.size() / 16 + 1, 0);
    for (const Owners::Life& l : life) {
        if (l.first >= l.end || span[l.stage].empty()) continue;
        const auto& sp = span[l.stage];
        auto it = std::upper_bound(sp.begin(), sp.end(), l.first, [](uint32_t v, const std::pair<uint32_t, uint32_t>& x) { return v < x.second; });
        if (it != sp.end() && it->first < l.end) stamp[l.off >> 4] = gen;
    }
    for (const Pair& p : g_pairs) {
        if (p.mi == NONE || p.prev == NONE || p.synth != NONE || stamp[p.cur >> 4] != gen) continue;
        const Cmd* c = (const Cmd*)(b + p.cur);
        if (c->op != OP_VSCF && c->op != OP_PSCF) continue;
        auto* q = (const PConst*)(c + 1);
        const float* y = (const float*)(q + 1);
        const float* x = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + p.prev) + 1) + 1);
        const Mask256* pm = c->op == OP_VSCF ? &g_pmask[q->ord] : nullptr;
        Mask256& m = g_pairMask[p.mi];
        for (UINT row = 0; row < q->count && row < 256; row++) {
            if (!m.test((int)row) || (pm && pm->test((int)(q->start + row)))) continue;  // (vertex positions keep blending)
            // a light position (w 1) in effect with a re-sorted slot that moved more than a twentieth of its distance: the
            // new light of that slot (a lamp keeps its world-space bits; ConstLerpMask's turn test measures from the
            // world's origin there) - an object's own lights, moving with it, move far less in a tick
            {
                const float *a = x + row * 4, *v = y + row * 4;
                if (std::fabs(a[3] - 1.0f) < 1e-3f && std::fabs(v[3] - 1.0f) < 1e-3f &&
                    std::max(std::fabs(v[0]), std::max(std::fabs(v[1]), std::fabs(v[2]))) > 16.0f) {
                    float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), lb = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    float mx = v[0] - a[0], my = v[1] - a[1], mz = v[2] - a[2];
                    float mv = std::sqrt(mx * mx + my * my + mz * mz);
                    // (one that moves on like it moved the tick before is an object's own light going with it)
                    const float* h = HistoryFor(p.prev);
                    bool steady = h != nullptr;
                    if (h) {
                        h += row * 4;
                        for (int k = 0; k < 3 && steady; k++) {
                            float v1 = v[k] - a[k], v0 = a[k] - h[k];
                            if (v1 == 0) continue;
                            steady = std::isfinite(v0) && v0 * v1 > 0 && std::fabs(v1 - v0) <= 0.35f * std::max(std::fabs(v1), std::fabs(v0));
                        }
                    }
                    if (!steady && mv > (h ? 0.05f : 0.1f) * std::max(la, lb)) {
                        m.reset((int)row), St.slotJumps++;
#ifdef PAIRBENCH
                        if (g_debugJumps)
                            printf("  coupled position %s c%u: (%.4g %.4g %.4g) -> (%.4g %.4g %.4g)%s\n", c->op == OP_PSCF ? "PS" : "VS", q->start + row, a[0], a[1], a[2], v[0], v[1], v[2], h ? "" : " (no history)");
#endif
                    }
                    continue;
                }
            }
            float dl = 0, mag = 0;
            bool sentinel = false, negative = false;
            for (int k = 0; k < 4; k++) {
                float a = x[row * 4 + k], v = y[row * 4 + k];
                if (!(std::fabs(a) < 1e20f) || !(std::fabs(v) < 1e20f)) { sentinel = true; continue; }  // (a sentinel, not a size)
                negative |= a < 0 || v < 0;
                mag = std::max(mag, std::max(std::fabs(a), std::fabs(v)));
                dl = std::max(dl, std::fabs(v - a));
            }
            // (light data: a range row - the sentinel - or colours and intensities, never negative, colour-sized; matrix rows,
            // directions and positions keep blending - the geometry needs them, positions have their own test)
            if (!sentinel && (negative || mag > 16.0f)) continue;
            if (dl > 0.02f * mag) {
                m.reset((int)row), St.slotRows++;
#ifdef PAIRBENCH
                if (g_debugJumps)
                    printf("  coupled %s c%u: (%.4g %.4g %.4g %.4g) -> (%.4g %.4g %.4g %.4g)\n", c->op == OP_PSCF ? "PS" : "VS", q->start + row, x[row * 4], x[row * 4 + 1],
                           x[row * 4 + 2], x[row * 4 + 3], y[row * 4], y[row * 4 + 1], y[row * 4 + 2], y[row * 4 + 3]);
#endif
            }
        }
    }
}

// Positional constants: pair the uploads each matched draw reads with those its counterpart read.
static std::vector<char> g_pktPaired;  // per VSCF ordinal of frame N
struct NeedSynth { uint32_t draw, oc; };
static std::vector<NeedSynth> g_needSynth;
static void AddCameraCandidate(void* rt, UINT start, const float* prev, const float* cur);

// An object whose origin is off-screen on opposite sides in the two real frames cannot plausibly cross the whole
// screen within one tick; such a pair is two different objects (or a bad estimate) and would sweep a "ghost" across
// the middle of the in-between frames.
static bool CrossesScreen(const float* mp, const float* mc) {
    double pw = mp[15], cw = mc[15];
    if (pw <= 1e-4 || cw <= 1e-4) return false;
    double px = mp[3] / pw, py = mp[7] / pw, cx = mc[3] / cw, cy = mc[7] / cw;
    const double L = 1.25;
    return (px > L && cx < -L) || (px < -L && cx > L) || (py > L && cy < -L) || (py < -L && cy > L);
}
// Diagnostics for the rolling recorder: per tick, the objects whose blended path moves far across the screen.
struct Sweep { uint32_t tick, draw; char kind; float px, py, pw, cx, cy, cw; };
static std::vector<Sweep> g_sweeps;
static size_t g_sweepPos;
static uint32_t g_tickNo;
static void NoteSweep(uint32_t draw, char kind, const float* mp, const float* mc) {
    if (!C.retro) return;
    double pw = mp[15], cw = mc[15];
    if (pw <= 1e-4 || cw <= 1e-4) return;
    double dx = mp[3] / pw - mc[3] / cw, dy = mp[7] / pw - mc[7] / cw;
    if (dx * dx + dy * dy < 0.25) return;  // moved less than half a screen-height in NDC
    // only real projections (world matrices / orthographic ones would just flood the log)
    if (std::fabs(mp[12]) + std::fabs(mp[13]) + std::fabs(mp[14]) < 1e-6 || std::fabs(mc[12]) + std::fabs(mc[13]) + std::fabs(mc[14]) < 1e-6) return;
    if (std::fabs(mc[3] / cw) > 100 || std::fabs(mc[7] / cw) > 100) return;
    if (g_sweeps.size() != 2048) g_sweeps.assign(2048, Sweep{});
    g_sweeps[g_sweepPos++ % 2048] = {g_tickNo, draw, kind, (float)(mp[3] / pw), (float)(mp[7] / pw), (float)pw,
                                     (float)(mc[3] / cw), (float)(mc[7] / cw), (float)cw};
}
// ------------------------------------------------------------------------------------------------ camera motion
// Objects that just came into view (culling), or switched level of detail, have no previous-frame data. For a
// static object the change of its 4x4 world-view-projection between frames is the same for every object in a
// pass: Mp * Mc^-1 = (P V)prev * (P V)cur^-1 =: D. D is measured from matched draws (taking the value most of
// them agree on, so moving objects don't count) and then applied to the new objects: Mprev_est = D * Mcur.
struct M4 { double m[4][4]; };
static bool Inverse(const M4& a, M4& out) {
    double t[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++) t[r][c] = c < 4 ? a.m[r][c] : (c - 4 == r ? 1.0 : 0.0);
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int r = col + 1; r < 4; r++) if (std::fabs(t[r][col]) > std::fabs(t[piv][col])) piv = r;
        if (std::fabs(t[piv][col]) < 1e-12) return false;
        if (piv != col) for (int c = 0; c < 8; c++) std::swap(t[piv][c], t[col][c]);
        double inv = 1.0 / t[col][col];
        for (int c = 0; c < 8; c++) t[col][c] *= inv;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = t[r][col];
            if (f != 0) for (int c = 0; c < 8; c++) t[r][c] -= f * t[col][c];
        }
    }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out.m[r][c] = t[r][c + 4];
    return true;
}
static M4 Mul(const M4& a, const M4& b) {
    M4 o;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a.m[r][k] * b.m[k][c];
            o.m[r][c] = s;
        }
    return o;
}
static M4 FromRegs(const float* v) {  // 4 registers = 4 rows
    M4 o;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o.m[r][c] = v[r * 4 + c];
    return o;
}
// A camera-motion candidate: the 4x4 of one object in both frames (pointers into the frame buffers, valid while
// BuildPairs runs); prev == nullptr means unchanged (identity). The matrix prev * cur^-1 is only computed for the
// candidates SolveCameras samples (at most 96 of up to 512 per context).
struct CamCand { const float *prev, *cur; };
struct CamCtx {
    std::vector<CamCand> cand;
    std::unordered_set<uint64_t> seen;
    bool has = false;
    M4 D;
    bool path = false;  // CameraPaths: D^-1, log D, and this replay's D^(1-t)
    M4 Dinv, LogD, Ct;
    bool hasA = false;  // AnchorMap: where a world-fixed origin was, from where it is (pairing only)
    double A[3][4];
};
struct CamPrev { bool has = false, hasA = false; M4 D; double A[3][4]; };
static std::unordered_map<uint64_t, CamPrev> g_camPrev;  // last tick's final estimates (MatchGroup's poor pairings)
static uint32_t g_camPrevSerial;                          // (the frame they were made for: good only for the next one)
static std::unordered_map<uint64_t, CamCtx> g_cam;
static uint64_t CamKey(void* rt, UINT start) { return (((uint64_t)(uintptr_t)rt << 16) ^ start) | (1ull << 63); }  // (never 0: "no context")
// One object is one witness: MHF draws many objects several times with the same matrices (main pass plus fog
// passes), and three copies of one fog card must not outvote the scene and define the camera motion.
static inline uint32_t Rotl32(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }
static inline uint32_t MurStep(uint32_t h, uint32_t k) {
    k *= 0xcc9e2d51u; k = Rotl32(k, 15); k *= 0x1b873593u;
    h ^= k; h = Rotl32(h, 13);
    return h * 5 + 0xe6546b64u;
}
static inline uint32_t Fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
// (rt, start) -> the context's witness hashes, in one flat table emptied with the contexts (was an unordered_set
// per context: a heap node per witness)
struct WitnessSet {
    static const int kCap = 1 << 16;
    std::vector<uint64_t> ctx, hash;
    std::vector<uint32_t> gen;
    uint32_t cur = 1;
    int n = 0;
    void Clear() { if (++cur == 0) { std::fill(gen.begin(), gen.end(), 0); cur = 1; } n = 0; }
    int Insert(uint64_t c, uint64_t h) {  // 1 new, 0 seen, -1 full
        if (gen.empty()) { ctx.resize(kCap); hash.resize(kCap); gen.assign(kCap, 0); }
        for (int i = (int)(Fmix32((uint32_t)h ^ (uint32_t)(h >> 32) ^ (uint32_t)c ^ Rotl32((uint32_t)(c >> 32), 11)) & (kCap - 1));; i = (i + 1) & (kCap - 1)) {
            if (gen[i] != cur) {
                if (n >= kCap / 2) return -1;
                gen[i] = cur; ctx[i] = c; hash[i] = h; n++;
                return 1;
            }
            if (ctx[i] == c && hash[i] == h) return 0;
        }
    }
};
static WitnessSet g_witness;
// 64-bit identity of a (prev, cur) matrix pair: four independent 32-bit multiply-rotate lanes (murmur3 steps) over
// the 32 words, folded into two mixed halves. (FNV over the 128 bytes was a chain of 160 dependent 64-bit multiplies
// - several instructions each in this 32-bit build - and the costliest line of the pairing in busy scenes.)
static uint64_t WitnessHash(const float* prev, const float* cur) {
    uint32_t w[32];
    memcpy(w, prev, 64);
    memcpy(w + 16, cur, 64);
    uint32_t a = 0x243f6a88u, b = 0x85a308d3u, c = 0x13198a2eu, d = 0x03707344u;
    for (int k = 0; k < 32; k += 4) {
        a = MurStep(a, w[k]);
        b = MurStep(b, w[k + 1]);
        c = MurStep(c, w[k + 2]);
        d = MurStep(d, w[k + 3]);
    }
    uint32_t h1 = Fmix32(a ^ Rotl32(b, 7) ^ Rotl32(c, 13) ^ Rotl32(d, 19) ^ 128u);
    uint32_t h2 = Fmix32(b ^ Rotl32(c, 7) ^ Rotl32(d, 13) ^ Rotl32(a, 19) ^ 0x9e3779b9u);
    return ((uint64_t)h1 << 32) | h2;
}
// (the last call's arguments: the same call again - consecutive draws reading one camera upload - is witnessed already)
static struct { bool valid; uint64_t key; const float *prev, *cur; } g_fwLast;
static bool FirstWitness(CamCtx& cx, uint64_t ctxKey, const float* prev, const float* cur) {
    if (g_fwLast.valid && g_fwLast.key == ctxKey && g_fwLast.prev == prev && g_fwLast.cur == cur) return false;
    g_fwLast = {true, ctxKey, prev, cur};
    uint64_t h = WitnessHash(prev, cur);
    int r = g_witness.Insert(ctxKey, h);
    return r < 0 ? cx.seen.insert(h).second : r == 1;  // (table full: the old per-context set)
}
// g_cam with the last context looked up kept at hand (consecutive candidates are usually the same pass and slot)
static uint64_t g_camLastKey = ~0ull;
static CamCtx* g_camLast;
static CamCtx& CamAt(uint64_t key) {
    if (key != g_camLastKey || !g_camLast) { g_camLast = &g_cam[key]; g_camLastKey = key; }
    return *g_camLast;
}
static void CamClear() {
    g_cam.clear();
    g_camLast = nullptr;
    g_camLastKey = ~0ull;
    g_witness.Clear();
    g_fwLast.valid = false;
}
// Inverse() succeeds exactly when this does: the same elimination on the left half only (the right half never
// feeds back into it), at half the cost.
static bool Invertible(const M4& a) {
    double t[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) t[r][c] = a.m[r][c];
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int r = col + 1; r < 4; r++) if (std::fabs(t[r][col]) > std::fabs(t[piv][col])) piv = r;
        if (std::fabs(t[piv][col]) < 1e-12) return false;
        if (piv != col) for (int c = 0; c < 4; c++) std::swap(t[piv][c], t[col][c]);
        double inv = 1.0 / t[col][col];
        for (int c = 0; c < 4; c++) t[col][c] *= inv;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = t[r][col];
            if (f != 0) for (int c = 0; c < 4; c++) t[r][c] -= f * t[col][c];
        }
    }
    return true;
}
static bool InvertibleRegs(const float* v) { return Invertible(FromRegs(v)); }
static M4 CandMatrix(const CamCand& c) {
    M4 o{};
    if (!c.prev) {
        for (int r = 0; r < 4; r++) o.m[r][r] = 1;
        return o;
    }
    M4 inv;
    Inverse(FromRegs(c.cur), inv);
    return Mul(FromRegs(c.prev), inv);
}
// AddCameraCandidate with the witness hash and the invertibility worked out beforehand (step A)
static void AddCameraCandidatePre(void* rt, UINT start, const float* prev, const float* cur, uint64_t hash, bool inv) {
    uint64_t key = CamKey(rt, start);
    CamCtx& cx = CamAt(key);
    if (cx.cand.size() >= 512) return;
    int r = g_witness.Insert(key, hash);
    if (!(r < 0 ? cx.seen.insert(hash).second : r == 1)) return;
    if (!inv) return;
    cx.cand.push_back({prev, cur});
}
static void AddCameraCandidate(void* rt, UINT start, const float* prev, const float* cur) {
    uint64_t key = CamKey(rt, start);
    CamCtx& cx = CamAt(key);
    if (cx.cand.size() >= 512 || !FirstWitness(cx, key, prev, cur)) return;
    if (!Invertible(FromRegs(cur))) return;
    cx.cand.push_back({prev, cur});
}
// The camera's motion between the two real frames as a path. D (prev = D * cur for whatever sits still) is the camera
// move conjugated by the projection; its fractional powers D^s = exp(s log D) move along that move's arc: a turn
// stays a turn, where blending two matrices linearly shrinks the view towards the centre mid-way (1.5% at 20 degrees
// per tick - the scene "breathes" in a fast camera turn, fine detail like grass visibly shifts).
static double NormInf(const M4& a) {
    double m = 0;
    for (int r = 0; r < 4; r++) {
        double s = 0;
        for (int c = 0; c < 4; c++) s += std::fabs(a.m[r][c]);
        m = std::max(m, s);
    }
    return m;
}
static M4 Ident4() {
    M4 o{};
    for (int r = 0; r < 4; r++) o.m[r][r] = 1;
    return o;
}
static M4 Comb(const M4& a, double sa, const M4& b, double sb) {
    M4 o;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) o.m[r][c] = a.m[r][c] * sa + b.m[r][c] * sb;
    return o;
}
static bool SqrtM4(const M4& Ain, M4& out) {  // Denman-Beavers (out may be Ain)
    const M4 A = Ain;
    M4 Y = A, Z = Ident4();
    for (int k = 0; k < 50; k++) {
        M4 Yi, Zi;
        if (!Inverse(Y, Yi) || !Inverse(Z, Zi)) return false;
        M4 Yn = Comb(Y, 0.5, Zi, 0.5), Zn = Comb(Z, 0.5, Yi, 0.5);
        double d = NormInf(Comb(Yn, 1, Y, -1));
        Y = Yn;
        Z = Zn;
        if (d <= 1e-14 * std::max(1.0, NormInf(Y))) break;
    }
    out = Y;
    return NormInf(Comb(Mul(Y, Y), 1, A, -1)) <= 1e-9 * std::max(1.0, NormInf(A));
}
static bool LogM4(const M4& A, M4& L) {  // inverse scaling and squaring
    M4 X = A;
    int k = 0;
    while (NormInf(Comb(X, 1, Ident4(), -1)) > 0.2) {
        if (k >= 16 || !SqrtM4(X, X)) return false;
        k++;
    }
    M4 E = Comb(X, 1, Ident4(), -1), P = E;
    L = M4{};
    for (int n = 1; n <= 24; n++) {
        L = Comb(L, 1, P, ((n & 1) ? 1.0 : -1.0) / n);
        P = Mul(P, E);
    }
    double s = std::ldexp(1.0, k);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            L.m[r][c] *= s;
            if (!std::isfinite(L.m[r][c])) return false;
        }
    return true;
}
static M4 ExpM4(const M4& L, double s) {  // scaling and squaring
    M4 S = Comb(L, s, L, 0);
    int j = 0;
    for (double n = NormInf(S); n > 0.25 && j < 40; n *= 0.5) j++;
    S = Comb(S, std::ldexp(1.0, -j), S, 0);
    M4 R = Ident4(), T = Ident4();
    for (int n = 1; n <= 18; n++) {
        T = Comb(Mul(T, S), 1.0 / n, T, 0);
        R = Comb(R, 1, T, 1);
    }
    for (int k = 0; k < j; k++) R = Mul(R, R);
    return R;
}
// after the matching (the camera estimates are final): each context's path, if D has a clean logarithm
static void CameraPaths() {
    for (auto& kv : g_cam) {
        CamCtx& cx = kv.second;
        cx.path = false;
        St.camCtx++;
        if (!cx.has || !C.cam_paths) continue;
        St.camSolved++;
        // (a camera move: its turn angle from the trace, which the projection does not change - 2 + 2 cos(angle))
        double nd = NormInf(Comb(cx.D, 1, Ident4(), -1));
        double cosA = (cx.D.m[0][0] + cx.D.m[1][1] + cx.D.m[2][2] + cx.D.m[3][3] - 2.0) * 0.5;
        if (nd < 1e-7 || !(cosA > -0.2 && cosA < 1.0001)) continue;  // (still: the lerp is exact; turned more than ~100 degrees: a cut)
        if (!Inverse(cx.D, cx.Dinv) || !LogM4(cx.D, cx.LogD)) continue;
        M4 back = ExpM4(cx.LogD, 1.0);
        if (NormInf(Comb(back, 1, cx.D, -1)) > 1e-7 * std::max(1.0, NormInf(cx.D))) continue;
        cx.path = true;
        St.camPath++;
    }
}
// Diagnostics (once per stats window): render-target textures a frame samples before it draws into them - content
// carried over from the previous tick (a copy of the last frame for heat haze or refraction, an exposure value, a
// reflection redrawn every other tick). In-between frames redraw such a target, so the next real frame would sample
// the in-between frame's version.
static std::unordered_map<void*, void*> g_surfTex;  // render-target surface -> its texture (nullptr: none)
static char g_carried[512];
static std::vector<IDirect3DTexture9*> g_carryTex;  // the carried targets (references held), at most 8
struct CarryBak {
    IDirect3DSurface9* bak = nullptr;  // the game frame's output (this tick)
    IDirect3DSurface9* in = nullptr;   // its input: the previous tick's output
    IDirect3DTexture9* of = nullptr;
    bool saved = false;
    uint32_t tick = 0, inTick = 0;     // (ticks the copies were made: the input is only good from the tick just before)
};
static CarryBak g_carryBak[8];
static void CarryRelease() {
    for (auto* x : g_carryTex) x->Release();
    g_carryTex.clear();
    for (auto& b : g_carryBak) { if (b.bak) b.bak->Release(); if (b.in) b.in->Release(); b = CarryBak(); }
    g_surfTex.clear();
}
// after the game frame, before the in-between frames: a copy of each carried target
static void CarrySave() {
    for (size_t i = 0; i < 8; i++) g_carryBak[i].saved = false;
    if (!C.carry_restore) return;
    for (size_t i = 0; i < g_carryTex.size() && i < 8; i++) {
        IDirect3DSurface9* src = nullptr;
        if (FAILED(g_carryTex[i]->GetSurfaceLevel(0, &src)) || !src) continue;
        D3DSURFACE_DESC d;
        src->GetDesc(&d);
        CarryBak& b = g_carryBak[i];
        for (IDirect3DSurface9** s : {&b.bak, &b.in}) {
            if (!*s) continue;
            D3DSURFACE_DESC e;
            (*s)->GetDesc(&e);
            if (b.of != g_carryTex[i] || e.Width != d.Width || e.Height != d.Height || e.Format != d.Format) { (*s)->Release(); *s = nullptr; b.tick = b.inTick = 0; }
        }
        // last tick's output is this tick's input (what the game frame sampled): kept for the in-between frames
        std::swap(b.bak, b.in);
        b.inTick = b.tick;
        if (!b.bak && FAILED(g_dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &b.bak, nullptr))) b.bak = nullptr;
        b.of = g_carryTex[i];
        b.saved = b.bak && SUCCEEDED(o_StretchRect(g_dev, src, nullptr, b.bak, nullptr, D3DTEXF_NONE));
        b.tick = b.saved ? g_tickNo : 0;
        src->Release();
    }
}
static void CarryInput() {  // before each in-between frame (carry_restore 2): what the game frame sampled
    if (C.carry_restore < 2) return;
    for (size_t i = 0; i < g_carryTex.size() && i < 8; i++) {
        CarryBak& b = g_carryBak[i];
        if (!b.in || !b.saved || b.of != g_carryTex[i] || !b.inTick || b.inTick + 1 != b.tick) continue;
        IDirect3DSurface9* dst = nullptr;
        if (SUCCEEDED(g_carryTex[i]->GetSurfaceLevel(0, &dst)) && dst) {
            if (SUCCEEDED(o_StretchRect(g_dev, b.in, nullptr, dst, nullptr, D3DTEXF_NONE))) St.carryInputs++;
            dst->Release();
        }
    }
}
static void CarryRestore() {  // after the in-between frames
    for (size_t i = 0; i < g_carryTex.size() && i < 8; i++) {
        CarryBak& b = g_carryBak[i];
        if (!b.saved || b.of != g_carryTex[i]) continue;
        IDirect3DSurface9* dst = nullptr;
        if (SUCCEEDED(g_carryTex[i]->GetSurfaceLevel(0, &dst)) && dst) {
            if (SUCCEEDED(o_StretchRect(g_dev, b.bak, nullptr, dst, nullptr, D3DTEXF_NONE))) St.carryRestores++;
            dst->Release();
        }
        b.saved = false;
    }
}
static void* TexOfSurface(void* s) {
    auto it = g_surfTex.find(s);
    if (it != g_surfTex.end()) return it->second;
    void* tx = nullptr;
    if (s && SUCCEEDED(((IDirect3DSurface9*)s)->GetContainer(__uuidof(IDirect3DBaseTexture9), &tx)) && tx) ((IUnknown*)tx)->Release();
    else tx = nullptr;
    if (g_surfTex.size() > 4096) g_surfTex.clear();
    g_surfTex[s] = tx;
    return tx;
}
static void FindCarriedTargets(const Frame& f, const Frame& prev) {
    auto drawn = [](const Frame& F, std::unordered_set<void*>& out) {
        void* rt[4] = {F.rt[0], F.rt[1], F.rt[2], F.rt[3]};
        for (uint32_t off = 0; off < F.buf.size();) {
            const Cmd* c = (const Cmd*)(F.buf.data() + off);
            if (!c->size) break;
            if (c->op == OP_RT) { auto* x = (const PPtr*)(c + 1); if (x->a < 4 && (x->a != 0 || x->p)) rt[x->a] = x->p; }
            else if (c->op == OP_DP || c->op == OP_DIP || c->op == OP_DPUP || c->op == OP_DIPUP || c->op == OP_CLEAR)
                for (void* s : rt) if (void* tx = s ? TexOfSurface(s) : nullptr) out.insert(tx);
            off += c->size;
        }
    };
    std::unordered_set<void*> before;
    drawn(prev, before);
    std::unordered_set<void*> now;
    void* rt[4] = {f.rt[0], f.rt[1], f.rt[2], f.rt[3]};
    void* tex[21];
    memcpy(tex, f.startTex, sizeof tex);
    void* ps = f.startPS;
    int n = 0, k = 0;
    uint32_t di = 0;
    std::unordered_set<void*> reported;
    n += snprintf(g_carried + n, sizeof g_carried - n, "%s", "");
    for (uint32_t off = 0; off < f.buf.size();) {
        const Cmd* c = (const Cmd*)(f.buf.data() + off);
        if (!c->size) break;
        switch (c->op) {
        case OP_RT: { auto* x = (const PPtr*)(c + 1); if (x->a < 4 && (x->a != 0 || x->p)) rt[x->a] = x->p; break; }
        case OP_TEX: { auto* x = (const PPtr*)(c + 1); int s = SamplerSlot(x->a); if (s >= 0) tex[s] = x->p; break; }
        case OP_PS: ps = ((const PPtr*)(c + 1))->p; break;
        case OP_CLEAR:
            for (void* s : rt) if (void* tx = s ? TexOfSurface(s) : nullptr) now.insert(tx);
            break;
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP: {
            uint16_t m = PsSamplers(ps);
            for (int s = 0; s < 16; s++) {
                if (!((m >> s) & 1) || !tex[s] || now.count(tex[s]) || !before.count(tex[s]) || reported.count(tex[s])) continue;
                if (((IDirect3DBaseTexture9*)tex[s])->GetType() != D3DRTYPE_TEXTURE) continue;  // (2D only: a cube map has other methods)
                reported.insert(tex[s]);
                D3DSURFACE_DESC d{};
                ((IDirect3DTexture9*)tex[s])->GetLevelDesc(0, &d);
                if (k++ < 6 && n < (int)sizeof g_carried - 96)
                    n += snprintf(g_carried + n, sizeof g_carried - n, " | %p %ux%u fmt %d, sampled at draw %u of %zu", tex[s], d.Width, d.Height,
                                  d.Format, di, f.draws.size());
            }
            for (void* s : rt) if (void* tx = s ? TexOfSurface(s) : nullptr) now.insert(tx);
            di++;
            break;
        }
        }
        off += c->size;
    }
    if (!k) snprintf(g_carried, sizeof g_carried, " none");
    // the set the restore works on: colour render targets only (StretchRect copies them)
    for (auto* x : g_carryTex) x->Release();
    g_carryTex.clear();
    for (void* tx : reported) {
        if (g_carryTex.size() >= 8) break;
        D3DSURFACE_DESC d{};
        if (FAILED(((IDirect3DTexture9*)tx)->GetLevelDesc(0, &d)) || !(d.Usage & D3DUSAGE_RENDERTARGET) || d.Pool != D3DPOOL_DEFAULT) continue;
        ((IDirect3DTexture9*)tx)->AddRef();
        g_carryTex.push_back((IDirect3DTexture9*)tx);
    }
}
static std::deque<CamCtx> g_pktCam;
static void CameraPathsAt(float t) {  // this replay's D^(1-t)
    for (auto& kv : g_cam)
        if (kv.second.path) kv.second.Ct = ExpM4(kv.second.LogD, 1.0 - t);
    for (CamCtx& cx : g_pktCam) cx.Ct = ExpM4(cx.LogD, 1.0 - t);
}
static double Dist(const M4& a, const M4& b) {
    double d = 0;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) d += std::fabs(a.m[r][c] - b.m[r][c]);
    return d;
}
// The origins' map: last frame's (x, y, w) of a world-fixed point from this frame's (x, y, w, 1). Exactly linear for any
// camera motion - w is the view depth and x, y its projection, so (x, y, w, 1) is the view position, scaled - and unlike
// the camera's 4x4 it leaves out z, which MHF maps per object (the reason the 4x4 candidates scatter), and it holds for
// camera-facing cards too (their anchor stays put while they turn). For pairing only: a matrix's own path needs the 4x4.
struct OriginPair { double n[4], p[3]; };  // (this frame's x, y, w, 1 - last frame's x, y, w)
// Least squares (in units of the mean depth): all pairs, then those within 5x the median residual, then those within
// 2e-3 (strict: the camera paths' test) or 3x the kept ones' median. Returns how many fit within 2e-3, -1: no fit.
static int FitOrigins(std::vector<OriginPair> v, bool strict, double A[3][4]) {
    if (v.size() < 6) return -1;  // (3 equations each for 12 unknowns: twice that at least)
    double S = 0;
    for (const OriginPair& a : v) S += a.n[2];
    S /= (double)v.size();
    if (!(S > 1e-6)) return -1;
    for (OriginPair& a : v)
        for (int k = 0; k < 3; k++) a.n[k] /= S, a.p[k] /= S;
    std::vector<char> in(v.size(), 1);
    std::vector<double> rs;
    double H[3][4] = {};
    auto resid = [&](const OriginPair& a) {
        double e = 0, m = 1e-9;
        for (int j = 0; j < 3; j++) {
            double q = H[j][0] * a.n[0] + H[j][1] * a.n[1] + H[j][2] * a.n[2] + H[j][3];
            e = std::max(e, std::fabs(q - a.p[j]));
            m = std::max(m, std::fabs(a.p[j]));
        }
        return e / m;
    };
    for (int it = 0; it < 3; it++) {
        M4 Nm{};
        double B[3][4] = {};
        int cnt = 0;
        for (size_t k = 0; k < v.size(); k++) {
            if (!in[k]) continue;
            cnt++;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) Nm.m[r][c] += v[k].n[r] * v[k].n[c];
                if (r < 3)
                    for (int c = 0; c < 4; c++) B[r][c] += v[k].p[r] * v[k].n[c];
            }
        }
        M4 Ni;
        if (cnt < 6 || !Inverse(Nm, Ni)) return -1;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 4; c++) H[r][c] = B[r][0] * Ni.m[0][c] + B[r][1] * Ni.m[1][c] + B[r][2] * Ni.m[2][c] + B[r][3] * Ni.m[3][c];
        if (it == 2) break;
        rs.clear();
        for (size_t k = 0; k < v.size(); k++)
            if (it == 0 || in[k]) rs.push_back(resid(v[k]));
        std::nth_element(rs.begin(), rs.begin() + rs.size() / 2, rs.end());
        double med = rs[rs.size() / 2];
        double lim = it == 0 ? std::max(2e-3, 5 * med) : strict ? 2e-3 : std::max(1e-7, 3 * med);
        for (size_t k = 0; k < v.size(); k++) in[k] = resid(v[k]) <= lim;
    }
    int good = 0;
    for (const OriginPair& a : v) good += resid(a) <= 2e-3;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) A[r][c] = H[r][c];
        A[r][3] = H[r][3] * S;
    }
    return good;
}
// A pass's origins' map from its camera candidates, when a clear majority of them sits still in the world.
static bool AnchorMap(CamCtx& cx) {
    std::vector<OriginPair> v;
    for (const CamCand& c : cx.cand) {
        if (!c.prev) continue;
        double wn = c.cur[15], wp = c.prev[15];
        if (!(wn > 1e-3) || !(wp > 1e-3) || !std::isfinite(c.cur[3] + c.cur[7] + c.prev[3] + c.prev[7])) continue;
        v.push_back({{c.cur[3], c.cur[7], wn, 1.0}, {c.prev[3], c.prev[7], wp}});
    }
    int good = FitOrigins(v, true, cx.A);
#ifdef PAIRBENCH
    if (g_debugJumps) printf("  origins' map: %zu origins, %d within 2e-3\n", v.size(), good);
#endif
    if (good < std::max(6, (int)v.size() / 2 + 1)) return false;
    cx.hasA = true;
    return true;
}static void SolveCameras() {
    for (auto& kv : g_cam) {
        CamCtx& cx = kv.second;
        // vote on an even sample spread over the whole frame (the first draws of a pass are often all the same
        // kind of object, e.g. foliage cards, which each turn to the camera and never agree on a camera motion)
        std::vector<M4> sm;
        size_t n = cx.cand.size(), step = std::max<size_t>(1, (n + 95) / 96);
        for (size_t k = 0; k < n; k += step) sm.push_back(CandMatrix(cx.cand[k]));
        std::vector<const M4*> s;
        for (const M4& m : sm) s.push_back(&m);
        // (the first with the most votes; counting stops where a candidate can no longer get more than the best so far)
        int best = -1, bestVotes = 0;
        const int ns = (int)s.size();
        for (int a = 0; a < ns && bestVotes < ns; a++) {
            double scale = 0;
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) scale += std::fabs(s[a]->m[r][c]);
            int votes = 0;
            for (int b = 0; b < ns && votes + (ns - b) > bestVotes; b++) if (Dist(*s[a], *s[b]) < 1e-3 * scale) votes++;
            if (votes > bestVotes) { bestVotes = votes; best = a; }
        }
        // real agreement only: static scenery dominates a normal pass; two foliage cards at the same spot agreeing by
        // coincidence must not define the camera. Without agreement nothing is estimated (new objects then just
        // appear at their real position).
        // at least two distinct objects must agree; a single object's own motion is not the camera's
        int need = std::max(s.size() >= 6 ? 3 : 2, (int)((s.size() + 3) / 4));
        bool voted = best >= 0 && bestVotes >= need;
        if (voted) { cx.has = true; cx.D = *s[best]; }
        // No agreement on the matrices themselves - in MHF each object's matrix maps depth its own way (z about w), so
        // Mp Mc^-1 of one object scatters in its depth columns although the camera moved one way. Vote on what a
        // candidate explains instead: the objects that sit exactly where it carries them (prev = D cur, as the camera
        // paths test it). (In a town pass 267 of 270 objects fit the camera's motion that no two candidates agreed on.)
        if (!voted && C.cam_fit && ns >= 6 && n >= 6) {
            std::vector<M4> mp, mc;
            for (size_t k = 0; k < n; k += step) {
                if (!cx.cand[k].prev) { mp.push_back(Ident4()); mc.push_back(Ident4()); continue; }  // (a still witness)
                mp.push_back(FromRegs(cx.cand[k].prev));
                mc.push_back(FromRegs(cx.cand[k].cur));
            }
            std::vector<double> tol(mp.size());
            for (size_t b = 0; b < mp.size(); b++) tol[b] = 2e-3 * std::max(1e-6, NormInf(mp[b]));
            auto fits = [&](const M4& D, int b) { return NormInf(Comb(Mul(D, mc[b]), 1, mp[b], -1)) <= tol[b]; };
            int fBest = -1, fVotes = 0;
            const int hyp = std::min(ns, 32), hstep = std::max(1, ns / hyp);  // (at most 32 hypotheses, spread out)
            for (int a = 0; a < ns && fVotes * 4 < ns * 3; a += hstep) {
                int votes = 0;
                for (int b = 0; b < ns && votes + (ns - b) > fVotes; b++) votes += fits(sm[a], b);
                if (votes > fVotes) { fVotes = votes; fBest = a; }
            }
            // a clear majority of distinct objects, not a group of props moving together
            if (fBest >= 0 && fVotes >= std::max(need, ns / 2 + 1)) { cx.has = true; cx.D = sm[fBest]; St.camFit++; }
        }
        // where the origins were, for the pairing (MatchGroup, when a group pairs poorly - the 4x4 estimate can be missing,
        // a pass of camera-facing trees outvoting its rocks, or wrong in a fast flick)
        if (C.group_cam && !cx.hasA && AnchorMap(cx)) St.camAnchor++;
    }
#ifdef PAIRBENCH
    if (g_anchorAll)  // (-anchorall: the origins' map where the 4x4 estimate worked too, to see how well it fits)
        for (auto& kv : g_cam) {
            CamCtx cx2 = kv.second;
            cx2.hasA = false;
            printf("  [ctx %016llx, 4x4 estimate %s]\n", (unsigned long long)kv.first, kv.second.has ? "yes" : "no");
            AnchorMap(cx2);
        }
#endif
}
// Give a positional 4x4 packet without a counterpart an estimated previous value.
PROF_NOINLINE static bool SynthesizePrev(const Frame& N, uint32_t drawIdx, uint32_t oc) {
    const Cmd* c = (const Cmd*)(N.buf.data() + oc);
    auto* q = (const PConst*)(c + 1);
    if (q->count != 4 || g_pktPaired[q->ord]) return g_pktPaired[q->ord] != 0;
    if (g_ablate & 1) return false;
    auto it = g_cam.find(CamKey(N.draws[drawIdx].rt, q->start));
    if (it == g_cam.end() || !it->second.has) return false;
    M4 cur = FromRegs((const float*)(q + 1));
    M4 est = Mul(it->second.D, cur);
    // Camera-facing cards (fog sheets, glows, foliage billboards) turn with the camera: their local x axis maps to
    // clip x only. Only their position is fixed in the world, so carry the origin back and keep the orientation
    // (a world-fixed estimate would turn a screen-wide fog sheet by the camera's motion, sweeping its edge across
    // the in-between frames).
    double sx = std::fabs(cur.m[0][0]);
    if (sx > 1e-6 && std::fabs(cur.m[0][2]) < 1e-3 * sx && std::fabs(cur.m[1][0]) < 1e-3 * sx && std::fabs(cur.m[3][0]) < 1e-3 * sx) {
        for (int r = 0; r < 4; r++) for (int k = 0; k < 3; k++) est.m[r][k] = cur.m[r][k];
        St.synthBillboard++;
    }
    float ef[16];
    for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) ef[r * 4 + k] = (float)est.m[r][k];
    if (CrossesScreen(ef, (const float*)(q + 1))) { St.crossing++; return false; }
    NoteSweep(drawIdx, 's', ef, (const float*)(q + 1));
    uint32_t si = (uint32_t)g_synth.size();
    for (int k = 0; k < 16; k++) g_synth.push_back(ef[k]);
    Mask256 m;
    for (int r = 0; r < 4; r++) m.set(r);
    uint32_t mi = (uint32_t)g_pairMask.size();
    g_pairMask.push_back(m);
    g_pairs.push_back({oc, NONE, mi, si});
    g_pairs.back().camKey = CamKey(N.draws[drawIdx].rt, q->start);
    g_pktPaired[q->ord] = 1;
    St.synth++;
    g_tkSynth++;
    return true;
}

// first vertex position of a UP draw
static const float* UpFirstPos(const Cmd* c) {
    if (c->op == OP_DPUP) {
        auto* x = (const PDPUP*)(c + 1);
        return x->posOff >= 0 && x->vbytes >= x->stride ? (const float*)((const uint8_t*)(x + 1) + x->posOff) : nullptr;
    }
    if (c->op == OP_DIPUP) {
        auto* x = (const PDIPUP*)(c + 1);
        if (x->posOff < 0 || x->ibytes < 2) return nullptr;
        const uint8_t* ib = (const uint8_t*)(x + 1);
        UINT v = x->fmt == D3DFMT_INDEX32 ? *(const uint32_t*)ib : *(const uint16_t*)ib;
        if ((v + 1) * x->stride > x->vbytes) return nullptr;
        return (const float*)(ib + ((x->ibytes + 3) & ~3u) + v * x->stride + x->posOff);
    }
    return nullptr;
}
// Distance between two instances of the same mesh: by the constant packet that positions it (chosen at record
// time using the draw's own vertex shader), or by the first vertex for CPU-built geometry.
static float SigDist(const Frame& N, const DrawRec& dn, const Frame& P, const DrawRec& dp) {
    const Cmd* cn = (const Cmd*)(N.buf.data() + dn.off);
    const Cmd* cp = (const Cmd*)(P.buf.data() + dp.off);
    if (cn->op == OP_DPUP || cn->op == OP_DIPUP) {
        const float* a = UpFirstPos(cn);
        const float* b = UpFirstPos(cp);
        if (!a || !b) return 1e30f;
        return std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
    }
    if (dn.sig == NONE || dp.sig == NONE) return 1e30f;
    auto* a = (const PConst*)((const Cmd*)(N.buf.data() + dn.sig) + 1);
    auto* b = (const PConst*)((const Cmd*)(P.buf.data() + dp.sig) + 1);
    if (a->start != b->start || a->count != b->count) return 1e30f;
    const VSInfo* vi = GetVSInfo(dn.vs);
    const float* x = (const float*)(a + 1);
    const float* y = (const float*)(b + 1);
    float d = 0;
    int used = 0;
    for (UINT r = 0; r < a->count && used < 8; r++) {
        if (vi && !vi->all && !vi->pos.test(a->start + r)) continue;
        used++;
        for (int k = 0; k < 4; k++) d += std::fabs(x[r * 4 + k] - y[r * 4 + k]);
    }
    return used ? d : 1e30f;
}

static bool g_cut;  // this tick looks like a camera cut / teleport: don't interpolate it
static uint32_t g_checkTick;
static std::vector<int> g_match;  // per draw of frame N: index of the matched draw in the previous frame, or -1
static std::vector<char> g_rejCur;  // per draw of frame N: a pairing was refused as implausible (stays unmatched)
// The first 4x4 positional upload a draw reads (its world-view-projection), or null.
static const PConst* FirstMat4(const Frame& F, const Owners& o, size_t d, UINT wantStart = 0xFFFF) {
    for (uint32_t k = 0; k < o.count[d]; k++) {
        const Owners::E& e = o.info[o.begin[d] + k];
        if (e.count == 4 && (wantStart == 0xFFFF || e.start == wantStart)) return (const PConst*)((const Cmd*)(F.buf.data() + o.flat[o.begin[d] + k]) + 1);
    }
    return nullptr;
}
static void CamCandidateFromOwners(const Frame& N, size_t i, const Frame& P, size_t j) {
    for (uint32_t k = 0; k < g_ownN.count[i]; k++) {
        if (g_ownN.info[g_ownN.begin[i] + k].count != 4) continue;
        auto* a = (const PConst*)((const Cmd*)(N.buf.data() + g_ownN.flat[g_ownN.begin[i] + k]) + 1);
        const PConst* b = FirstMat4(P, g_ownP, j, a->start);
        if (b && memcmp(a + 1, b + 1, 64) != 0) AddCameraCandidate(N.draws[i].rt, a->start, (const float*)(b + 1), (const float*)(a + 1));
    }
}
// Distance between instance i (frame N) and instance j (frame P) of the same mesh: compare the clip-space
// position of i's origin, carried back into the previous frame with the measured camera motion, against j's.
// Billboards (foliage cards turned to the camera) keep their world position, so this pairs them correctly even
// when the camera swings and the draw order is reshuffled. Falls back to SigDist without a 4x4 upload.
static float DupDist(const Frame& N, int i, const Frame& P, int j) {
    const PConst* a = FirstMat4(N, g_ownN, i);
    const PConst* b = a ? FirstMat4(P, g_ownP, j, a->start) : nullptr;
    if (!a || !b) return SigDist(N, N.draws[i], P, P.draws[j]);
    const float* va = (const float*)(a + 1);
    const float* vb = (const float*)(b + 1);
    double c[4] = {va[3], va[7], va[11], va[15]};  // M * (0,0,0,1)
    auto it = g_cam.find(CamKey(N.draws[i].rt, a->start));
    if (it != g_cam.end() && it->second.has) {
        double e[4];
        for (int r = 0; r < 4; r++) e[r] = it->second.D.m[r][0] * c[0] + it->second.D.m[r][1] * c[1] + it->second.D.m[r][2] * c[2] + it->second.D.m[r][3] * c[3];
        memcpy(c, e, sizeof c);
    }
    double p[4] = {vb[3], vb[7], vb[11], vb[15]};
    double d;
    if (std::fabs(c[3]) > 1e-6 && std::fabs(p[3]) > 1e-6) {
        double dx = c[0] / c[3] - p[0] / p[3], dy = c[1] / c[3] - p[1] / p[3], dz = (c[3] - p[3]) / std::max(std::fabs(p[3]), 1.0);
        d = dx * dx + dy * dy + dz * dz;
    } else {
        d = 0;
        for (int r = 0; r < 4; r++) d += std::fabs(c[r] - p[r]);
    }
    double o = 0, s = 1;  // small orientation term separates cards sharing an origin
    for (int k = 0; k < 16; k++) { o += std::fabs(va[k] - vb[k]); s += std::fabs(vb[k]); }
    return (float)(d + 1e-4 * o / s);
}
// Assign the instances of one repeated mesh: globally by ascending distance (draw-order distance breaks ties),
// or, for huge groups, nearest within a window around the same draw-order position.
// Bone-driven meshes (swaying trees, characters) are placed by a bone palette (3 registers per bone, world space)
// rather than a 4x4 matrix: their instance identity is the root bone's world position.
static const PConst* FirstBones(const Frame& F, const Owners& o, size_t d, UINT wantStart = 0xFFFF) {
    for (uint32_t k = 0; k < o.count[d]; k++) {
        const Owners::E& e = o.info[o.begin[d] + k];
        if (e.count >= 3 && e.count % 3 == 0 && (wantStart == 0xFFFF || e.start == wantStart))
            return (const PConst*)((const Cmd*)(F.buf.data() + o.flat[o.begin[d] + k]) + 1);
    }
    return nullptr;
}
struct Inst { const float* m; double c[4]; bool ok, bones; };
static void InstOf(const Frame& F, const Owners& o, int d, UINT slot, const M4* D, Inst& out, bool bones = false) {
    out.bones = bones;
    if (bones) {
        const PConst* q = FirstBones(F, o, d, slot);
        out.ok = q != nullptr;
        if (!q) return;
        out.m = (const float*)(q + 1);
        out.c[0] = out.m[3]; out.c[1] = out.m[7]; out.c[2] = out.m[11]; out.c[3] = 1;
        return;
    }
    const PConst* q = FirstMat4(F, o, d, slot);
    out.ok = q != nullptr;
    if (!q) return;
    out.m = (const float*)(q + 1);
    double c[4] = {out.m[3], out.m[7], out.m[11], out.m[15]};
    if (D) for (int r = 0; r < 4; r++) out.c[r] = D->m[r][0] * c[0] + D->m[r][1] * c[1] + D->m[r][2] * c[2] + D->m[r][3] * c[3];
    else memcpy(out.c, c, sizeof c);
}
static float InstDist(const Inst& a, const Inst& b) {
    if (a.bones) {  // world-space root bone positions: plain distance
        double dx = a.c[0] - b.c[0], dy = a.c[1] - b.c[1], dz = a.c[2] - b.c[2];
        return (float)std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    {  // a pair whose origins sit off-screen on opposite sides is never the same object
        const double L = 1.25;
        if (a.c[3] > 1e-4 && b.c[3] > 1e-4) {
            double ax = a.c[0] / a.c[3], ay = a.c[1] / a.c[3], bx = b.c[0] / b.c[3], by = b.c[1] / b.c[3];
            if ((ax > L && bx < -L) || (ax < -L && bx > L) || (ay > L && by < -L) || (ay < -L && by > L)) return 1e6f;
        }
    }
    double d;
    if (std::fabs(a.c[3]) > 1e-6 && std::fabs(b.c[3]) > 1e-6) {
        double dx = a.c[0] / a.c[3] - b.c[0] / b.c[3], dy = a.c[1] / a.c[3] - b.c[1] / b.c[3];
        double dz = (a.c[3] - b.c[3]) / std::max(std::fabs(b.c[3]), 1.0);
        d = dx * dx + dy * dy + dz * dz;
    } else {
        d = 0;
        for (int r = 0; r < 4; r++) d += std::fabs(a.c[r] - b.c[r]);
    }
    double o = 0, s = 1;
    for (int k = 0; k < 16; k++) { o += std::fabs(a.m[k] - b.m[k]); s += std::fabs(b.m[k]); }
    return (float)(d + 1e-4 * o / s);
}
// Greedy assignment by ascending cost, ties by lower row then lower column: the same result as sorting every
// (row, column) edge and taking each one whose ends are both still free, without sorting all rows x columns edges
// (that sort was half of BuildPairs in busy scenes). Each row keeps its candidates in a min-heap; a queue holds
// every unassigned row's cheapest candidate, and a candidate whose column was taken meanwhile is dropped when it
// comes up. fill(row, out) appends the row's candidates {cost, column}. Returns the summed cost of the accepted
// edges (each clamped to clampAt), added up in acceptance order like the sorted version.
struct GEdge { float d; int b; };
static struct GreedyScratch { std::vector<GEdge> edges; std::vector<int> beg, end; } g_greedy[kMaxThreads];
template <class Fill>
static double GreedyAssign(int na, int nb, Fill fill, std::vector<int>& a2b, std::vector<float>& ad, int& accepted, float clampAt = 3e38f, int tid = 0) {
    std::vector<GEdge>& g_gEdges = g_greedy[tid].edges;
    std::vector<int>& g_gBeg = g_greedy[tid].beg;
    std::vector<int>& g_gEnd = g_greedy[tid].end;
    auto rowLess = [](const GEdge& x, const GEdge& y) { return x.d > y.d || (x.d == y.d && x.b > y.b); };  // min-heap
    g_gEdges.clear();
    g_gBeg.resize(na);
    g_gEnd.resize(na);
    struct Q { float d; int a, b; };
    auto qLess = [](const Q& x, const Q& y) { return x.d > y.d || (x.d == y.d && (x.a > y.a || (x.a == y.a && x.b > y.b))); };
    std::vector<Q> q;
    q.reserve(na);
    for (int a = 0; a < na; a++) {
        g_gBeg[a] = (int)g_gEdges.size();
        fill(a, g_gEdges);
        g_gEnd[a] = (int)g_gEdges.size();
    }
    for (int a = 0; a < na; a++) {
        if (g_gEnd[a] == g_gBeg[a]) continue;
        std::make_heap(g_gEdges.begin() + g_gBeg[a], g_gEdges.begin() + g_gEnd[a], rowLess);
        const GEdge& top = g_gEdges[g_gBeg[a]];
        q.push_back({top.d, a, top.b});
    }
    std::make_heap(q.begin(), q.end(), qLess);
    a2b.assign(na, -1);
    ad.assign(na, 0.0f);
    std::vector<char> usedB(nb, 0);
    double total = 0;
    accepted = 0;
    while (!q.empty()) {
        std::pop_heap(q.begin(), q.end(), qLess);
        Q e = q.back();
        q.pop_back();
        if (!usedB[e.b]) {
            a2b[e.a] = e.b;
            ad[e.a] = e.d;
            usedB[e.b] = 1;
            total += std::min(e.d, clampAt);
            accepted++;
            continue;
        }
        // column taken: this row's next free candidate goes back in the queue
        auto beg = g_gEdges.begin() + g_gBeg[e.a];
        int& end = g_gEnd[e.a];
        do {
            std::pop_heap(beg, g_gEdges.begin() + end, rowLess);
            end--;
        } while (end > g_gBeg[e.a] && usedB[g_gEdges[g_gBeg[e.a]].b]);
        if (end > g_gBeg[e.a]) {
            const GEdge& top = g_gEdges[g_gBeg[e.a]];
            q.push_back({top.d, e.a, top.b});
            std::push_heap(q.begin(), q.end(), qLess);
        }
    }
    return total;
}

// The screen motion of the objects matched uniquely (pass 0's single instances): per camera context, each origin's
// place now (NDC, depth w) and its move to the previous frame. Where repeated meshes were is read off the nearest ones
// (field_match).
struct FieldS { uint64_t ctx; float x, y, w, dx, dy, rw; };  // (rw: previous depth / current)
static std::vector<FieldS> g_field;
// Instances of one repeated mesh carried back by the field: each origin moved by the median of the 5 samples of its
// context nearest to it (on screen and in depth). False when the context has fewer than 3 samples.
static std::vector<const FieldS*> g_fieldScratch[kMaxThreads];  // (per pairing thread; no thread_local, see Owners::refs)
static bool FieldInsts(uint64_t ctx, std::vector<Inst>& ci, int tid) {
    std::vector<const FieldS*>& s = g_fieldScratch[tid];
    s.clear();
    for (const FieldS& f : g_field) if (f.ctx == ctx) s.push_back(&f);
    if (s.size() < 3) return false;
    for (Inst& in : ci) {
        if (!in.ok || !(in.c[3] > 1e-3)) continue;
        float x = (float)(in.c[0] / in.c[3]), y = (float)(in.c[1] / in.c[3]), w = (float)in.c[3];
        std::pair<float, int> nb[5];
        int nn = 0;
        for (int k = 0; k < (int)s.size(); k++) {
            std::pair<float, int> e{std::fabs(s[k]->x - x) + std::fabs(s[k]->y - y) + 0.5f * std::fabs(std::log(s[k]->w / w)), k};
            if (nn == 5 && !(e < nb[4])) continue;
            int at = nn < 5 ? nn++ : 4;
            for (; at > 0 && e < nb[at - 1]; at--) nb[at] = nb[at - 1];
            nb[at] = e;
        }
        float dx[5], dy[5], rw[5];
        for (int k = 0; k < nn; k++) dx[k] = s[nb[k].second]->dx, dy[k] = s[nb[k].second]->dy, rw[k] = s[nb[k].second]->rw;
        std::nth_element(dx, dx + nn / 2, dx + nn);
        std::nth_element(dy, dy + nn / 2, dy + nn);
        std::nth_element(rw, rw + nn / 2, rw + nn);
        double pw = w * rw[nn / 2];
        in.c[0] = (x + dx[nn / 2]) * pw, in.c[1] = (y + dy[nn / 2]) * pw, in.c[3] = pw;
    }
    return true;
}
struct IdxSpan {  // (draw indices: a group's members in a flat list)
    const int* p = nullptr;
    size_t n = 0;
    size_t size() const { return n; }
    bool empty() const { return !n; }
    int operator[](size_t k) const { return p[k]; }
    const int* begin() const { return p; }
    const int* end() const { return p + n; }
};
// (returns: the pairing explains the group poorly - another try once more is known about the camera may do better)
static bool MatchGroup(const Frame& N, const Frame& P, const IdxSpan& cur, const IdxSpan& prev, int tid = 0) {
    size_t nc = cur.size(), np = prev.size();
    if (nc * np <= 120000) {
        // cache each instance's (camera-corrected) origin once
        const PConst* first = FirstMat4(N, g_ownN, cur[0]);
        bool bones = false;
        if (!first && (first = FirstBones(N, g_ownN, cur[0]))) bones = true;  // bone-driven mesh: world position
        // a 4x4 the same for every instance (a shared camera or light matrix) tells them apart by nothing: by their
        // bones then, if they have any (two identical NPCs were paired crosswise, each skeleton blending from the other's)
        if (first && !bones && nc > 1) {
            bool same = true;
            for (size_t x = 1; x < nc && same; x++) {
                const PConst* o = FirstMat4(N, g_ownN, cur[x], first->start);
                same = o && memcmp(o + 1, first + 1, 64) == 0;
            }
            if (same)
                if (const PConst* fb = FirstBones(N, g_ownN, cur[0])) first = fb, bones = true;
        }
        UINT slot = first ? first->start : 0xFFFF;
        const M4* D = nullptr;
        if (first && !bones) {
            auto it = g_cam.find(CamKey(N.draws[cur[0]].rt, slot));
            if (it != g_cam.end() && it->second.has) D = &it->second.D;
        }
#ifdef PAIRBENCH
        if (g_debugJumps && nc >= 6 && !D) printf("  group key %016llx (%zu here, %zu before): no camera estimate%s\n", (unsigned long long)N.draws[cur[0]].key, nc, np, bones ? " (bones)" : "");
#endif
        std::vector<Inst> pi(np);
        for (size_t y = 0; y < np; y++) InstOf(P, g_ownP, prev[y], slot, nullptr, pi[y], bones);
        // greedy assignment by ascending distance; returns the summed distance of what it assigned
        std::vector<float> dist, distAlt;
        bool positional = true;  // distances are clip-space positions (false: SigDist fallback, other units)
        auto build = [&](const M4* Dm, std::vector<Inst>& ci) {
            ci.resize(nc);
            for (size_t x = 0; x < nc; x++) InstOf(N, g_ownN, cur[x], slot, Dm, ci[x], bones);
        };
        auto assign = [&](const std::vector<Inst>& ci, std::vector<int>& a2b, std::vector<float>& ad) -> double {
            int accepted;
            return GreedyAssign((int)nc, (int)np, [&](int x, std::vector<GEdge>& out) {
                for (size_t y = 0; y < np; y++) {
                    if (g_prevUsed[prev[y]]) continue;
                    float rank = (float)std::abs((double)x / std::max<size_t>(nc, 1) - (double)y / std::max<size_t>(np, 1));
                    float d;
                    if (ci[x].ok && pi[y].ok) d = InstDist(ci[x], pi[y]);
                    else { d = SigDist(N, N.draws[cur[x]], P, P.draws[prev[y]]); positional = false; }
                    out.push_back({d + 1e-7f * rank, (int)y});
                }
            }, a2b, ad, accepted, 1e6f, tid);
        };
        // with the camera-motion prediction, and without it (in case this group belongs to another camera):
        // keep whichever explains the group better
        std::vector<int> best, alt;
        std::vector<Inst> ciD, ci0;
        bool byField = false;  // (paired by the field: every pair is near its prediction already)
        build(D, ciD);
        double cost = assign(ciD, best, dist);
        if (D) {
            build(nullptr, ci0);
            double c0 = assign(ci0, alt, distAlt);
#ifdef PAIRBENCH
            if (g_debugJumps && nc >= 6) printf("  group key %016llx (%zu here, %zu before): cost with the camera %.4g, as is %.4g -> %s\n", (unsigned long long)N.draws[cur[0]].key, nc, np, cost, c0, c0 < cost ? "as is" : "camera");
#endif
            if (c0 < cost) { best.swap(alt); dist.swap(distAlt); cost = c0; }
        } else if (C.field_match && !bones && first && positional && !g_field.empty()) {
            // no camera estimate: where the objects around each instance say it was - if that explains the group clearly
            // better, and every pair it makes is close to its prediction (about 3% of the screen; else the group keeps
            // nearest-position pairing)
            std::vector<Inst> ciF = ciD;
            if (FieldInsts(CamKey(N.draws[cur[0]].rt, slot), ciF, tid)) {
                double fc = assign(ciF, alt, distAlt);
                float worst = 0;
                for (size_t x = 0; x < nc; x++) if (alt[x] >= 0) worst = std::max(worst, distAlt[x]);
                if (fc < 0.75 * cost && worst <= 4e-3f) {
                    best.swap(alt);
                    dist.swap(distAlt);
                    cost = fc;
                    byField = true;
                    InterlockedIncrement64((volatile LONG64*)&St.fieldGroups);
                }
            }
        }
        // A poor pairing (more than about 1% of the screen per instance, squared): other predictions of where each
        // instance was, the one that explains the group best kept. In a fast flick the pass's own camera estimate can be
        // missing or wrong (MHF's per-object depth mapping fools its vote), and by nearest position a field of identical
        // distant trees pairs with its neighbours - blended between two trees, or refused and drawn a tick ahead: the
        // treeline flickered. Tried, in order:
        //  - the pass's origins' map (AnchorMap: exact for anything fixed in the world, z left out, camera-facing cards
        //    included);
        //  - another target's camera or origins' map for the same register: MHF draws every object into the main target
        //    and then into two depth targets in turn with the same matrices, and a depth pass of trees gets no estimate
        //    of its own (66 identical cards outvote the rest);
        //  - the camera motion most of the group's own pairs agree on (prev = D cur, the camera paths' test).
        // A pairing that is good already is left as it is (and costs nothing more).
        // (measured with each pair counted at most 0.01 - a tenth of the screen: instances that come into view or leave it
        // are paired with whatever is left, far off, whatever the prediction, and must not decide between predictions)
        auto robust = [&](const std::vector<int>& a2b, const std::vector<float>& ad) {
            double s = 0;
            for (size_t x = 0; x < nc; x++)
                if (a2b[x] >= 0) s += std::min(ad[x], 0.01f);
            return s;
        };
        // poor: the pairs that aren't far off (those are instances coming into view or leaving it) are off by more than
        // about 1% of the screen on average, or a quarter of them are far off
        auto poorly = [&](const std::vector<int>& a2b, const std::vector<float>& ad) {
            double s = 0;
            int n = 0, distant = 0;
            for (size_t x = 0; x < nc; x++) {
                if (a2b[x] < 0) continue;
                if (ad[x] >= 0.01f) { distant++; continue; }
                s += ad[x], n++;
            }
            return s > 1e-4 * std::max(n, 1) || distant * 4 > n + distant;
        };
        // explained: the pairs that aren't far off sit within about 0.3% of the screen of the prediction - scenery
        // standing still under the camera's motion (a group of characters walking by themselves never does)
        auto explained = [&](const std::vector<int>& a2b, const std::vector<float>& ad) {
            double s = 0;
            int n = 0;
            for (size_t x = 0; x < nc; x++)
                if (a2b[x] >= 0 && ad[x] < 0.01f) s += ad[x], n++;
            return n > 0 && s < 1e-5 * n;
        };
        float preMedian = 0;  // (a rescued pairing: the typical distance before it - see the limit below)
        bool rescued = false;
        if (C.group_cam && first && !bones && positional && !byField) {
            int na = 0;
            for (size_t x = 0; x < nc; x++) na += best[x] >= 0;
            double rc = robust(best, dist);
            if (na && poorly(best, dist)) {
                const std::vector<Inst>& asIs = D ? ci0 : ciD;  // (with an estimate, ci0 is built above)
                double bc = rc;
                std::vector<int> bb;
                std::vector<float> bd;
                int how = 0;  // (1 origins' map, 2 another target's camera, 3 its own pairs' camera, 4 last tick's camera)
                auto tryMap = [&](const double (*A)[4], int kind) {
                    std::vector<Inst> ci = asIs;
                    for (Inst& in : ci) {
                        if (!in.ok) continue;
                        double q[3];
                        for (int r = 0; r < 3; r++) q[r] = A[r][0] * in.c[0] + A[r][1] * in.c[1] + A[r][2] * in.c[3] + A[r][3];
                        in.c[0] = q[0], in.c[1] = q[1], in.c[3] = q[2];
                    }
                    assign(ci, alt, distAlt);
                    double c = robust(alt, distAlt);
                    if (c < bc) bc = c, bb.swap(alt), bd.swap(distAlt), how = kind;
                };
                auto tryCam = [&](const M4& Dm, int kind) {
                    std::vector<Inst> ci;
                    build(&Dm, ci);
                    assign(ci, alt, distAlt);
                    double c = robust(alt, distAlt);
                    if (c < bc) bc = c, bb.swap(alt), bd.swap(distAlt), how = kind;
                };
                uint64_t own = CamKey(N.draws[cur[0]].rt, slot);
                auto ito = g_cam.find(own);
                if (ito != g_cam.end() && ito->second.hasA) tryMap(ito->second.A, 1);
                for (const auto& kv : g_cam) {
                    if (kv.first == own || (kv.first & 0xFFFF) != slot) continue;
                    if (kv.second.has) tryCam(kv.second.D, 2);
                    if (kv.second.hasA) tryMap(kv.second.A, 2);
                }
                // last tick's camera, for this pass and the others (a flick turns about as fast from one tick to the next)
                if ((how ? poorly(bb, bd) : true) && g_camPrevSerial && g_camPrevSerial == P.serial)
                    for (const auto& kv : g_camPrev) {
                        if ((kv.first & 0xFFFF) != slot) continue;
                        if (kv.second.has) tryCam(kv.second.D, 4);
                        if (kv.second.hasA) tryMap(kv.second.A, 4);
                    }
                if ((how ? poorly(bb, bd) : true) && nc >= 4) {  // (still poor: the group's own pairs)
                    std::vector<int> xs;
                    for (size_t x = 0; x < nc; x++)
                        if (best[x] >= 0 && asIs[x].ok && pi[best[x]].ok) xs.push_back((int)x);
                    if (xs.size() >= 4) {
                        std::vector<M4> mn(xs.size()), mp(xs.size());
                        std::vector<double> tol(xs.size());
                        for (size_t k = 0; k < xs.size(); k++) {
                            mn[k] = FromRegs(asIs[xs[k]].m), mp[k] = FromRegs(pi[best[xs[k]]].m);
                            tol[k] = 2e-3 * std::max(1e-6, NormInf(mp[k]));
                        }
                        const int nx = (int)xs.size(), hstep = std::max(1, nx / 16);  // (at most 16 hypotheses, spread out)
                        M4 Dg{};
                        int votesBest = 0;
                        for (int h = 0; h < nx && votesBest * 4 < nx * 3; h += hstep) {
                            M4 inv;
                            if (!Inverse(mn[h], inv)) continue;
                            M4 Dh = Mul(mp[h], inv);
                            int votes = 0;
                            for (int k = 0; k < nx && votes + (nx - k) > votesBest; k++) votes += NormInf(Comb(Mul(Dh, mn[k]), 1, mp[k], -1)) <= tol[k];
                            if (votes > votesBest) votesBest = votes, Dg = Dh;
                        }
                        if (votesBest >= std::max(4, nx / 2 + 1)) tryCam(Dg, 3);  // (a clear majority: scenery standing still)
                    }
                }
                // Still poor: the group's own motion, found by pairing and fitting in turn. A tree is a cluster of a dozen
                // identical leaf cards; in a fast pan the cluster moves further than its cards are apart, so nearest
                // position pairs each card with its neighbour (the leaves shuffled in the in-between frames) - but the
                // cluster as a whole is paired right, so the origins' map fitted to those pairs carries the pan; with it
                // the cards pair right, and the map fitted to those is exact. (Kept only if it explains the group better.)
                if (how ? poorly(bb, bd) : true) {
                    std::vector<int> pairsNow = how ? bb : best;
                    double prevC = bc;
                    for (int iter = 0; iter < 4; iter++) {
                        std::vector<OriginPair> v;
                        for (size_t x = 0; x < nc; x++) {
                            int y = pairsNow[x];
                            if (y < 0 || !asIs[x].ok || !pi[y].ok || !(asIs[x].c[3] > 1e-3) || !(pi[y].c[3] > 1e-3)) continue;
                            v.push_back({{asIs[x].c[0], asIs[x].c[1], asIs[x].c[3], 1.0}, {pi[y].c[0], pi[y].c[1], pi[y].c[3]}});
                        }
                        double Af[3][4];
                        if (FitOrigins(v, false, Af) < 0) break;
                        std::vector<Inst> ci = asIs;
                        for (Inst& in : ci) {
                            if (!in.ok) continue;
                            double q[3];
                            for (int r = 0; r < 3; r++) q[r] = Af[r][0] * in.c[0] + Af[r][1] * in.c[1] + Af[r][2] * in.c[3] + Af[r][3];
                            in.c[0] = q[0], in.c[1] = q[1], in.c[3] = q[2];
                        }
                        assign(ci, alt, distAlt);
                        double c = robust(alt, distAlt);
                        if (c < bc) bc = c, bb = alt, bd = distAlt, how = 5;
                        if (!(c < 0.98 * prevC)) break;
                        prevC = c;
                        pairsNow = alt;
                    }
                }
#ifdef PAIRBENCH
                if (g_debugJumps && nc >= 6)
                    printf("  group key %016llx (%zu here): poor pairing %.4g -> %.4g (%s)\n", (unsigned long long)N.draws[cur[0]].key, nc, rc, bc,
                           how == 1 ? "its pass's origins' map" : how == 2 ? "another target's camera" : how == 3 ? "its own pairs' camera" : how == 4 ? "last tick's camera" : how == 5 ? "its own motion, paired and fitted in turn" : "nothing better");
#endif
                if (how && bc < 0.5 * rc && explained(bb, bd)) {  // (clearly better only: a group of characters walking by themselves fits a
                                             // camera-like motion a little better too, and must keep its own pairing)
                    std::vector<float> ds;
                    for (size_t x = 0; x < nc; x++) if (best[x] >= 0) ds.push_back(dist[x]);
                    std::nth_element(ds.begin(), ds.begin() + ds.size() / 2, ds.end());
                    preMedian = ds[ds.size() / 2];
                    rescued = true;
                    best.swap(bb);
                    dist.swap(bd);
                    cost = bc;
                    InterlockedIncrement64((volatile LONG64*)&St.groupsRescued);
                }
            }
        }
        // Reject implausible pairs: when instances enter/leave view (culling), the leftovers of both frames are
        // different objects; pairing them makes "ghost" cards fly across the scene. A true counterpart sits near its
        // predicted position, so anything far beyond the group's typical match distance is treated as new.
        float limit = 3e30f;
        if (positional && !byField) {
            std::vector<float> ds;
            for (size_t x = 0; x < nc; x++) if (best[x] >= 0) ds.push_back(dist[x]);
            if (!ds.empty()) {
                std::nth_element(ds.begin(), ds.begin() + ds.size() / 2, ds.end());
                // clip-space groups: squared NDC distance; bone groups: world distance (static trees ~0)
                limit = std::max(bones ? 2.0f : 4e-4f, 16.0f * ds[ds.size() / 2]);
                // (a rescued group fits its prediction closely; what moves by itself must not be refused for that)
                if (rescued) limit = std::max(limit, 4.0f * preMedian);
                if (bones) limit = std::min(limit, 150.0f);  // nothing bone-driven moves further in one tick
            }
        }
        for (size_t x = 0; x < nc; x++) {
            if (best[x] < 0) continue;
            if (dist[x] > limit) {
                InterlockedIncrement64((volatile LONG64*)&St.rejected);  // (groups run on several threads)
                InterlockedIncrement((volatile LONG*)&g_tkRejected);
                g_rejCur[cur[x]] = 1;
#ifdef PAIRBENCH
                if (g_debugJumps) printf("  reject (group limit): draw %u <- %d, distance %.4g, limit %.4g, %zu in group\n", cur[x], prev[best[x]], dist[x], limit, nc);
#endif
                continue;
            }
            g_match[cur[x]] = prev[best[x]];
            g_prevUsed[prev[best[x]]] = 1;
        }
        int nAssigned = 0;
        for (size_t x = 0; x < nc; x++) nAssigned += best[x] >= 0;
        return first && !bones && positional && !byField && nAssigned && poorly(best, dist);
    }
    for (size_t x = 0; x < nc; x++) {
        int lo = (int)std::max<long>(0, (long)x - 24), hi = (int)std::min<size_t>(np, x + 25);
        float best = 3e30f;
        int bj = -1;
        for (int y = lo; y < hi; y++) {
            if (g_prevUsed[prev[y]]) continue;
            float d = DupDist(N, cur[x], P, prev[y]) + 1e-7f * std::abs((int)x - y);
            if (d < best) { best = d; bj = prev[y]; }
        }
        if (bj >= 0) { g_match[cur[x]] = bj; g_prevUsed[bj] = 1; }
    }
    return false;
}

// Screen-space CPU-built quads (text glyphs, UI, sprites) carry no motion model of their own. Repeated instances
// of one glyph (the two l's of "Small") are told apart by where their neighbours went: every matched quad of a
// different kind nearby gives a displacement, their median predicts where each instance was in the previous
// frame, and the group is re-assigned by that prediction when it explains the group better than nearest-position
// matching. (A label moving further than one letter per tick otherwise swaps its identical letters.)
static bool ScreenPos(const Frame& F, int d, float& x, float& y) {
    const Cmd* c = (const Cmd*)(F.buf.data() + F.draws[d].off);
    bool rhw;
    if (c->op == OP_DPUP) rhw = ((const PDPUP*)(c + 1))->rhw;
    else if (c->op == OP_DIPUP) rhw = ((const PDIPUP*)(c + 1))->rhw;
    else return false;
    const float* p = rhw ? UpFirstPos(c) : nullptr;
    if (!p || !std::isfinite(p[0]) || !std::isfinite(p[1])) return false;
    x = p[0];
    y = p[1];
    return true;
}
// Median of v (the element nth_element puts in the middle). same: every value is equal (static UI: all quads
// at rest), so the answer is known without the partial sort.
static inline float MedianOf(std::vector<float>& v, bool same) {
    if (!same) std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}
// Screen points for "everything within r px" queries: sorted by 16-px row, then x. A query visits a superset of the
// square window (whole rows, x range widened by a pixel); callers apply the exact distance test. (Was a hash grid
// of 64-px cells scanned 3x3: about twice the visits, and the busiest part of BuildPairs with lots of text.)
struct ScreenIdx {
    struct E { float x; int row, k; };
    std::vector<E> e, tmp;
    std::vector<int> cnt;
    std::vector<int> rowId, rowBeg;  // distinct rows ascending, where each starts in e (plus the end)
    static int RowOf(float y) {  // floor(y / 16) of the clamped value (fits an int; no floorf call)
        float q = std::min(std::max(y, -1e8f), 1e8f) / 16.0f;
        int i = (int)q;
        return (float)i > q ? i - 1 : i;
    }
    template <class Pts> void Build(const Pts& pts) {
        e.clear();
        int r0 = INT_MAX, r1 = INT_MIN;
        for (int k = 0; k < (int)pts.size(); k++) {
            e.push_back({pts[k].x, RowOf(pts[k].y), k});
            r0 = std::min(r0, e.back().row), r1 = std::max(r1, e.back().row);
        }
        // by row (counting), then by x within each row; a sort for points spread over too many rows
        if (!e.empty() && (int64_t)r1 - r0 < 4096) {
            cnt.assign(r1 - r0 + 2, 0);
            for (const E& x : e) cnt[x.row - r0 + 1]++;
            for (size_t r = 1; r < cnt.size(); r++) cnt[r] += cnt[r - 1];
            tmp.resize(e.size());
            for (const E& x : e) tmp[cnt[x.row - r0]++] = x;
            e.swap(tmp);
            for (size_t i = 0, j; i < e.size(); i = j) {
                for (j = i + 1; j < e.size() && e[j].row == e[i].row; j++) {}
                if (j - i > 1) std::sort(e.begin() + i, e.begin() + j, [](const E& a, const E& b) { return a.x < b.x; });
            }
        } else {
            std::sort(e.begin(), e.end(), [](const E& a, const E& b) { return a.row < b.row || (a.row == b.row && a.x < b.x); });
        }
        rowId.clear();
        rowBeg.clear();
        for (int i = 0; i < (int)e.size(); i++)
            if (i == 0 || e[i].row != e[i - 1].row) { rowId.push_back(e[i].row); rowBeg.push_back(i); }
        rowBeg.push_back((int)e.size());
    }
    template <class F> void Query(float x, float y, float r, F f) const {
        int r0 = RowOf(y - r) - 1, r1 = RowOf(y + r) + 1;
        for (size_t ri = std::lower_bound(rowId.begin(), rowId.end(), r0) - rowId.begin(); ri < rowId.size() && rowId[ri] <= r1; ri++) {
            auto b = e.begin() + rowBeg[ri], en = e.begin() + rowBeg[ri + 1];
            for (auto it = std::lower_bound(b, en, x - r - 1.0f, [](const E& a, float v) { return a.x < v; }); it != en && it->x <= x + r + 1.0f; ++it)
                f(it->k);
        }
    }
};
static ScreenIdx g_scrIdx;

// Screen-space text is drawn one glyph quad at a time (MHF: a DrawPrimitiveUP each, every label twice - a dark shadow
// 2 px off, then the text). A string - consecutive quads on one line, of one height, with no other draw between - is
// matched as a whole: aligned with a string of the previous frame (it may have lost glyphs at the screen edge), the
// same glyphs in the same order, cut into stretches that moved together (two labels at different depths can merge
// into one string, and drift apart as the camera orbits), the counterpart of the same colour preferred (the text,
// not its shadow). Identical letters ("Gatekeeper": four e's) then cannot swap, however far the label moved in a fast
// camera turn.
static bool QuadBox(const Frame& F, uint32_t d, float* b, uint32_t* col = nullptr) {  // x0, y0, x1, y1 of a screen-space UP draw
    const Cmd* c = (const Cmd*)(F.buf.data() + F.draws[d].off);
    const uint8_t* v;
    UINT stride, bytes;
    int posOff;
    bool rhw;
    if (c->op == OP_DPUP) {
        auto* x = (const PDPUP*)(c + 1);
        v = (const uint8_t*)(x + 1), stride = x->stride, bytes = x->vbytes, posOff = x->posOff, rhw = x->rhw;
    } else if (c->op == OP_DIPUP) {
        auto* x = (const PDIPUP*)(c + 1);
        v = (const uint8_t*)(x + 1) + ((x->ibytes + 3) & ~3u), stride = x->stride, bytes = x->vbytes, posOff = x->posOff, rhw = x->rhw;
    } else {
        return false;
    }
    if (!rhw || posOff < 0 || !stride || bytes < stride) return false;
    if (col) *col = (UINT)posOff + 20 <= stride ? *(const uint32_t*)(v + posOff + 16) : 0;  // (XYZRHW | DIFFUSE: the colour)
    b[0] = b[1] = 3e38f;
    b[2] = b[3] = -3e38f;
    for (UINT k = 0; k + stride <= bytes; k += stride) {
        const float* p = (const float*)(v + k + posOff);
        if (!std::isfinite(p[0]) || !std::isfinite(p[1])) return false;
        b[0] = std::min(b[0], p[0]), b[1] = std::min(b[1], p[1]), b[2] = std::max(b[2], p[0]), b[3] = std::max(b[3], p[1]);
    }
    return true;
}
struct TextRun { uint32_t first, n; };
static void BuildRuns(const Frame& F, std::vector<TextRun>& runs, std::vector<float>& box, std::vector<uint32_t>& col) {
    runs.clear();
    box.assign(F.draws.size() * 4, 0.0f);
    col.assign(F.draws.size(), 0);
    TextRun cur{0, 0};
    const float* pb = nullptr;
    void* prt = nullptr;
    auto close = [&]() {
        if (cur.n >= 2) runs.push_back(cur);
        cur.n = 0;
        pb = nullptr;
    };
    for (uint32_t d = 0; d < F.draws.size(); d++) {
        float* b = &box[d * 4];
        if (!QuadBox(F, d, b, &col[d])) { close(); continue; }
        bool joins = false;
        if (pb && F.draws[d].rt == prt) {
            float h = b[3] - b[1], ph = pb[3] - pb[1];
            float H = std::max(std::max(h, ph), 1.0f);
            joins = std::max(h, ph) <= 2.0f * std::max(std::min(h, ph), 1.0f) && std::fabs(b[1] - pb[1]) <= 0.5f * H &&
                    std::max(b[0], pb[0]) - std::min(b[2], pb[2]) <= 1.5f * H;
        }
        if (!joins) {
            close();
            cur = {d, 0};
        }
        cur.n++;
        pb = b;
        prt = F.draws[d].rt;
    }
    close();
}
static inline uint64_t GlyphPair(uint64_t a, uint64_t b) { return a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull + (a << 6) + (a >> 2)); }
static inline bool SameColour(uint32_t a, uint32_t b) {  // (RGB; a fading label changes its alpha)
    if (a == b) return true;
    for (int s = 0; s < 24; s += 8)
        if (std::abs((int)((a >> s) & 255) - (int)((b >> s) & 255)) > 48) return false;
    return true;
}
// A frame's glyph keys: each screen quad that sampled a glyph atlas, known by the pixels of its cell (its texture
// coordinates' rectangle in the atlas version it sampled, less a one-texel border) - the same letter in another atlas
// slot is the same key. Made once per frame (it is the previous frame of the next tick's matching too).
static void TextKeys(const Frame& Fc) {
    Frame& F = const_cast<Frame&>(Fc);  // (a cache of the recording)
    if (F.ckeysDone) return;
    F.ckeysDone = true;
    F.ckeys.assign(F.draws.size(), 0);
    if (F.glyphs.empty()) return;
    static std::unordered_map<uint64_t, uint64_t> cache;  // (atlas version, cell) -> pixel hash
    if (cache.size() > 65536) cache.clear();
    for (const GlyphSrc& g : F.glyphs) {
        if (g.draw >= F.draws.size() || g.snap >= F.snaps.size()) continue;
        const Cmd* c = (const Cmd*)(F.buf.data() + F.draws[g.draw].off);
        if (c->op != OP_DPUP) continue;
        auto* x = (const PDPUP*)(c + 1);
        const uint8_t* v = (const uint8_t*)(x + 1);
        if (!x->stride || x->stride < (UINT)g.uvOff + 8) continue;
        float u0 = 3e38f, v0 = 3e38f, u1 = -3e38f, v1 = -3e38f;
        for (UINT k = 0; k + x->stride <= x->vbytes; k += x->stride) {
            const float* uv = (const float*)(v + k + g.uvOff);
            if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) { u0 = 3e38f; break; }
            u0 = std::min(u0, uv[0]), v0 = std::min(v0, uv[1]), u1 = std::max(u1, uv[0]), v1 = std::max(v1, uv[1]);
        }
        const AtlasSnap& s = *F.snaps[g.snap];
        if (u0 < -0.001f || v0 < -0.001f || u1 > 1.001f || v1 > 1.001f) continue;  // (not a cell of the atlas)
        int x0 = (int)std::floor(u0 * s.w + 0.5f), x1 = (int)std::floor(u1 * s.w + 0.5f);
        int y0 = (int)std::floor(v0 * s.h + 0.5f), y1 = (int)std::floor(v1 * s.h + 0.5f);
        x0 = std::max(0, x0), y0 = std::max(0, y0), x1 = std::min((int)s.w, x1), y1 = std::min((int)s.h, y1);
        if (x1 - x0 >= 4) x0++, x1--;  // (the cell's border can hold a neighbour's texels when slots are packed)
        if (y1 - y0 >= 4) y0++, y1--;
        if (x1 <= x0 || y1 <= y0 || (x1 - x0) * (y1 - y0) > 64 * 64) continue;  // (a glyph-sized cell only)
        uint64_t ck = Fnv(Fnv(s.serial, (uint64_t)x0 << 32 | (uint32_t)y0), (uint64_t)x1 << 32 | (uint32_t)y1);  // (a version's serial: addresses get reused)
        auto it = cache.find(ck);
        uint64_t h;
        if (it != cache.end() && it->second) {
            h = it->second;
        } else {
            h = 1469598103934665603ull ^ (uint64_t)(x1 - x0) << 20 ^ (uint64_t)(y1 - y0);
            for (int y = y0; y < y1; y++)
                for (int xx = x0; xx < x1; xx++) h = (h ^ s.px[(size_t)y * s.w + xx]) * 1099511628211ull;
            h |= 1;
            cache[ck] = h;
        }
        F.ckeys[g.draw] = Fnv(g.base, h) | 1;
        St.ckeyGlyphs++;
    }
}
struct TextPending { uint32_t first, n; float x, y; };
static std::vector<TextPending> g_textPending;  // guessed strings with no text around to move with (PlacePendingText)
static double g_textMs;  // (the last call's time: pairbench)

static double g_textLap[8];  // (time per stage, summed: pairbench)
static void MatchTextRunsImpl(const Frame& N, const Frame& P);
PROF_NOINLINE static void MatchTextRuns(const Frame& N, const Frame& P) {
    int64_t t0 = Qpc();
    MatchTextRunsImpl(N, P);
    g_textMs = Ms(Qpc() - t0);
}
static void MatchTextRunsImpl(const Frame& N, const Frame& P) {
    // (glyphs by their pixels when both frames have them, else by their atlas coordinates)
    const bool ck = C.text_ckeys && N.ckeysDone && P.ckeysDone && N.ckeys.size() == N.draws.size() && P.ckeys.size() == P.draws.size();
    auto GK = [&](const Frame& F, uint32_t i) { return ck && F.ckeys[i] ? F.ckeys[i] : F.draws[i].key; };
    g_runMatch.assign(N.draws.size(), 0);
    int64_t tq = Qpc();
    auto lap = [&](int k) { int64_t q = Qpc(); g_textLap[k] += Ms(q - tq); tq = q; };
    g_runSynth.assign(N.draws.size() * 2, 0.0f);
    g_textPending.clear();
    if (!C.text_runs || (g_ablate & 2)) return;
    // each frame's strings (boxes, colours, glyph keys): the previous frame's are last tick's of this one
    struct Runs { uint32_t serial = 0; bool ck = false; std::vector<TextRun> runs; std::vector<float> box; std::vector<uint32_t> col; std::vector<uint64_t> gk; };
    static Runs RN, RP;
    auto build = [&](const Frame& F, Runs& R) {
        BuildRuns(F, R.runs, R.box, R.col);
        R.gk.assign(F.draws.size(), 0);
        for (const TextRun& r : R.runs)
            for (uint32_t m = 0; m < r.n; m++) R.gk[r.first + m] = GK(F, r.first + m);
        R.serial = F.serial, R.ck = ck;
    };
    if (P.serial && RN.serial == P.serial && RN.ck == ck && RN.gk.size() == P.draws.size()) std::swap(RN, RP);
    else build(P, RP);
    build(N, RN);
    const std::vector<TextRun>&rn = RN.runs, &rp = RP.runs;
    const std::vector<float>&bn = RN.box, &bp = RP.box;
    const std::vector<uint32_t>&cn = RN.col, &cp = RP.col;
    const uint64_t *gn = RN.gk.data(), *gp = RP.gk.data();
    if (rn.empty() || rp.empty()) return;
    static std::vector<char> keyedRun;  // per string of this frame: most of its glyphs have pixel keys (text, not icons)
    keyedRun.assign(rn.size(), 0);
    if (ck)
        for (size_t ai = 0; ai < rn.size(); ai++) {
            uint32_t kn = 0;
            for (uint32_t m = 0; m < rn[ai].n; m++) kn += N.ckeys[rn[ai].first + m] != 0;
            keyedRun[ai] = 2 * kn > rn[ai].n;
        }
    lap(0);  // (runs)
    // where each pair of neighbouring glyphs (this glyph, the next in its string) is in the previous frame: (string,
    // position) and the glyph's place, by pair, then string and position
    struct IxE { uint64_t key; std::pair<int, int> e; float x, y; };
    static std::vector<IxE> ix;
    ix.clear();
    for (int ri = 0; ri < (int)rp.size(); ri++)
        for (uint32_t k = 0; k + 1 < rp[ri].n; k++) {
            uint32_t j = rp[ri].first + k;
            ix.push_back({GlyphPair(gp[j], gp[j + 1]), {ri, (int)k}, bp[j * 4], bp[j * 4 + 1]});
        }
    std::sort(ix.begin(), ix.end(), [](const IxE& a, const IxE& b) { return a.key != b.key ? a.key < b.key : a.e < b.e; });
    struct Occs {  // (one pair's places)
        const IxE* p;
        size_t n;
        size_t size() const { return n; }
        const std::pair<int, int>& operator[](size_t k) const { return p[k].e; }
        struct It {
            const IxE* p;
            const std::pair<int, int>& operator*() const { return p->e; }
            It& operator++() { ++p; return *this; }
            bool operator!=(const It& o) const { return p != o.p; }
        };
        It begin() const { return {p}; }
        It end() const { return {p + n}; }
    };
    auto occ = [&](uint64_t key) {
        const IxE* b = ix.data();
        const IxE* lo = std::lower_bound(b, b + ix.size(), key, [](const IxE& e, uint64_t k) { return e.key < k; });
        const IxE* hi = std::upper_bound(lo, b + ix.size(), key, [](uint64_t k, const IxE& e) { return k < e.key; });
        return Occs{lo, (size_t)(hi - lo)};
    };
    static std::vector<char> usedP, covered;
    usedP.assign(P.draws.size(), 0);
    covered.assign(N.draws.size(), 0);  // (glyphs of this frame already given to a piece)
    struct Cand { int run, off; };
    struct Seg { uint32_t m0, m1; };  // aligned positions [m0, m1) of a stretch that moved together
    // one alignment of string a with a string of the previous frame: the glyphs that match (and are not covered yet),
    // cut into stretches that moved together; its movement is the longest stretch's
    struct Fit { int run = -1, off = 0, matched = 0; double cost = 0; bool colour = true; float dx = 0, dy = 0; std::vector<Seg> segs; };
    std::vector<Cand> cands;
    auto fit = [&](const TextRun& a, const Cand& c, Fit& f) PROF_NOINLINE {
        const TextRun& b = rp[c.run];
        int m0 = std::max(0, -c.off), m1 = std::min((int)a.n, (int)b.n - c.off);
        f.run = c.run, f.off = c.off, f.matched = 0, f.cost = 0, f.colour = true, f.dx = f.dy = 0;
        f.segs.clear();
        int s0 = -1;
        float sdx = 0, sdy = 0;
        auto closeSeg = [&](int m) {
            if (s0 >= 0 && m - s0 >= 2) { f.segs.push_back({(uint32_t)s0, (uint32_t)m}); f.matched += m - s0; }
            s0 = -1;
        };
        for (int m = m0; m < m1; m++) {
            uint32_t i = a.first + m, j = b.first + m + c.off;
            const float* q = &bn[i * 4];
            const float* r = &bp[j * 4];
            bool ok = !covered[i] && gn[i] == gp[j] && !usedP[j] && std::fabs((q[2] - q[0]) - (r[2] - r[0])) <= 1.5f &&
                      std::fabs((q[3] - q[1]) - (r[3] - r[1])) <= 1.5f;
            float dx = q[0] - r[0], dy = q[1] - r[1];
            if (ok && s0 >= 0 && (std::fabs(dx - sdx) > 1.5f || std::fabs(dy - sdy) > 1.5f)) closeSeg(m);  // moved differently
            if (!ok) { closeSeg(m); continue; }
            if (s0 < 0) { s0 = m; sdx = dx; sdy = dy; }
            f.cost += std::fabs(dx) + std::fabs(dy);
            if (!SameColour(cn[i], cp[j])) f.colour = false;
        }
        closeSeg(m1);
        const Seg* lg = nullptr;
        for (const Seg& s : f.segs) if (!lg || s.m1 - s.m0 > lg->m1 - lg->m0) lg = &s;
        if (lg) {
            uint32_t i0 = a.first + lg->m0, j0 = b.first + lg->m0 + c.off;
            f.dx = bn[i0 * 4] - bp[j0 * 4], f.dy = bn[i0 * 4 + 1] - bp[j0 * 4 + 1];
        }
    };
    // better: the same colour (the text, not its shadow - unless the other has clearly more glyphs: the shadow, 2 px
    // further in, can keep a glyph more at the screen edge), then more glyphs, then less movement
    auto better = [](const Fit& x, const Fit& y) {
        if (x.colour != y.colour && std::abs(x.matched - y.matched) <= std::max(1, std::max(x.matched, y.matched) / 4)) return x.colour;
        if (x.matched != y.matched) return x.matched > y.matched;
        if (x.colour != y.colour) return x.colour;
        return x.cost < y.cost;
    };
    auto apart = [](const Fit& x, const Fit& y) { return std::fabs(x.dx - y.dx) + std::fabs(x.dy - y.dy) > 4.0f; };  // (not a shadow / outline copy)
    auto overlap = [](const Fit& x, const Fit& y) {  // glyphs of this frame both alignments match
        int o = 0;
        for (const Seg& s : x.segs)
            for (const Seg& u : y.segs) o += std::max(0, (int)std::min(s.m1, u.m1) - (int)std::max(s.m0, u.m0));
        return o;
    };
    static std::vector<Fit> fits;
    static std::vector<int> alt;  // (the alternatives of the best: as good, for the same glyphs, elsewhere)
    // a's best alignment among the glyphs not covered yet (-1: none), and the others as good for the same glyphs
    // somewhere else: other copies of the same text. Which one this string continues is then a guess - a crowd's
    // identical titles, swept by a camera turn further than they are apart, are nearest to each other's previous place.
    // candidate alignments, by votes: each glyph pair of the string (a sample of 24 on a long one) votes for where it
    // is in the previous frame; a pair found there very often (rows of identical tiles) votes only for its 16 nearest
    // occurrences. The 64 best supported (then the nearest) are fitted.
    static std::vector<uint64_t> votes;
    // (the places of a pair found very often, by x - made when first needed this tick)
    struct XS { float x, y; int k; };
    static std::vector<XS> xs;
    static std::vector<char> xsDone;
    xs.resize(ix.size());
    xsDone.assign(ix.size(), 0);
    auto evaluate = [&](const TextRun& a) PROF_NOINLINE -> int {
        cands.clear();
        votes.clear();
        if (C.debug_textcands == 1) {  // (the first-found search, for comparisons)
            for (uint32_t m = 0; m + 1 < a.n; m++) {
                if (covered[a.first + m] || covered[a.first + m + 1]) continue;
                const Occs es = occ(GlyphPair(gn[a.first + m], gn[a.first + m + 1]));
                for (const auto& e : es) {
                    Cand c{e.first, e.second - (int)m};
                    bool have = false;
                    for (const Cand& x : cands) have |= x.run == c.run && x.off == c.off;
                    if (!have && cands.size() < 64) cands.push_back(c);
                }
            }
        } else {
        uint32_t pairs = a.n > 1 ? a.n - 1 : 0, step = pairs > 24 ? (pairs + 23) / 24 : 1;
        for (uint32_t m = 0; m < pairs; m += step) {
            if (covered[a.first + m] || covered[a.first + m + 1]) continue;
            const Occs es = occ(GlyphPair(gn[a.first + m], gn[a.first + m + 1]));
            if (!es.n) continue;
            auto vote = [&](const std::pair<int, int>& e) {
                votes.push_back((uint64_t)(uint32_t)e.first << 32 | (uint32_t)(e.second - (int)m + 0x40000000));
            };
            if (es.size() <= 32) {
                for (const auto& e : es) vote(e);
                continue;
            }
            const float* q = &bn[(a.first + m) * 4];
            std::pair<float, int> near16[16];  // (the 16 nearest so far, nearest first: by distance, then index)
            int nn = 0;
            // outwards from q's x, the nearer side first; done where the x distance alone is beyond the 16th nearest
            const int n = (int)es.size();
            size_t g0 = es.p - ix.data();
            XS* xb = xs.data() + g0;
            if (!xsDone[g0]) {
                xsDone[g0] = 1;
                for (int k = 0; k < n; k++) xb[k] = {es.p[k].x, es.p[k].y, k};
                std::sort(xb, xb + n, [](const XS& u, const XS& v) { return u.x < v.x; });
            }
            int R = (int)(std::lower_bound(xb, xb + n, q[0], [](const XS& u, float v) { return u.x < v; }) - xb), L = R - 1;
            for (;;) {
                float dl = L >= 0 ? q[0] - xb[L].x : 3e38f, dr = R < n ? xb[R].x - q[0] : 3e38f;
                bool left = dl <= dr;
                float dx = left ? dl : dr;
                if (dx >= 3e38f || (nn == 16 && dx > near16[15].first)) break;
                const XS& o = xb[left ? L-- : R++];
                std::pair<float, int> e{std::fabs(q[0] - o.x) + std::fabs(q[1] - o.y), o.k};
                if (nn == 16 && !(e < near16[15])) continue;
                int at = nn < 16 ? nn++ : 15;
                for (; at > 0 && e < near16[at - 1]; at--) near16[at] = near16[at - 1];
                near16[at] = e;
            }
            for (int k = 0; k < 16; k++) vote(es[near16[k].second]);
        }
        std::sort(votes.begin(), votes.end());
        struct VC { int n; float d; Cand c; };
        static std::vector<VC> vc;
        vc.clear();
        for (size_t i = 0; i < votes.size();) {
            size_t j = i;
            while (j < votes.size() && votes[j] == votes[i]) j++;
            Cand c{(int)(votes[i] >> 32), (int)(uint32_t)votes[i] - 0x40000000};
            int m0 = std::max(0, -c.off);
            float d = 3e38f;
            if (m0 < (int)a.n && m0 + c.off < (int)rp[c.run].n) {
                const float* q = &bn[(a.first + m0) * 4];
                const float* r = &bp[(rp[c.run].first + m0 + c.off) * 4];
                d = std::fabs(q[0] - r[0]) + std::fabs(q[1] - r[1]);
            }
            vc.push_back({(int)(j - i), d, c});
            i = j;
        }
        if (vc.size() > 64)
            std::partial_sort(vc.begin(), vc.begin() + 64, vc.end(), [](const VC& x, const VC& y) {
                return x.n != y.n ? x.n > y.n : x.d != y.d ? x.d < y.d : x.c.run != y.c.run ? x.c.run < y.c.run : x.c.off < y.c.off;
            });
        for (size_t k = 0; k < vc.size() && k < 64; k++) cands.push_back(vc[k].c);
        }
        if (fits.size() < cands.size()) fits.resize(cands.size());
        int best = -1;
        for (size_t k = 0; k < cands.size(); k++) {
            fit(a, cands[k], fits[k]);
            if (fits[k].matched >= 2 && (best < 0 || better(fits[k], fits[best]))) best = (int)k;
        }
        alt.clear();
        if (best >= 0)
            for (size_t k = 0; k < cands.size(); k++)
                if ((int)k != best && fits[k].matched == fits[best].matched && fits[k].colour == fits[best].colour && apart(fits[k], fits[best]) &&
                    2 * overlap(fits[k], fits[best]) >= fits[best].matched && alt.size() < 8)
                    alt.push_back((int)k);
        return best;
    };
    lap(1);  // (index)
    // 1) each string's pieces: its best alignment, then the best for the glyphs left (two labels on one line can be one
    //    string here and two there), up to four
    struct Piece { uint32_t run; Fit f; char state; std::vector<Fit> alts; };  // state 1 to match, 2 a guess (left as it is: not moved, not split up)
    static std::vector<Piece> pieces;
    pieces.clear();
    for (size_t ai = 0; ai < rn.size(); ai++) {
        const TextRun& a = rn[ai];
        for (int k = 0; k < 4; k++) {
            int b = evaluate(a);
            if (b < 0) break;
            pieces.push_back({(uint32_t)ai, Fit(), 1, {}});
            Piece& p = pieces.back();
            std::swap(p.f, fits[b]);
            for (int x : alt) p.alts.push_back(fits[x]);
            for (const Seg& s : p.f.segs)
                for (uint32_t m = s.m0; m < s.m1; m++) covered[a.first + m] = 1;
        }
    }
    lap(2);  // (pieces)
    // a string whose pieces cover less than three quarters of it lost most of its glyphs' identity (an atlas
    // reshuffle: the few glyph keys left in common with the previous frame are other letters', anywhere): matched by
    // its layout instead (step 5), its pieces dropped
    static std::vector<int> cov;
    static std::vector<char> byLayout;
    // (of what both frames could show: a string clipped at the screen edge last tick had fewer glyphs - all of them
    // matching is full coverage)
    static std::vector<int> span;
    cov.assign(rn.size(), 0);
    span.assign(rn.size(), 0);
    byLayout.assign(rn.size(), 0);
    for (const Piece& p : pieces) cov[p.run] += p.f.matched, span[p.run] = std::max(span[p.run], (int)std::min(rn[p.run].n, rp[p.f.run].n));
    for (size_t ai = 0; ai < rn.size(); ai++) byLayout[ai] = cov[ai] > 0 && 4 * cov[ai] < 3 * std::max(span[ai], 2);
    for (Piece& p : pieces) if (byLayout[p.run]) p.state = 0;    auto mag = [](const Fit& f) { return std::fabs(f.dx) + std::fabs(f.dy); };
    auto posOf = [&](const Piece& p) { return &bn[(rn[p.run].first + p.f.segs[0].m0) * 4]; };
    lap(3);  // (coverage)
    // 2) pieces of this frame after the same glyphs of the previous one (a label coming into view, identical to one
    //    already there): the better one keeps them, as good ones are guesses
    static std::vector<int> claim;
    claim.assign(P.draws.size(), -1);
    static std::vector<std::pair<int, int>> contested;  // (two pieces as good, after the same glyphs: decided by the field below)
    contested.clear();
    for (size_t pi = 0; pi < pieces.size(); pi++) {
        const Fit& f = pieces[pi].f;
        if (pieces[pi].state == 0) continue;
        for (const Seg& s : f.segs)
            for (uint32_t m = s.m0; m < s.m1; m++) {
                uint32_t j = rp[f.run].first + m + f.off;
                int o = claim[j];
                if (o < 0 || o == (int)pi) { claim[j] = (int)pi; continue; }
                const Fit& g = pieces[o].f;
                if (!apart(f, g) || pieces[o].state == 0) continue;  // (the same place: harmless)
                bool distinct = f.matched != g.matched || f.colour != g.colour;
                if (C.debug_text > 0)
                    Log("text: claim conflict: piece at %.0f,%.0f (moved %.0f,%.0f, %d glyphs) and piece at %.0f,%.0f (moved %.0f,%.0f, %d glyphs) want the same glyph",
                        posOf(pieces[pi])[0], posOf(pieces[pi])[1], f.dx, f.dy, f.matched, posOf(pieces[o])[0], posOf(pieces[o])[1], g.dx, g.dy, g.matched);
                bool fs = mag(f) <= 1.5f, gs = mag(g) <= 1.5f;  // (one stayed put, the other jumped onto its glyphs: the one that stayed is itself)
                if (fs != gs) { if (fs) pieces[o].state = 2, claim[j] = (int)pi; else pieces[pi].state = 2; }
                else if (distinct && better(f, g)) { pieces[o].state = 2; claim[j] = (int)pi; }
                else if (distinct) pieces[pi].state = 2;
                else { pieces[pi].state = 2; pieces[o].state = 2; contested.push_back({(int)pi, o}); }
            }
    }
    lap(4);  // (claims)
    // 3) the motion of the screen's text: the moving pieces with one counterpart. A camera turn moves every label
    //    about alike (by depth; a running player's own label a little more); the HUD stays put and is left out.
    //    Predicted movement at a place: the field's, weighted by nearness (leaving out the piece asked about).
    // (a weighted median: robust to the odd wrong match - an atlas reshuffle makes a few)
    // (the median without a sort per call: the pieces that can be in the field are kept in order of their movement,
    // re-sorted when that set or their movements change. A run of equal movements is summed in any order: the half is
    // reached in the same run as in a sort's order - unless the sum ends within rounding of it; then the call sorts.)
    static std::vector<double> pw;          // per piece, this call: its weight (0: left out)
    static std::vector<char> pel;           // per piece, when the orders were made: could be in the field
    static std::vector<float> pdx, pdy;     // ... and its movement
    static std::vector<int> ordX, ordY;     // those pieces by dx, by dy
    static std::vector<std::pair<float, double>> tie;
    size_t ordFor = ~(size_t)0;
    // (for text - a string whose glyphs have pixel keys - the field is the other text's: a screen-fixed icon or a menu
    // highlight near it is not the motion of the labels around it; with no such text near, everything)
    auto predict = [&](size_t self, const float* at, float& px, float& py, bool still = false, int run = -1) {
        bool stale = ordFor != pieces.size();
        double sw = 0;
        int n = 0;
#ifdef PAIRBENCH
        if (g_textOld) run = -1;
#endif
        for (int pass = run >= 0 && keyedRun[run] ? 0 : 1; pass < 2 && !n; pass++) {
            pw.assign(pieces.size(), 0.0);
            sw = 0;
            for (size_t o = 0; o < pieces.size(); o++) {
                const Piece& q = pieces[o];
                bool el = q.state == 1 && q.alts.empty();
                if (!stale && (el != (pel[o] != 0) || (el && (q.f.dx != pdx[o] || q.f.dy != pdy[o])))) stale = true;
                if (o == self || !el || (!still && mag(q.f) <= 1.5f) || (pass == 0 && !keyedRun[q.run])) continue;
                const float* po = posOf(q);
                if (self < pieces.size() && std::fabs(po[0] - at[0]) <= 40.0f && std::fabs(po[1] - at[1]) <= 40.0f) continue;  // (its own shadow copy)
                double dd = std::fabs(po[0] - at[0]) + std::fabs(po[1] - at[1]) + 60.0, w = 1.0 / (dd * dd);  // (local: the HUD at the edges must not outvote the labels around)
                pw[o] = w;
                sw += w;
                n++;
            }
        }
        if (!n) return 0;
        if (stale) {
            ordFor = pieces.size();
            pel.assign(pieces.size(), 0);
            pdx.assign(pieces.size(), 0.0f);
            pdy.assign(pieces.size(), 0.0f);
            ordX.clear();
            for (size_t o = 0; o < pieces.size(); o++)
                if (pieces[o].state == 1 && pieces[o].alts.empty()) pel[o] = 1, pdx[o] = pieces[o].f.dx, pdy[o] = pieces[o].f.dy, ordX.push_back((int)o);
            ordY = ordX;
            std::sort(ordX.begin(), ordX.end(), [](int a, int b) { return pdx[a] < pdx[b] || (!(pdx[b] < pdx[a]) && a < b); });
            std::sort(ordY.begin(), ordY.end(), [](int a, int b) { return pdy[a] < pdy[b] || (!(pdy[b] < pdy[a]) && a < b); });
        }
        bool unsure = false;
        auto wmed = [&](const std::vector<int>& ord, const std::vector<float>& val) {
            double acc = 0, half = 0.5 * sw, tol = 1e-9 * sw;
            for (size_t k = 0; k < ord.size();) {
                float v = val[ord[k]], first = 0;
                bool any = false;
                for (; k < ord.size() && !(v < val[ord[k]]); k++)
                    if (pw[ord[k]] > 0) {
                        if (!any) first = val[ord[k]], any = true;
                        acc += pw[ord[k]];
                    }
                if (!any) continue;
                if (std::fabs(acc - half) <= tol) unsure = true;
                if (acc >= half) return first;
            }
            unsure = true;  // (not reached: the whole is more than half)
            return 0.0f;
        };
        px = wmed(ordX, pdx), py = wmed(ordY, pdy);
        if (unsure) {  // (as a sort of (movement, weight) has it)
            auto smed = [&](bool y) {
                tie.clear();
                for (size_t o = 0; o < pieces.size(); o++)
                    if (pw[o] > 0) tie.push_back({y ? pieces[o].f.dy : pieces[o].f.dx, pw[o]});
                std::sort(tie.begin(), tie.end());
                double acc = 0;
                for (auto& e : tie) if ((acc += e.second) >= 0.5 * sw) return e.first;
                return tie.back().first;
            };
            px = smed(false), py = smed(true);
        }
#ifdef PAIRBENCH
        if (g_checkPredict) {  // (the sorted median it replaces, for -checkpredict)
            std::vector<std::pair<float, double>> wx, wy;
            for (size_t o = 0; o < pieces.size(); o++)
                if (pw[o] > 0) wx.push_back({pieces[o].f.dx, pw[o]}), wy.push_back({pieces[o].f.dy, pw[o]});
            auto smed = [&](std::vector<std::pair<float, double>>& v) {
                std::sort(v.begin(), v.end());
                double acc = 0;
                for (auto& e : v) if ((acc += e.second) >= 0.5 * sw) return e.first;
                return v.back().first;
            };
            float ox = smed(wx), oy = smed(wy);
            g_predictCalls++;
            if (ox != px || oy != py || (int)wx.size() != n) g_predictDiff++;  // (numerically: of equal entries, a sort leaves +0 / -0 in either order)
        }
#endif
        return n;
    };
    // a piece with one counterpart that moved far from the others' movement (or more than a quarter of the screen with
    // nothing to compare) is a label come into view matched with another copy that left: a guess
    const float W = (float)std::max<UINT>(g_pp.BackBufferWidth, 640);
    static std::vector<char> demote;
    demote.assign(pieces.size(), 0);
    for (size_t pi = 0; pi < pieces.size(); pi++) {
        const Piece& p = pieces[pi];
        if (p.state != 1 || !p.alts.empty() || mag(p.f) <= 1.5f) continue;
        float px = 0, py = 0;
        int n = predict(pi, posOf(p), px, py, false, (int)p.run);
        float err = std::fabs(p.f.dx - px) + std::fabs(p.f.dy - py);
        if (n >= 2 ? err > std::max(64.0f, 0.5f * (std::fabs(px) + std::fabs(py))) : mag(p.f) > 0.25f * W) {
            // ...unless other text near it moved the same way: the median of a mixed neighbourhood (world labels
            // panning past the player's own name, which moves with the player) fits neither; a wrong copy fits nothing
            const float* at = posOf(p);
            float tol = std::max(64.0f, 0.5f * mag(p.f));
            int support = 0;
            for (size_t o = 0; o < pieces.size() && support < 2; o++) {
                const Piece& q = pieces[o];
                if (o == pi || q.state != 1 || !q.alts.empty() || demote[o]) continue;
                const float* po = posOf(q);
                float dist = std::fabs(po[0] - at[0]) + std::fabs(po[1] - at[1]);
                if (dist <= 40.0f || dist > 800.0f) continue;  // (its own shadow copy; far away)
                support += std::fabs(q.f.dx - p.f.dx) + std::fabs(q.f.dy - p.f.dy) <= tol;
            }
#ifdef PAIRBENCH
            if (g_textOld) support = 0;
#endif
            if (support >= (p.f.matched >= 4 ? 1 : 2)) continue;  // (a string of 4+ glyphs moving with another one: itself)
            demote[pi] = 1;
            if (C.debug_text > 0)
                Log("text: n %u at %.0f,%.0f moved %.0f,%.0f, the text around it %.0f,%.0f (%d pieces): not this copy - a guess", rn[p.run].n, posOf(p)[0], posOf(p)[1],
                    p.f.dx, p.f.dy, px, py, n);
        }
    }
    for (size_t pi = 0; pi < pieces.size(); pi++) if (demote[pi]) pieces[pi].state = 2;
    // two copies as good after the same glyphs of the previous frame (a label and its twin elsewhere, in a fast turn):
    // the one that moved like the text around it is the label, the other a guess - rather than both guesses. A piece
    // keeps its match only if it fits the field in every such conflict and its rival doesn't.
#ifdef PAIRBENCH
    if (g_textOld) contested.clear();
#endif
    if (!contested.empty()) {
        static std::vector<int8_t> fits;  // per piece: -1 not asked, 0 doesn't fit the moving text, 1 fits
        static std::vector<char> lost;
        fits.assign(pieces.size(), -1);
        lost.assign(pieces.size(), 0);
        auto fitOf = [&](int k) {
            if (fits[k] < 0) {
                float px = 0, py = 0;
                const Piece& p = pieces[k];
                int n = mag(p.f) > 1.5f ? predict((size_t)k, posOf(p), px, py, false, (int)p.run) : 0;
                float err = std::fabs(p.f.dx - px) + std::fabs(p.f.dy - py);
                fits[k] = n >= 2 && err <= std::max(32.0f, 0.25f * (std::fabs(px) + std::fabs(py)));
            }
            return fits[k] == 1;
        };
        for (auto& c : contested) {
            bool a = fitOf(c.first), b = fitOf(c.second);
            if (a == b) lost[c.first] = lost[c.second] = 1;
            else lost[a ? c.second : c.first] = 1;
        }
        for (auto& c : contested)
            for (int k : {c.first, c.second})
                if (!lost[k] && fits[k] == 1 && !demote[k]) pieces[k].state = 1;
    }
    // copies: the one whose movement fits the field's - clearly (much nearer to it than the next copy is), or a guess;
    // with no field to go by only text that stayed put
    for (size_t pi = 0; pi < pieces.size(); pi++) {
        Piece& p = pieces[pi];
        if (p.state != 1 || p.alts.empty()) continue;
        if (mag(p.f) <= 1.5f) continue;  // (text that stayed put is itself)
        float px = 0, py = 0;
        if (!predict(pieces.size(), posOf(p), px, py, true, (int)p.run)) px = py = 0;  // (the text around it, moving or not; nothing: it stayed put)
        const Fit* pick = &p.f;
        float e0 = std::fabs(p.f.dx - px) + std::fabs(p.f.dy - py);
        for (const Fit& x : p.alts) {
            float e = std::fabs(x.dx - px) + std::fabs(x.dy - py);
            if (e < e0) e0 = e, pick = &x;
        }
        float sep = 3e38f;  // (from the pick to the next copy)
        auto gap = [&](const Fit& x) { if (&x != pick) sep = std::min(sep, std::fabs(x.dx - pick->dx) + std::fabs(x.dy - pick->dy)); };
        gap(p.f);
        for (const Fit& x : p.alts) gap(x);
        if (C.debug_text > 0) {
            char alts[200] = "";
            int n = 0;
            for (const Fit& x : p.alts) if (n < 180) n += snprintf(alts + n, sizeof alts - n, " %.0f,%.0f", x.dx, x.dy);
            Log("text: copies: n %u at %.0f,%.0f: best moved %.0f,%.0f, others%s | field says %.0f,%.0f -> pick off by %.0f, next copy %.0f away", rn[p.run].n,
                posOf(p)[0], posOf(p)[1], p.f.dx, p.f.dy, alts, px, py, e0, sep);
        }
        // (copies far apart leave room: a pick tens of px off the field is still clearly not the copy across the screen)
        if (e0 <= std::max({16.0f, 0.25f * (std::fabs(px) + std::fabs(py)), 0.1f * sep}) && e0 < 0.25f * sep) {
            if (pick != &p.f) p.f = *pick;
        } else {
            p.state = 2;
        }
    }

    lap(5);  // (field+copies)
    // a string is matched whole or not at all: a guess anywhere in it makes all of it one (pieces of one label
    // moving differently is the label breaking up)
    static std::vector<char> guessed;
    guessed.assign(rn.size(), 0);
    for (const Piece& p : pieces) if (p.state == 2) guessed[p.run] = 1;
    for (Piece& p : pieces) if (guessed[p.run]) p.state = 2;
    // 4) the matches
    uint64_t guesses = 0;
    for (size_t ai = 0; ai < rn.size(); ai++) {
        if (!guessed[ai]) continue;
        // a guess moves whole with the screen's text around it (the field's prediction) - or, with no such text, with
        // the 3D objects around it once their motion is known (PlacePendingText); no glyph of it is paired on its own
        // either (that splits it into letters)
        const TextRun& a = rn[ai];
        float px = 0, py = 0;
        bool field = predict(pieces.size(), &bn[a.first * 4], px, py, true, (int)ai) > 0;  // (with the text that stayed put: one moving tab highlight is not the menu's motion)
        for (uint32_t m = 0; m < a.n; m++) {
            uint32_t i = a.first + m;
            g_rejCur[i] = 1;
            g_runMatch[i] = field ? 2 : 3;
            if (field) g_runSynth[i * 2] = -px, g_runSynth[i * 2 + 1] = -py;
        }
        if (!field) g_textPending.push_back({a.first, a.n, 0.5f * (bn[a.first * 4] + bn[a.first * 4 + 2]), 0.5f * (bn[a.first * 4 + 1] + bn[a.first * 4 + 3])});
        guesses++;
        if (C.debug_text > 0)
            Log("text: n %u at %.0f,%.0f col %08x - a guess, %s %.0f,%.0f", a.n, bn[a.first * 4], bn[a.first * 4 + 1], cn[a.first],
                field ? "moved with the text field" : "to move with the objects around it", px, py);
    }
    for (const Piece& p : pieces) {
        const TextRun& a = rn[p.run];
        const TextRun& b = rp[p.f.run];
        if (p.state != 1) continue;
        int n = 0;
        for (const Seg& s : p.f.segs) {
            for (uint32_t m = s.m0; m < s.m1; m++) {
                uint32_t i = a.first + m, j = b.first + m + p.f.off;
                if (usedP[j]) continue;
                g_match[i] = (int)j;
                g_prevUsed[j] = 1;
                usedP[j] = 1;
                g_runMatch[i] = 1;
                n++;
            }
            St.textRuns++;
        }
        St.textRunQuads += n;
        if (!n)  // (its counterpart's glyphs went to another piece: left as it is)
            for (const Seg& s : p.f.segs)
                for (uint32_t m = s.m0; m < s.m1; m++) g_rejCur[a.first + m] = 1;
        if (C.debug_text > 0)
            Log("text: n %u at %.0f,%.0f col %08x moved %.0f,%.0f, %d glyphs", a.n, bn[(a.first + p.f.segs[0].m0) * 4], bn[(a.first + p.f.segs[0].m0) * 4 + 1],
                cn[a.first + p.f.segs[0].m0], p.f.dx, p.f.dy, n);
    }
    // the strings' glyphs without a counterpart (not drawn in the previous frame: they came in at the screen edge) move
    // with the nearest stretch of their string that was matched, instead of standing at their new place while the
    // rest slides in - unless part of the string is a guess
    for (size_t ai = 0; ai < rn.size(); ai++) {
        const TextRun& a = rn[ai];
        bool any = false, guess = false;
        for (uint32_t m = 0; m < a.n; m++) any |= g_runMatch[a.first + m] == 1, guess |= covered[a.first + m] && !g_runMatch[a.first + m];
        if (!any || guess || guessed[ai] || byLayout[ai]) continue;
        for (uint32_t m = 0; m < a.n; m++) {
            uint32_t i = a.first + m;
            if (g_runMatch[i]) continue;
            int nd = 1 << 30;
            uint32_t nearest = 0;
            for (uint32_t k = 0; k < a.n; k++)
                if (g_runMatch[a.first + k] == 1 && std::abs((int)k - (int)m) < nd) nd = std::abs((int)k - (int)m), nearest = a.first + k;
            if (nd == 1 << 30) continue;
            g_runSynth[i * 2] = bp[g_match[nearest] * 4] - bn[nearest * 4];
            g_runSynth[i * 2 + 1] = bp[g_match[nearest] * 4 + 1] - bn[nearest * 4 + 1];
            g_runMatch[i] = 2;
            g_rejCur[i] = 1;  // (not to be paired with some other glyph of its kind)
            St.textRunMoved++;
        }
    }
    St.textGuesses += guesses;
    lap(6);  // (apply)
    // 5) strings whose glyphs all changed identity: MHF draws text from a glyph atlas it rewrites every frame, and when
    //    new text needs room the letters move to other slots - the same "e" has other texcoords next tick, and another
    //    letter has its old ones. Such a string has no counterpart by glyphs; by layout it does: the string of the
    //    previous frame with as many glyphs, of the same height, colour and spacing, where the text around it says it
    //    went (or, with no text moving, where it was). Matched glyph by glyph in order. Never letter by letter on their
    //    own keys (those now belong to other letters: the label flies apart); with no clear counterpart it stays as it
    //    is for the tick.
    // (two passes: first only what stayed about put; those matched then join the text's movement for the rest)
    static std::vector<char> laid;
    laid.assign(rn.size(), 0);
    for (int pass = 0; pass < 2; pass++) for (size_t ai = 0; ai < rn.size(); ai++) {
        const TextRun& a = rn[ai];
        if (laid[ai]) continue;
        bool any = false;
        for (uint32_t m = 0; m < a.n; m++) any |= covered[a.first + m] || g_runMatch[a.first + m] != 0;
        if (any && !byLayout[ai]) continue;
        const float* q0 = &bn[a.first * 4];
        float px = 0, py = 0;
        bool field = predict(pieces.size(), q0, px, py, true, (int)ai) > 0;
        // candidates: the strings of P laid out alike - as many glyphs, or fewer / more where one of the two is cut off
        // at the screen edge (a label coming into view: P had only its end, or only its start)
        const float SW = (float)std::max<UINT>(g_pp.BackBufferWidth, 640);
        auto edgeL = [](const float* g) { return g[0] <= 2.0f * (g[2] - g[0]) + 2.0f; };
        auto edgeR = [&](const float* g) { return g[2] >= SW - 2.0f * (g[2] - g[0]) - 2.0f; };
        int best = -1, near0 = -1, bestOff = 0, nearOff = 0;
        float e0 = 3e38f, e1 = 3e38f;  // (the best and second-best distance from the text's movement)
        float w0 = 3e38f, w1 = 3e38f;  // (... from where it was)
        for (int bi = 0; bi < (int)rp.size(); bi++) {
            const TextRun& b = rp[bi];
            int offs[2], no = 0;  // (N glyph m <-> P glyph m + off)
            if (b.n == a.n) offs[no++] = 0;
            else if (b.n < a.n) {  // P cut off: its start (left edge: N's end) or its end (right edge: N's start)
                if (edgeL(&bp[b.first * 4])) offs[no++] = -(int)(a.n - b.n);
                if (edgeR(&bp[(b.first + b.n - 1) * 4])) offs[no++] = 0;
            } else {  // this one cut off
                if (edgeL(&bn[a.first * 4])) offs[no++] = (int)(b.n - a.n);
                if (edgeR(&bn[(a.first + a.n - 1) * 4])) offs[no++] = 0;
            }
            for (int oi = 0; oi < no; oi++) {
                int off = offs[oi], m0 = std::max(0, -off), m1 = std::min((int)a.n, (int)b.n - off);
                if (m1 - m0 < 2) continue;
                const float* g0 = &bn[(a.first + m0) * 4];
                const float* r0 = &bp[(b.first + m0 + off) * 4];
                if (usedP[b.first + m0 + off] || std::fabs((g0[3] - g0[1]) - (r0[3] - r0[1])) > 1.5f || !SameColour(cn[a.first + m0], cp[b.first + m0 + off])) continue;
                bool same = true;
                for (int m = m0 + 1; m < m1 && same; m++) {
                    const float* q = &bn[(a.first + m) * 4];
                    const float* r = &bp[(b.first + m + off) * 4];
                    same = !usedP[b.first + m + off] && std::fabs((q[0] - g0[0]) - (r[0] - r0[0])) <= 1.5f && std::fabs((q[1] - g0[1]) - (r[1] - r0[1])) <= 1.5f &&
                           std::fabs((q[2] - q[0]) - (r[2] - r[0])) <= 1.5f;
                }
                if (!same) continue;
                float e = std::fabs(g0[0] - r0[0] - px) + std::fabs(g0[1] - r0[1] - py);
                if (e < e0) e1 = e0, e0 = e, best = bi, bestOff = off;
                else if (e < e1) e1 = e;
                float r = std::fabs(g0[0] - r0[0]) + std::fabs(g0[1] - r0[1]);
                if (r < w0) w1 = w0, w0 = r, near0 = bi, nearOff = off;
                else if (r < w1) w1 = r;
            }
        }
        // where it was (text that stayed about put, nothing else like it near), or else where the text around it went
        bool ok = false;
        if (near0 >= 0 && w0 <= 8.0f && w1 >= 4.0f * w0 + 16.0f) ok = true, best = near0, bestOff = nearOff, e0 = w0, e1 = w1, field = false;
        else if (pass == 0) continue;  // (the second pass, with the first's matches in the field)
        else if (field && best >= 0)  // (a layout found once on the screen leaves more room)
            ok = e1 >= 3e38f ? e0 <= std::max(24.0f, 0.5f * (std::fabs(px) + std::fabs(py)))
                             : e0 <= std::max(8.0f, 0.25f * (std::fabs(px) + std::fabs(py))) && e1 >= 4.0f * e0 + 8.0f;
        if (C.debug_text > 0)
            Log("text: n %u at %.0f,%.0f col %08x - no counterpart by glyphs; by layout: %s (off the %s by %.0f, the next by %.0f; glyph offset %d)", a.n, q0[0], q0[1],
                cn[a.first], ok ? "matched" : best >= 0 ? "not clear, left as it is" : "none, left as it is", field ? "text's movement" : "place it was",
                e0 < 3e38f ? e0 : -1.0f, e1 < 3e38f ? e1 : -1.0f, bestOff);
        if (!ok) {
            bool moving = field && std::fabs(px) + std::fabs(py) > 1.5f;
            for (uint32_t m = 0; m < a.n; m++) {
                g_rejCur[a.first + m] = 1;
                if (moving) g_runMatch[a.first + m] = 2, g_runSynth[(a.first + m) * 2] = -px, g_runSynth[(a.first + m) * 2 + 1] = -py;
            }
            St.textNoLayout++;
            continue;
        }
        const TextRun& b = rp[best];
        int m0 = std::max(0, -bestOff), m1 = std::min((int)a.n, (int)b.n - bestOff);
        float sdx = bp[(b.first + m0 + bestOff) * 4] - bn[(a.first + m0) * 4], sdy = bp[(b.first + m0 + bestOff) * 4 + 1] - bn[(a.first + m0) * 4 + 1];
        for (int m = 0; m < (int)a.n; m++) {
            uint32_t i = a.first + m;
            if (m >= m0 && m < m1) {
                g_match[i] = (int)(b.first + m + bestOff);
                g_prevUsed[b.first + m + bestOff] = 1;
                usedP[b.first + m + bestOff] = 1;
                g_runMatch[i] = 1;
            } else {  // (came into view: moves with the rest)
                g_runSynth[i * 2] = sdx;
                g_runSynth[i * 2 + 1] = sdy;
                g_runMatch[i] = 2;
                g_rejCur[i] = 1;
                St.textRunMoved++;
            }
        }
        St.textLayout++;
        St.textRunQuads += m1 - m0;
        laid[ai] = 1;
        // (it joins the field: a piece with one counterpart)
        Fit lf;
        lf.run = best, lf.off = bestOff, lf.matched = m1 - m0, lf.dx = -sdx, lf.dy = -sdy;
        lf.segs.push_back({(uint32_t)m0, (uint32_t)m1});
        pieces.push_back({(uint32_t)ai, lf, 1, {}});
    }
    lap(7);  // (layout)
    if (C.debug_text > 0 && --C.debug_text == 0) Log("text: (end of the text log)");
}
template <class F>
static void ParallelFor(int count, F& f);  // (the worker pool, below)
PROF_NOINLINE static void RefineScreenGroups(const Frame& N, const Frame& P) {
    g_screenSynth.clear();
    if (g_ablate & 2) return;
    const float CELL = 64.0f;
    struct Pt { float x, y, dx, dy; uint64_t key; };
    std::vector<Pt> pts;
    ScreenIdx& grid = g_scrIdx;
    // each frame's screen positions (ScreenPos of every draw): the previous frame's are last tick's of this one
    struct ScrPos { uint32_t serial = 0; std::vector<float> x, y; std::vector<char> ok; };
    static ScrPos SN, SP;
    auto fill = [](const Frame& F, ScrPos& S) {
        S.x.assign(F.draws.size(), 0.0f);
        S.y.assign(F.draws.size(), 0.0f);
        S.ok.assign(F.draws.size(), 0);
        for (size_t i = 0; i < F.draws.size(); i++) S.ok[i] = ScreenPos(F, (int)i, S.x[i], S.y[i]);
        S.serial = F.serial;
    };
    if (P.serial && SN.serial == P.serial && SN.ok.size() == P.draws.size()) std::swap(SN, SP);
    else fill(P, SP);
    fill(N, SN);
    const std::vector<float>&cx = SN.x, &cy = SN.y;
    const std::vector<char>& scr = SN.ok;
    auto prevPos = [&](int j, float& px, float& py) {
        if (!SP.ok[j]) return false;
        px = SP.x[j], py = SP.y[j];
        return true;
    };
    // the screen-space draws by key: the map numbers the keys (and gives the order the groups are refined in), the
    // members are in flat lists in draw order - this frame's, and the previous frame's of the same keys
    std::unordered_map<uint64_t, uint32_t> groups;
    static std::vector<uint32_t> gN, gP, begN, begP, at;
    static std::vector<int> memN, memP;
    gN.assign(N.draws.size(), NONE);
    gP.assign(P.draws.size(), NONE);
    begN.clear();
    for (size_t i = 0; i < N.draws.size(); i++) {
        if (!scr[i]) continue;
        if (!g_runMatch[i]) {  // (a matched string stays as it is)
            auto in = groups.try_emplace(N.draws[i].key, (uint32_t)begN.size());
            if (in.second) begN.push_back(0);
            gN[i] = in.first->second;
            begN[gN[i]]++;
        }
        float px, py;
        if (g_match[i] >= 0 && prevPos(g_match[i], px, py)) pts.push_back({cx[i], cy[i], px - cx[i], py - cy[i], N.draws[i].key});
    }
    grid.Build(pts);
    size_t ng = begN.size();
    begP.assign(ng, 0);
    for (size_t j = 0; j < P.draws.size(); j++) {
        if (!SP.ok[j]) continue;
        auto it = groups.find(P.draws[j].key);
        if (it != groups.end()) gP[j] = it->second, begP[it->second]++;
    }
    auto lay = [&](std::vector<uint32_t>& beg, const std::vector<uint32_t>& g, std::vector<int>& mem) {  // (counts -> starts, then fill)
        uint32_t s = 0;
        for (size_t k = 0; k < ng; k++) { uint32_t c = beg[k]; beg[k] = s; s += c; }
        beg.push_back(s);
        mem.resize(s);
        at.assign(beg.begin(), beg.end() - 1);
        for (size_t d = 0; d < g.size(); d++) if (g[d] != NONE) mem[at[g[d]]++] = (int)d;
    };
    lay(begN, gN, memN);
    lay(begP, gP, memP);
    auto curOf = [&](uint32_t k) { return IdxSpan{memN.data() + begN[k], begN[k + 1] - begN[k]}; };
    auto prevOf = [&](uint32_t k) { return IdxSpan{memP.data() + begP[k], begP[k + 1] - begP[k]}; };
    static std::vector<char> held;  // per draw of P: matched by the group being refined
    held.assign(P.draws.size(), 0);
    static std::vector<int> posOf;  // per draw of P: its index in the group's candidates (-1: not one)
    posOf.assign(P.draws.size(), -1);
    Phase(12);
    std::vector<float> mx, my;
    bool sameX = true, sameY = true;  // all gathered values equal (see MedianOf)
    auto take = [&](float dx, float dy) {
        if (!mx.empty()) { sameX &= dx == mx[0]; sameY &= dy == my[0]; }
        mx.push_back(dx);
        my.push_back(dy);
    };
    // The predicted motion of every instance (the median displacement of the matched quads of other kinds around
    // it) reads only the snapshot above: computed for all groups first, on all threads. The re-assignment below
    // then runs group after group as before (groups can hand each other's previous instances on).
    struct GroupMed { IdxSpan cur; uint32_t g; uint64_t key; size_t first; bool all; };
    static std::vector<GroupMed> todo;
    static std::vector<float> medX, medY;
    todo.clear();
    size_t nInst = 0;
    for (auto& kv : groups) {
        IdxSpan members = curOf(kv.second);
        if (members.size() < 2) continue;
        bool skip = false;
        for (int i : members) skip |= g_rejCur[i] != 0;
        if (skip) continue;
        todo.push_back({members, kv.second, kv.first, nInst, true});
        nInst += members.size();
    }
    medX.resize(nInst);
    medY.resize(nInst);
    {
        struct Scratch { std::vector<float> x, y; };
        static Scratch scratch[kMaxThreads];
        auto medians = [&](int g, int tid) {
            GroupMed& gm = todo[g];
            std::vector<float>& sx = scratch[tid].x;
            std::vector<float>& sy = scratch[tid].y;
            for (size_t a = 0; a < gm.cur.size(); a++) {
                int i = gm.cur[a];
                sx.clear();
                sy.clear();
                bool sX = true, sY = true;
                grid.Query(cx[i], cy[i], CELL, [&](int k) {
                    const Pt& p = pts[k];
                    if (p.key == gm.key) return;
                    if (std::fabs(p.x - cx[i]) > CELL || std::fabs(p.y - cy[i]) > CELL) return;
                    if (!sx.empty()) { sX &= p.dx == sx[0]; sY &= p.dy == sy[0]; }
                    sx.push_back(p.dx);
                    sy.push_back(p.dy);
                });
                if (sx.size() < 3) { gm.all = false; break; }
                medX[gm.first + a] = MedianOf(sx, sX);
                medY[gm.first + a] = MedianOf(sy, sY);
            }
        };
        ParallelFor((int)todo.size(), medians);
    }
    for (GroupMed& gm : todo) {
        const IdxSpan& cur = gm.cur;
        // candidates: previous instances of this glyph that are free or held by this group
        std::vector<int> prev;
        for (int i : cur) if (g_match[i] >= 0) held[g_match[i]] = 1;
        for (int j : prevOf(gm.g)) if (held[j] || !g_prevUsed[j]) prev.push_back(j);
        for (int i : cur) if (g_match[i] >= 0) held[g_match[i]] = 0;
        if (prev.size() < 2) continue;
        // predicted motion of each instance (computed above)
        if (!gm.all) continue;
        const float* mdx = &medX[gm.first];
        const float* mdy = &medY[gm.first];
        // cost of an assignment under the prediction; greedy re-assignment by ascending cost
        std::vector<float> ppx(prev.size()), ppy(prev.size());
        for (size_t b = 0; b < prev.size(); b++) prevPos(prev[b], ppx[b], ppy[b]);
        auto cost = [&](size_t a, size_t b) {
            return std::fabs(ppx[b] - (cx[cur[a]] + mdx[a])) + std::fabs(ppy[b] - (cy[cur[a]] + mdy[a]));
        };
        double oldCost = 0;
        int oldN = 0;
        for (size_t b = 0; b < prev.size(); b++) posOf[prev[b]] = (int)b;  // (prev: distinct draws)
        for (size_t a = 0; a < cur.size(); a++)
            if (g_match[cur[a]] >= 0 && posOf[g_match[cur[a]]] >= 0) { oldCost += cost(a, (size_t)posOf[g_match[cur[a]]]); oldN++; }
        for (size_t b = 0; b < prev.size(); b++) posOf[prev[b]] = -1;
        // the re-assignment is taken only when cheaper by 0.5: never when the current one costs less than that
        // (costs are >= 0) - static text, mostly; not even computed then
        if (oldCost < 0.5) continue;
        std::vector<int> a2b;
        std::vector<float> a2d;
        int newN = 0;
        double newCost = GreedyAssign((int)cur.size(), (int)prev.size(), [&](int a, std::vector<GEdge>& out) {
            for (size_t b = 0; b < prev.size(); b++) out.push_back({(float)cost(a, b), (int)b});
        }, a2b, a2d, newN);
        if (newN < oldN || newCost >= oldCost - 0.5) continue;
        bool changed = false;
        for (size_t a = 0; a < cur.size(); a++) {
            int i = cur[a];
            if (g_match[i] >= 0) g_prevUsed[g_match[i]] = 0;
        }
        for (size_t a = 0; a < cur.size(); a++) {
            int i = cur[a], j = a2b[a] >= 0 ? prev[a2b[a]] : -1;
            changed |= g_match[i] != j;
            g_match[i] = j;
            if (j >= 0) g_prevUsed[j] = 1;
        }
        if (changed) St.screenRefined++;
    }
    Phase(10);
    // Consistency: whatever paired it, a screen-space quad must move like the quads around it. A letter of a menu
    // whose text just changed (13 -> 14) otherwise takes an identical letter from elsewhere and flies across the
    // in-between frames; static UI then has neighbours that agree on "no motion", a moving label on its motion.
    // Disagreeing pairs become unmatched (drawn where they are in the new frame).
    pts.clear();
    std::vector<int> ptDraw;
    for (size_t i = 0; i < N.draws.size(); i++) {
        float px, py;
        if (!scr[i] || g_match[i] < 0 || !prevPos(g_match[i], px, py)) continue;
        pts.push_back({cx[i], cy[i], px - cx[i], py - cy[i], N.draws[i].key});
        ptDraw.push_back((int)i);
    }
    grid.Build(pts);
    std::vector<int> drop;
    {
        static std::vector<char> bad;
        bad.assign(pts.size(), 0);
        struct Scratch { std::vector<float> x, y; };
        static Scratch scratch[kMaxThreads];
        const int kChunk = 64;
        auto consist = [&](int ch, int tid) {
            std::vector<float>& sx = scratch[tid].x;
            std::vector<float>& sy = scratch[tid].y;
            for (size_t k = (size_t)ch * kChunk; k < pts.size() && k < (size_t)(ch + 1) * kChunk; k++) {
                if (g_runMatch[ptDraw[k]]) continue;  // a matched string moved rigidly as a whole: kept
                const Pt& me = pts[k];
                sx.clear();
                sy.clear();
                bool sX = true, sY = true;
                grid.Query(me.x, me.y, CELL, [&](int j) {
                    if ((size_t)j == k) return;
                    const Pt& p = pts[j];
                    if (std::fabs(p.x - me.x) > CELL || std::fabs(p.y - me.y) > CELL) return;
                    if (!sx.empty()) { sX &= p.dx == sx[0]; sY &= p.dy == sy[0]; }
                    sx.push_back(p.dx);
                    sy.push_back(p.dy);
                });
                if (sx.size() < 4) continue;
                double mdx = MedianOf(sx, sX), mdy = MedianOf(sy, sY);
                double dev = std::fabs(me.dx - mdx) + std::fabs(me.dy - mdy);
                if (dev > 6.0 + 0.25 * (std::fabs(mdx) + std::fabs(mdy))) bad[k] = 1;
            }
        };
        ParallelFor((int)((pts.size() + kChunk - 1) / kChunk), consist);
        for (size_t k = 0; k < pts.size(); k++)
            if (bad[k]) drop.push_back(ptDraw[k]);
    }
    for (int i : drop) {
        g_prevUsed[g_match[i]] = 0;
        g_match[i] = -1;
        g_rejCur[i] = 1;
        St.screenDropped++;
    }
    Phase(11);
    // Quads left without a counterpart (dropped above, or new: text that just changed) would sit at their next-tick
    // position in every in-between frame, almost a tick ahead of their label at the first ones. They move with the
    // consistent quads around them instead (static menu: no motion; a label: its motion).
    std::vector<char> isDrop(N.draws.size(), 0);
    for (int i : drop) isDrop[i] = 1;
    pts.clear();
    for (size_t i = 0; i < N.draws.size(); i++) {
        float px, py;
        if (!scr[i] || g_match[i] < 0 || isDrop[i] || !prevPos(g_match[i], px, py)) continue;
        pts.push_back({cx[i], cy[i], px - cx[i], py - cy[i], N.draws[i].key});
    }
    grid.Build(pts);
    for (size_t i = 0; i < N.draws.size(); i++) {
        if (!scr[i] || g_match[i] >= 0) continue;
        if (g_runMatch[i] == 2) {  // moves with its string
            if (g_runSynth[i * 2] != 0.0f || g_runSynth[i * 2 + 1] != 0.0f)  // (moved by nothing: drawn as it is, no copy needed)
                g_screenSynth.push_back({(uint32_t)i, g_runSynth[i * 2], g_runSynth[i * 2 + 1]});
            continue;
        }
        if (g_runMatch[i] == 3) continue;  // (a guessed string: PlacePendingText)
        mx.clear();
        my.clear();
        sameX = sameY = true;
        grid.Query(cx[i], cy[i], CELL, [&](int j) {
            const Pt& p = pts[j];
            if (std::fabs(p.x - cx[i]) > CELL || std::fabs(p.y - cy[i]) > CELL) return;
            take(p.dx, p.dy);
        });
        if (mx.size() < 4) continue;
        float sx = MedianOf(mx, sameX), sy = MedianOf(my, sameY);
        if (sx != 0.0f || sy != 0.0f) g_screenSynth.push_back({(uint32_t)i, sx, sy});
    }
}
// Guessed strings with no unique text around them move with the 3D objects around them on the screen (labels hang
// over characters and signs; the nearest objects are theirs): each matched draw's own 4x4 placement gives its
// origin's screen movement from the last tick to this one; the median of the nearest few is the string's. With no
// object near, it stays as it is (the game's own 30 Hz).
PROF_NOINLINE static void PlacePendingText(const Frame& N, const Frame& P) {
    if (g_textPending.empty()) return;
    const float W = (float)std::max<UINT>(g_pp.BackBufferWidth, 640), H = (float)std::max<UINT>(g_pp.BackBufferHeight, 480);
    struct S { float x, y, dx, dy; };
    static std::vector<S> smp;
    smp.clear();
    for (uint32_t i = 0; i < N.draws.size() && i < g_ownN.count.size(); i++) {
        int j = g_match[i];
        if (j < 0 || (size_t)j >= g_ownP.count.size()) continue;
        for (uint32_t k = 0; k < g_ownN.count[i]; k++) {
            if (g_ownN.info[g_ownN.begin[i] + k].count != 4) continue;
            auto* q = (const PConst*)((const Cmd*)(N.buf.data() + g_ownN.flat[g_ownN.begin[i] + k]) + 1);
            const PConst* r = FirstMat4(P, g_ownP, (size_t)j, q->start);
            if (!r) continue;
            const float* a = (const float*)(q + 1);
            const float* b = (const float*)(r + 1);
            float wa = a[15], wb = b[15];  // (origin: the 4th column; a world or view matrix has w 1 and far-off x, y)
            if (!(wa > 1e-3f) || !(wb > 1e-3f)) continue;
            float xa = a[3] / wa, ya = a[7] / wa, xb = b[3] / wb, yb = b[7] / wb;
            if (!(std::fabs(xa) <= 1.2f && std::fabs(ya) <= 1.2f && std::fabs(xb) <= 2.0f && std::fabs(yb) <= 2.0f)) continue;
            smp.push_back({(xa + 1) * 0.5f * W, (1 - ya) * 0.5f * H, (xa - xb) * 0.5f * W, (yb - ya) * 0.5f * H});
        }
    }
    const float R = 0.2f * W;
    if (C.debug_text > 0) {
        Log("text: %zu objects with a screen movement, %zu guessed strings to place", smp.size(), g_textPending.size());
        for (size_t k = 0; k < smp.size() && k < 40; k++) Log("text:   object at %.0f,%.0f moved %.0f,%.0f", smp[k].x, smp[k].y, smp[k].dx, smp[k].dy);
    }
    std::vector<std::pair<float, int>> near6;
    std::vector<float> mx, my;
    for (const TextPending& tp : g_textPending) {
        near6.clear();
        for (int k = 0; k < (int)smp.size(); k++) {
            float d = std::fabs(smp[k].x - tp.x) + std::fabs(smp[k].y - tp.y);
            if (d > R) continue;
            near6.push_back({d, k});
        }
        bool ok = !near6.empty();
        float sx = 0, sy = 0;
        if (ok) {
            // the objects nearest the string (the one it hangs over, and its parts), not the scenery further off
            size_t K = std::min<size_t>(near6.size(), 6);
            std::partial_sort(near6.begin(), near6.begin() + K, near6.end());
            while (K > 1 && near6[K - 1].first > 1.5f * near6[0].first + 20.0f) K--;
            mx.clear();
            my.clear();
            for (size_t k = 0; k < K; k++) mx.push_back(smp[near6[k].second].dx), my.push_back(smp[near6[k].second].dy);
            std::nth_element(mx.begin(), mx.begin() + K / 2, mx.end());
            std::nth_element(my.begin(), my.begin() + K / 2, my.end());
            sx = -mx[K / 2], sy = -my[K / 2];  // (the synthetic previous place: this one less the movement)
            // the objects near must agree (scenery at other depths, or a character and the wall behind it, don't)
            auto [x0, x1] = std::minmax_element(mx.begin(), mx.end());
            auto [y0, y1] = std::minmax_element(my.begin(), my.end());
            ok = std::fabs(sx) + std::fabs(sy) <= 0.5f * W && (*x1 - *x0) + (*y1 - *y0) <= std::max(16.0f, 0.3f * (std::fabs(sx) + std::fabs(sy)));
        }
        if (C.debug_text > 0)
            Log("text: guessed string at %.0f,%.0f: %s %.0f,%.0f (%zu objects near)", tp.x, tp.y, ok ? "moved with the objects" : "left as it is", -sx, -sy,
                near6.size());
        if (!ok) { St.textLeft++; continue; }
        St.textGeo++;
        if (sx != 0.0f || sy != 0.0f)
            for (uint32_t m = 0; m < tp.n; m++) g_screenSynth.push_back({tp.first + m, sx, sy});
    }
}
// Synthetic previous commands for the unpaired screen quads: the same quad, shifted by its neighbours' displacement.
PROF_NOINLINE static void BuildScreenSynth(const Frame& N) {
    g_upSynth.clear();
    size_t need = 0;
    for (const ScreenSynth& s : g_screenSynth) need += ((const Cmd*)(N.buf.data() + N.draws[s.draw].off))->size;
    g_upSynth.reserve(need);  // no reallocation below: pairs keep offsets into it
    for (const ScreenSynth& s : g_screenSynth) {
        uint32_t off = N.draws[s.draw].off;
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        uint8_t* v = nullptr;
        UINT stride = 0, bytes = 0;
        int posOff = -1;
        uint32_t at = (uint32_t)g_upSynth.size();
        g_upSynth.insert(g_upSynth.end(), (const uint8_t*)c, (const uint8_t*)c + c->size);
        Cmd* copy = (Cmd*)(g_upSynth.data() + at);
        if (c->op == OP_DPUP) {
            auto* x = (PDPUP*)(copy + 1);
            v = (uint8_t*)(x + 1);
            stride = x->stride, bytes = x->vbytes, posOff = x->posOff;
        } else if (c->op == OP_DIPUP) {
            auto* x = (PDIPUP*)(copy + 1);
            v = (uint8_t*)(x + 1) + ((x->ibytes + 3) & ~3u);
            stride = x->stride, bytes = x->vbytes, posOff = x->posOff;
        }
        if (!v || posOff < 0 || !stride) { g_upSynth.resize(at); continue; }
        for (UINT k = 0; k + stride <= bytes; k += stride) {
            float* p = (float*)(v + k + posOff);
            p[0] += s.dx;
            p[1] += s.dy;
        }
        Pair pr{off, NONE, NONE, NONE};
        pr.upSynth = at;
        g_pairs.push_back(pr);
        St.screenSynth++;
    }
}

// Final check of every match against the camera motion measured over the whole scene. A static object must sit
// exactly where the camera motion carries it, and nothing placed by a 4x4 moves hundreds of units by itself in one
// tick. A pair failing both is two instances of one mesh (one leaving view at one edge, another entering at the
// other edge, or a far one swapped with a near one); blending them slides a "ghost" tree through the in-between
// frames. Such draws become unmatched, so their 4x4 is carried back by the camera motion instead (exact for scenery).
PROF_NOINLINE static void VerifyMatches(const Frame& N, const Frame& P) {
    if (g_ablate & 4) return;
    CamClear();
    M4 ident{};
    for (int r = 0; r < 4; r++) ident.m[r][r] = 1;
    for (size_t i = 0; i < N.draws.size(); i++) {
        if (g_match[i] < 0) continue;
        for (uint32_t k = 0; k < g_ownN.count[i]; k++) {
            if (g_ownN.info[g_ownN.begin[i] + k].count != 4) continue;
            auto* a = (const PConst*)((const Cmd*)(N.buf.data() + g_ownN.flat[g_ownN.begin[i] + k]) + 1);
            const PConst* b = FirstMat4(P, g_ownP, g_match[i], a->start);
            if (!b) continue;
            if (memcmp(a + 1, b + 1, 64) == 0) {  // unchanged: a vote for a still camera
                uint64_t key = CamKey(N.draws[i].rt, a->start);
                CamCtx& cx = CamAt(key);
                if (cx.cand.size() < 512 && FirstWitness(cx, key, (const float*)(b + 1), (const float*)(a + 1))) cx.cand.push_back({nullptr, nullptr});
            } else {
                AddCameraCandidate(N.draws[i].rt, a->start, (const float*)(b + 1), (const float*)(a + 1));
            }
        }
    }
    SolveCameras();
    struct Bad { int draw; uint64_t ctx; float px, py, pw, cx, cy, cw; };
    std::vector<Bad> bad;
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> tally;  // per camera context: checked, failed
    for (size_t i = 0; i < N.draws.size(); i++) {
        if (g_match[i] < 0) continue;
        const PConst* a = FirstMat4(N, g_ownN, i);
        if (!a) continue;
        uint64_t key = CamKey(N.draws[i].rt, a->start);
        auto it = g_cam.find(key);
        const PConst* b = FirstMat4(P, g_ownP, g_match[i], a->start);
        if (!b) continue;
        const float* vc = (const float*)(a + 1);
        const float* vp = (const float*)(b + 1);
        if (it == g_cam.end() || !it->second.has) {
            // no camera estimate for this pass: can't verify; record big jumps for the rolling recorder ('u')
            if (C.retro && vc[15] > 1e-4f && vp[15] > 1e-4f) {
                float px = vp[3] / vp[15], py = vp[7] / vp[15], cx = vc[3] / vc[15], cy = vc[7] / vc[15];
                if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > 0.25f && std::fabs(vc[15] - vp[15]) + std::fabs(vc[3] - vp[3]) > 150.0f) {
                    if (g_sweeps.size() != 2048) g_sweeps.assign(2048, Sweep{});
                    g_sweeps[g_sweepPos++ % 2048] = {g_tickNo, (uint32_t)i, 'u', px, py, vp[15], cx, cy, vc[15]};
                }
            }
            continue;
        }
        const M4& D = it->second.D;
        double c[4] = {vc[3], vc[7], vc[11], vc[15]}, e[4], p[4] = {vp[3], vp[7], vp[11], vp[15]};
        for (int r = 0; r < 4; r++) e[r] = D.m[r][0] * c[0] + D.m[r][1] * c[1] + D.m[r][2] * c[2] + D.m[r][3] * c[3];
        auto& t = tally[key];
        t.first++;
        // own motion of the origin in clip units (x, y scaled by the projection, w = view depth): about world units
        double dx = e[0] - p[0], dy = e[1] - p[1], dw = e[3] - p[3];
        bool fail = dx * dx + dy * dy + dw * dw > 150.0 * 150.0;
        if (fail && e[3] > 1e-4 && p[3] > 1e-4) {  // ...and a visible jump: across the screen or in depth
            double nx = e[0] / e[3] - p[0] / p[3], ny = e[1] / e[3] - p[1] / p[3];
            fail = nx * nx + ny * ny > 0.25 || std::fabs(dw) > 0.5 * std::max(e[3], p[3]);
            // (an origin far off the screen - a world-space mesh placed by the camera matrix itself, its origin the
            // world's - magnifies any error of the estimate: there, a pair that sat where it was is kept)
            double px = p[0] / p[3], py = p[1] / p[3];
            if (fail && (std::fabs(px) > 1.5 || std::fabs(py) > 1.5) && c[3] > 1e-4) {
                double rx = c[0] / c[3] - px, ry = c[1] / c[3] - py;
                if (rx * rx + ry * ry <= 0.25 && std::fabs(c[3] - p[3]) <= 0.5 * std::max(c[3], p[3])) fail = false;
            }
        }
        if (!fail) continue;
        t.second++;
        bad.push_back({(int)i, key, (float)(p[3] ? p[0] / p[3] : 0), (float)(p[3] ? p[1] / p[3] : 0), (float)p[3],
                       (float)(c[3] ? c[0] / c[3] : 0), (float)(c[3] ? c[1] / c[3] : 0), (float)c[3]});
    }
    for (const Bad& x : bad) {
        const auto& t = tally[x.ctx];
        if (t.second * 4 > t.first) continue;  // most of a pass disagrees: the camera estimate is what's wrong
#ifdef PAIRBENCH
        if (g_debugJumps) printf("  reject (verify): draw %d <- %d\n", x.draw, g_match[x.draw]);
#endif
        g_prevUsed[g_match[x.draw]] = 0;
        g_match[x.draw] = -1;
        g_rejCur[x.draw] = 1;
        St.rejected++;
        St.verifyRejected++;
        g_tkRejected++;
        if (C.retro) {
            if (g_sweeps.size() != 2048) g_sweeps.assign(2048, Sweep{});
            g_sweeps[g_sweepPos++ % 2048] = {g_tickNo, (uint32_t)x.draw, 'x', x.px, x.py, x.pw, x.cx, x.cy, x.cw};
        }
    }
}

// After the pairing the in-between frames of a tick use: keep it as next tick's history.
PROF_NOINLINE static void SaveHistory(const Frame& N, const Frame& P) {
    size_t most = 0;
    for (const Pair& p : g_pairs) most += p.prev != NONE;
    g_lastPairs.Reset(most);
    for (const Pair& p : g_pairs) {
        if (p.prev == NONE) continue;
        uint16_t op = ((const Cmd*)(N.buf.data() + p.cur))->op;
        if (op == OP_VSCF || op == OP_PSCF) g_lastPairs.Set(p.cur, p.prev);
    }
    g_lastPairsCur = N.serial;
    g_lastPairsPrev = P.serial;
}

#ifdef FPS_SAMPLER
// Profiling build only (build.ps1 -Profile): a thread samples the render thread's instruction pointer and three
// frame-pointer return addresses while BuildPairs runs; mhfz-fps-prof.bin holds the per-16-byte histograms
// (self, inclusive) for tools\prof.py.
static volatile LONG g_sampling;
static uint32_t g_profSelf[1 << 16], g_profIncl[1 << 16];
static uintptr_t g_profBase;
static DWORD WINAPI SamplerThread(void*) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_tid);
    if (!th) return 0;
    for (int64_t n = 0;; n++) {
        int64_t t0 = Qpc();
        while (Ms(Qpc() - t0) < 0.05) YieldProcessor();
        if (!g_sampling) { if ((n & 0x3fff) == 0) Sleep(1); continue; }
        if (SuspendThread(th) == (DWORD)-1) continue;
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(th, &c) && g_sampling) {
            uintptr_t ip = c.Eip, bp = c.Ebp;
            uint32_t seen[4];
            int ns = 0;
            for (int k = 0; k < 4; k++) {
                if (ip >= g_codeBeg && ip < g_codeEnd) {
                    uint32_t b = (uint32_t)((ip - g_profBase) >> 4) & 0xffff;
                    if (k == 0) g_profSelf[b]++;
                    bool dup = false;
                    for (int s = 0; s < ns; s++) dup |= seen[s] == b;
                    if (!dup) { g_profIncl[b]++; seen[ns++] = b; }
                }
                if (!bp || bp & 3 || IsBadReadPtr((void*)bp, 8)) break;
                ip = ((uintptr_t*)bp)[1];
                bp = ((uintptr_t*)bp)[0];
            }
        }
        ResumeThread(th);
    }
}
static void ProfSave() {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\mhfz-fps-prof.bin", g_dir);
    if (FILE* fp = fopen(path, "wb")) {
        uint32_t base = (uint32_t)g_profBase;
        fwrite(&base, 4, 1, fp);
        fwrite(g_profSelf, 4, 1 << 16, fp);
        fwrite(g_profIncl, 4, 1 << 16, fp);
        fclose(fp);
    }
}
#endif

// ------------------------------------------------------------------------------------------------ worker threads
// BuildPairs spreads work whose outputs don't interact over a few threads (the render thread takes part): the
// per-frame collection passes, and the mesh groups (each touches only its own draws). The result is identical to
// running them one after the other.
static int g_workers;  // besides the render thread
static HANDLE g_wkSem, g_wkDone;
static volatile LONG g_wkNext, g_wkCount, g_wkBusy;
static void (*g_wkFn)(void*, int, int);
static void* g_wkCtx;
static DWORD WINAPI WorkerMain(void* p) {
    int tid = (int)(intptr_t)p;
    for (;;) {
        WaitForSingleObject(g_wkSem, INFINITE);
        for (LONG i; (i = InterlockedIncrement(&g_wkNext) - 1) < g_wkCount;) g_wkFn(g_wkCtx, i, tid);
        if (InterlockedDecrement(&g_wkBusy) == 0) SetEvent(g_wkDone);
    }
}
static void PoolInit() {
    if (g_wkSem) return;
    // automatic: the physical cores less two (the game's render thread, the driver's thread), at most 6 threads
    int cores = 0;
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> lpi(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    if (len && GetLogicalProcessorInformation(lpi.data(), &len))
        for (DWORD i = 0; i < len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i++) cores += lpi[i].Relationship == RelationProcessorCore;
    if (cores <= 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cores = (int)si.dwNumberOfProcessors / 2;
    }
    int want = C.pair_threads > 0 ? C.pair_threads - 1 : std::min(6, std::max(1, cores - 2)) - 1;
    g_workers = std::max(0, std::min(kMaxThreads - 1, want));
    g_wkSem = CreateSemaphoreA(nullptr, 0, kMaxThreads, nullptr);
    g_wkDone = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    for (int k = 0; k < g_workers; k++) {
        HANDLE h = CreateThread(nullptr, 256 * 1024, WorkerMain, (void*)(intptr_t)(k + 1), 0, nullptr);
        if (!h) { g_workers = k; break; }
        CloseHandle(h);
    }
    Log("pairing threads: %d (+ render thread), %d cores", g_workers, cores);
}
template <class F>
static void ParallelFor(int count, F& f) {  // f(item, thread index); thread 0 = the caller
    if (g_workers == 0 || count < 2) {
        for (int i = 0; i < count; i++) f(i, 0);
        return;
    }
    g_wkFn = [](void* c, int i, int tid) { (*(F*)c)(i, tid); };
    g_wkCtx = &f;
    g_wkNext = 0;
    g_wkCount = count;
    int wake = std::min(g_workers, count - 1);
    g_wkBusy = wake;
    ReleaseSemaphore(g_wkSem, wake, nullptr);
    for (LONG i; (i = InterlockedIncrement(&g_wkNext) - 1) < count;) f(i, 0);
    WaitForSingleObject(g_wkDone, INFINITE);
}

// The whole pairing result in one number (debug_pairhash, tools\pairhash.ps1, tools\pairbench.cpp).
static uint64_t PairHash() {
    uint64_t h = 1469598103934665603ull;
    for (const Pair& p : g_pairs) h = Fnv(Fnv(Fnv(Fnv(Fnv(Fnv(h, p.cur), p.prev), p.mi), p.synth), (uint32_t)(int32_t)p.boneBase), p.upSynth);
    for (int m : g_match) h = Fnv(h, (uint32_t)m);
    for (char f : g_drawFlag) h = Fnv(h, (uint32_t)f);
    // (and what goes with them: the lerp masks, the text decisions, the moved screen quads)
    for (const Mask256& m : g_pairMask) h = Fnv(Fnv(Fnv(Fnv(h, m.b[0]), m.b[1]), m.b[2]), m.b[3]);
    for (char c : g_runMatch) h = Fnv(h, (uint8_t)c);
    for (float f : g_runSynth) { uint32_t u; memcpy(&u, &f, 4); h = Fnv(h, u); }
    for (size_t k = 0; k + 8 <= g_upSynth.size(); k += 8) { uint64_t u; memcpy(&u, &g_upSynth[k], 8); h = Fnv(h, u); }
    return h;
}
static void BuildPairsImpl(const Frame& N, const Frame& P);
static void BuildPairs(const Frame& N, const Frame& P);

// The matching of frame N with N-1 needs N's whole recording, which is complete at the limiter's Sleep - and that is
// where the deferred game frame gets executed (several ms of driver calls on the render thread). The matching runs on
// its own thread meanwhile (with the worker pool). It touches nothing the execution does (which records nothing);
// everything else the render thread could do that might (record a call, create a shader, Sleep, Present, Reset)
// waits for it first (PairJoin). Its result is used if nothing was recorded after it started.
static HANDLE g_pjGo, g_pjDone;
static const Frame *g_pjN, *g_pjP;
static const Frame* g_pjReal;  // the frame matched
static uint32_t g_pjNvscf;     // (its constant uploads are numbered: none may come after it)
static int64_t g_pjStartAt;
static uint32_t g_pjNSerial, g_pjPSerial;
static size_t g_pjBuf, g_pjDraws;
static bool g_pjOk;
static double g_pjLastMs;
static DWORD WINAPI PairJobMain(void*) {
    for (;;) {
        WaitForSingleObject(g_pjGo, INFINITE);
        int64_t t0 = Qpc();
        BuildPairs(*g_pjN, *g_pjP);
        g_pjLastMs = Ms(Qpc() - t0);
        g_pjOk = true;
        InterlockedExchange(&g_pjBusy, 0);
        SetEvent(g_pjDone);
    }
}
static void PairJoin() {
    if (!g_pjBusy || GetCurrentThreadId() != g_tid) return;
    WhereScope ws("PairJoin");
    PhaseScope ps(3);
    int64_t t0 = Qpc();
    WaitForSingleObject(g_pjDone, INFINITE);
    St.pjWaitMs += Ms(Qpc() - t0);
}
static int g_selfcheckDue;  // (the self-check pairs differently: no background job then)
static int g_pcLeft;  // (pairing captures still to save: PairCapSave)
// A Present said "device lost": no matching and no in-between frames until the device answers again - a Reset, or the
// game's own next Present going through (see PresentBody). Logged the first time it happens each time.
static void NoteLost(const char* whose) {
    St.lostReports++;
    if (!g_lost) Log("%s Present said the device is lost - in-between frames paused until it answers again", whose);
    g_lost = true;
}static void PairJobStart() {
    PairJoin();
    if (!C.pair_async || !g_defer || C.mode != 2 || !C.enabled || g_lost || g_trState == 2 || g_selfcheckDue || g_pcLeft > 0) return;
    const Frame& N = g_fr[g_cur];
    const Frame& P = g_fr[PrevIdx(1)];
    if (!N.valid || N.draws.empty() || !P.valid || P.draws.empty()) return;
    if (!g_pjGo) {
        g_pjGo = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        g_pjDone = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        HANDLE h = g_pjGo && g_pjDone ? CreateThread(nullptr, 512 * 1024, PairJobMain, nullptr, 0, nullptr) : nullptr;
        if (!h) { C.pair_async = 0; return; }
        CloseHandle(h);
    }
    PoolInit();
    g_pjNvscf = N.nvscf;
    g_pjReal = &N;
    g_pjN = &N;
    g_pjStartAt = Qpc();
    g_pjP = &P;
    g_pjNSerial = N.serial;
    g_pjPSerial = P.serial;
    g_pjBuf = N.buf.size();
    g_pjDraws = N.draws.size();
    g_pjOk = false;
    ResetEvent(g_pjDone);
    InterlockedExchange(&g_pjBusy, 1);
    SetEvent(g_pjGo);
}
static bool PairJobFor(const Frame& N, const Frame& P) {  // its result is the matching of N with P as they are now
    // (state recorded after its copy is fine: no draw reads it in this frame, and no pair refers to it)
    return g_pjOk && g_pjReal == &N && g_pjP == &P && g_pjNSerial == N.serial && g_pjPSerial == P.serial && g_pjBuf <= N.buf.size() &&
           g_pjDraws == N.draws.size() && g_pjNvscf == N.nvscf;
}

static void BuildPairs(const Frame& N, const Frame& P) {
#ifdef FPS_SAMPLER
    static bool started;
    if (!started) { started = true; g_profBase = g_modBase; CreateThread(nullptr, 0, SamplerThread, nullptr, 0, nullptr); }
    g_sampling = 1;
    BuildPairsImpl(N, P);
    g_sampling = 0;
    static int saves;
    if (++saves % 30 == 0) ProfSave();
#else
    BuildPairsImpl(N, P);
#endif
    if (C.debug_pairhash) Log("pairhash %llu %016llx %zu", (unsigned long long)g_tickNo, (unsigned long long)PairHash(), g_pairs.size());
}
static void BuildPairsImpl(const Frame& N, const Frame& P) {
    g_phaseT = Qpc();
    g_pairsGen++;
    g_histFrame = nullptr;
    if (P.serial && P.serial == g_lastPairsCur)
        for (const Frame& f : g_fr)
            if (f.valid && f.serial == g_lastPairsPrev) g_histFrame = &f;
    g_posPkts = g_bigPkts = 0;
    g_pairs.clear();
    g_pairMask.clear();
    g_prevIdx.clear();
    g_occ.clear();
    PoolInit();
    g_prevUsed.assign(P.draws.size(), 0);
    g_drawFlag.assign(N.draws.size(), 0);
    g_match.assign(N.draws.size(), -1);
    std::vector<int>& match = g_match;
    Phase(0);
    // per-frame passes, independent of each other: in parallel. (The previous frame was last tick's current frame:
    // its lists are already there.)
    bool reuse = P.serial && P.serial == g_ownNSerial;
    if (reuse) { std::swap(g_ownP, g_ownN); std::swap(g_liveP, g_liveN); }
    // ...and the matching of screen text alongside them: it reads only the glyph quads (no shader tables, none of the
    // passes' lists) and writes only what they don't read
    g_rejCur.assign(N.draws.size(), 0);
    {
        auto pass = [&](int k, int) {
            switch (k) {
                case 0:
                    if (C.text_ckeys) TextKeys(N), TextKeys(P);
                    MatchTextRuns(N, P);
                    break;
                case 1: CollectOwners(N, g_ownN); break;
                case 2: CollectLive(N, g_liveN); break;
                case 3: ComputePosMasks(N); break;
                case 4: CollectOwners(P, g_ownP); break;
                case 5: CollectLive(P, g_liveP); break;
            }
        };
        int n = reuse ? 4 : 6;
        g_infoMiss = 0;
        g_infoRO = g_workers > 0;
        ParallelFor(n, pass);
        g_infoRO = false;
        if (g_infoMiss)  // a shader not analysed yet (created before our hook): the passes again on this thread, with lookups
            for (int k = 1; k < n; k++) pass(k, 0);
    }
    Phase(1);
    g_ownNSerial = N.serial;
    Phase(2);
    CamClear();
    // Matching per key group; pass 0 strict key, pass 1 geometry-only key for what is left. Meshes that occur once
    // in both frames pair directly and give a first camera-motion estimate; repeated meshes (leaf billboards,
    // props, decals: often hundreds, re-sorted every frame) are then paired over the whole group by where each
    // instance should have been in the previous frame.
    std::vector<int> ones;
    for (int pass = 0; pass < 2; pass++) {
        // the draws by key: the map numbers the keys (and gives the order the groups are visited in - the camera
        // estimate depends on it), the members of each group are in flat lists, in draw order
        std::unordered_map<uint64_t, uint32_t> groups;
        static std::vector<uint32_t> gN, gP, begN, begP, at;
        static std::vector<int> memN, memP;
        gN.assign(N.draws.size(), NONE);
        gP.assign(P.draws.size(), NONE);
        begN.clear();
        for (size_t i = 0; i < N.draws.size(); i++)  // a draw refused as implausible stays unmatched in pass 1
            if (match[i] < 0 && !g_rejCur[i]) {
                auto in = groups.try_emplace(pass ? N.draws[i].key2 : N.draws[i].key, (uint32_t)begN.size());
                if (in.second) begN.push_back(0);
                gN[i] = in.first->second;
                begN[gN[i]]++;
            }
        size_t ng = begN.size();
        begP.assign(ng, 0);
        for (size_t j = 0; j < P.draws.size(); j++) {
            if (g_prevUsed[j]) continue;
            auto it = groups.find(pass ? P.draws[j].key2 : P.draws[j].key);
            if (it != groups.end()) gP[j] = it->second, begP[it->second]++;
        }
        auto lay = [&](std::vector<uint32_t>& beg, const std::vector<uint32_t>& g, std::vector<int>& mem) {  // (counts -> starts, then fill)
            uint32_t s = 0;
            for (size_t k = 0; k < ng; k++) { uint32_t c = beg[k]; beg[k] = s; s += c; }
            beg.push_back(s);
            mem.resize(s);
            at.assign(beg.begin(), beg.end() - 1);
            for (size_t d = 0; d < g.size(); d++) if (g[d] != NONE) mem[at[g[d]]++] = (int)d;
        };
        lay(begN, gN, memN);
        lay(begP, gP, memP);
        auto curOf = [&](uint32_t k) { return IdxSpan{memN.data() + begN[k], begN[k + 1] - begN[k]}; };
        auto prevOf = [&](uint32_t k) { return IdxSpan{memP.data() + begP[k], begP[k + 1] - begP[k]}; };
        for (auto& kv : groups) {
            struct { IdxSpan first, second; } g{curOf(kv.second), prevOf(kv.second)};
            if (g.first.size() != 1 || g.second.size() != 1) continue;
            if (const PConst* bc = FirstMat4(N, g_ownN, g.first[0]) ? nullptr : FirstBones(N, g_ownN, g.first[0])) {
                // the only instance in both frames can still be two different objects (one leaving view, one
                // entering): a bone-driven object cannot move further than this in one tick
                if (const PConst* bp = FirstBones(P, g_ownP, g.second[0], bc->start)) {
                    const float *a = (const float*)(bc + 1), *b = (const float*)(bp + 1);
                    double dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
                    if (dx * dx + dy * dy + dz * dz > 150.0 * 150.0) {
                        St.rejected++;
                        g_tkRejected++;
                        g_rejCur[g.first[0]] = 1;
                        continue;
                    }
                }
            }
            match[g.first[0]] = g.second[0];
            g_prevUsed[g.second[0]] = 1;
            ones.push_back(g.first[0]);
            if (pass == 0) CamCandidateFromOwners(N, g.first[0], P, g.second[0]);
        }
        if (pass == 0) SolveCameras();
        // Single instances placed by a 4x4: with the camera motion known, a static object must sit where predicted;
        // a pair far off is one object leaving view and a different one entering
        for (int i : ones) {
            const PConst* a = FirstMat4(N, g_ownN, i);
            if (!a) continue;
            auto it = g_cam.find(CamKey(N.draws[i].rt, a->start));
            if (it == g_cam.end() || !it->second.has) continue;
            Inst ci, pi;
            InstOf(N, g_ownN, i, a->start, &it->second.D, ci);
            InstOf(P, g_ownP, match[i], a->start, nullptr, pi);
            // only near-teleports (about half a screen beyond what the camera explains): objects that move by
            // themselves must keep their own motion
            if (!ci.ok || !pi.ok || InstDist(ci, pi) <= 0.25f) continue;
            // ...and only if it's far off as it is, too: the estimate can be wrong (MHF's per-object depth mapping
            // spoils the vote), and a world-space mesh placed by the camera matrix itself, its origin the world's,
            // far off the screen, magnifies any error - it sat where it was, yet was dropped every tick and drawn
            // with the next tick's camera in every in-between frame
            Inst c0;
            InstOf(N, g_ownN, i, a->start, nullptr, c0);
            if (c0.ok && InstDist(c0, pi) <= 0.25f) continue;
#ifdef PAIRBENCH
            if (g_debugJumps)
                printf("  reject (teleport): draw %zu <- %d: distance %.4g, predicted ndc (%.3g %.3g) w %.4g, previous (%.3g %.3g) w %.4g\n", (size_t)i, match[i], InstDist(ci, pi),
                       ci.c[0] / ci.c[3], ci.c[1] / ci.c[3], ci.c[3], pi.c[0] / pi.c[3], pi.c[1] / pi.c[3], pi.c[3]);
#endif
            g_prevUsed[match[i]] = 0;
            match[i] = -1;
            g_rejCur[i] = 1;
            St.rejected++;
            g_tkRejected++;
        }
        // (the field: what pass 0 matched uniquely, for the repeated meshes)
        if (pass == 0) {
            g_field.clear();
#ifdef PAIRBENCH
            if (C.field_match || g_drawInfoField)  // (-drawinfo: the field for the analysis too)
#else
            if (C.field_match)
#endif
                for (int i : ones) {
                    if (match[i] < 0) continue;
                    const PConst* a = FirstMat4(N, g_ownN, i);
                    const PConst* b = a ? FirstMat4(P, g_ownP, match[i], a->start) : nullptr;
                    if (!b) continue;
                    const float *va = (const float*)(a + 1), *vb = (const float*)(b + 1);
                    if (!(va[15] > 1e-3f) || !(vb[15] > 1e-3f)) continue;
                    float x = va[3] / va[15], y = va[7] / va[15], px = vb[3] / vb[15], py = vb[7] / vb[15];
                    if (std::fabs(x) > 2 || std::fabs(y) > 2 || std::fabs(px) > 2 || std::fabs(py) > 2) continue;  // (on or near the screen)
                    g_field.push_back({CamKey(N.draws[i].rt, a->start), x, y, va[15], px - x, py - y, vb[15] / va[15]});
                }
        }
        ones.clear();
        Phase(13);
        // repeated meshes, group by group: each group only touches its own draws, so they run in parallel
        struct Multi { IdxSpan first, second; };
        std::vector<Multi> multi;
        for (auto& kv : groups) {
            Multi g{curOf(kv.second), prevOf(kv.second)};
            if (g.first.empty() || g.second.empty() || (g.first.size() == 1 && g.second.size() == 1)) continue;
            multi.push_back(g);
        }
        // biggest first, so one large group doesn't finish last on its own
        std::sort(multi.begin(), multi.end(), [](const Multi& a, const Multi& b) {
            size_t x = a.first.size() * a.second.size(), y = b.first.size() * b.second.size();
            return x != y ? x > y : a.first[0] < b.first[0];
        });
        static std::vector<char> poorK;  // (per group: paired poorly - see below)
        poorK.assign(multi.size(), 0);
        auto group = [&](int k, int tid) { poorK[k] = MatchGroup(N, P, multi[k].first, multi[k].second, tid); };
        g_infoMiss = 0;
        g_infoRO = g_workers > 0;
        ParallelFor((int)multi.size(), group);
        g_infoRO = false;
        if (g_infoMiss) Log("pairing: shader table miss during the group matching");
        // Groups paired with no camera estimate for their pass (it came from the objects met once, and a pass of trees
        // and hills has few), or paired poorly (the estimate wrong - a handful of objects met once fooled it in a fast
        // flick): what is paired now gives a better one - candidates from all of it for those passes only, the origins'
        // maps fitted again - and they are paired again with it. (Distant trees are camera-facing cards: each turns to the camera, so their own matrices
        // never agree on a camera motion, and by nearest position a fast flick paired some with their neighbours; the
        // pair was refused as too far and the tree drawn a tick ahead - a flicker in the treeline.)
        if (pass == 0 && C.group_cam) {
            static std::vector<int> redo;
            static std::vector<uint64_t> ctxs;
            redo.clear(), ctxs.clear();
            for (int k = 0; k < (int)multi.size(); k++) {
                const IdxSpan& c = multi[k].first;
                if (c.size() < 2) continue;
                const PConst* f = FirstMat4(N, g_ownN, c[0]);
                if (!f) continue;
                bool same = true;  // (one 4x4 for every instance: paired by bones, no camera needed)
                for (size_t x = 1; x < c.size() && same; x++) {
                    const PConst* o = FirstMat4(N, g_ownN, c[x], f->start);
                    same = o && memcmp(o + 1, f + 1, 64) == 0;
                }
                if (same) continue;
                uint64_t key = CamKey(N.draws[c[0]].rt, f->start);
                auto it = g_cam.find(key);
                if (it != g_cam.end() && (it->second.has || it->second.hasA) && !poorK[k]) continue;
                redo.push_back(k);
                if (std::find(ctxs.begin(), ctxs.end(), key) == ctxs.end()) ctxs.push_back(key);
            }
            if (!redo.empty()) {
                for (size_t i = 0; i < N.draws.size(); i++) {
                    if (match[i] < 0) continue;
                    for (uint32_t k = 0; k < g_ownN.count[i]; k++) {  // (every 4x4 it reads in those passes - not only its first)
                        if (g_ownN.info[g_ownN.begin[i] + k].count != 4) continue;
                        auto* a = (const PConst*)((const Cmd*)(N.buf.data() + g_ownN.flat[g_ownN.begin[i] + k]) + 1);
                        if (std::find(ctxs.begin(), ctxs.end(), CamKey(N.draws[i].rt, a->start)) == ctxs.end()) continue;
                        const PConst* b = FirstMat4(P, g_ownP, match[i], a->start);
                        if (b && memcmp(a + 1, b + 1, 64) != 0) AddCameraCandidate(N.draws[i].rt, a->start, (const float*)(b + 1), (const float*)(a + 1));
                    }
                }
#ifdef PAIRBENCH
                if (g_debugJumps)
                    for (uint64_t key : ctxs) {
                        auto it = g_cam.find(key);
                        printf("  again: context %016llx has %zu candidates now (%zu groups waiting)\n", (unsigned long long)key, it != g_cam.end() ? it->second.cand.size() : (size_t)0, redo.size());
                    }
#endif
                for (uint64_t key : ctxs) {  // (fitted again with all of it)
                    auto it = g_cam.find(key);
                    if (it != g_cam.end()) it->second.hasA = false;
                }
                SolveCameras();  // (only those passes got new candidates: the others come out as before)
                static std::vector<int> again;
                again.clear();
                for (int k : redo) {
                    const IdxSpan& c = multi[k].first;
                    const PConst* f = FirstMat4(N, g_ownN, c[0]);
                    auto it = g_cam.find(CamKey(N.draws[c[0]].rt, f->start));
                    if (it == g_cam.end() || !(it->second.has || it->second.hasA)) continue;
                    for (int i : c) {  // (undone: its pairs, and the refusals of its first try)
                        if (match[i] >= 0) g_prevUsed[match[i]] = 0, match[i] = -1;
                        if (g_rejCur[i]) g_rejCur[i] = 0, St.rejected--, g_tkRejected--;
                    }
                    again.push_back(k);
                    St.groupsAgain++;
                }
                auto group2 = [&](int k, int tid) { MatchGroup(N, P, multi[again[k]].first, multi[again[k]].second, tid); };
                g_infoRO = g_workers > 0;
                ParallelFor((int)again.size(), group2);
                g_infoRO = false;
            }
        }
        Phase(14);
    }
    Phase(3);
    RefineScreenGroups(N, P);
    Phase(4);
    VerifyMatches(N, P);
    Phase(5);
    CamClear();  // re-estimated below from all matched pairs
    g_pktPaired.assign(N.nvscf, 0);
    g_synth.clear();
    g_needSynth.clear();
    {
        // first matched reader of every live upload (what decides it), in draw order
        if (++g_frCur == 0) { std::fill(g_frE.begin(), g_frE.end(), FrE{0, 0}); g_frCur = 1; }
        if (g_frE.size() < N.buf.size() / 16 + 1) g_frE.resize(N.buf.size() / 16 + 1, FrE{0, 0});
        static std::vector<uint32_t> matched;
        matched.clear();
        g_ownFirst.assign(N.nvscf, NONE);
        for (uint32_t i = 0; i < N.draws.size(); i++) {
            if (match[i] < 0) { St.unmatched++; continue; }
            St.matched++;
            matched.push_back(i);
            for (uint32_t k = 0; k < g_liveN.count[i]; k++) {
                uint32_t o = g_liveN.flat[g_liveN.begin[i] + k] >> 4;
                if (g_frE[o].gen != g_frCur) g_frE[o] = {g_frCur, i};
            }
            for (uint32_t k = 0; k < g_ownN.count[i]; k++) {
                uint32_t ord = g_ownN.info[g_ownN.begin[i] + k].ord;
                if (g_ownFirst[ord] == NONE) g_ownFirst[ord] = i;
            }
        }
        // step A: chunks of matched draws on all threads
        const int kChunk = 32;
        int nChunks = (int)((matched.size() + kChunk - 1) / kChunk);
        static std::vector<PairChunk> chunks;
        if ((int)chunks.size() < nChunks) chunks.resize(nChunks);
        auto stepA = [&](int ch, int tid) {
            PairChunk& out = chunks[ch];
            out.Clear();
            size_t a = (size_t)ch * kChunk, b = std::min(matched.size(), a + kChunk);
            for (size_t m = a; m < b; m++) {
                uint32_t i = matched[m], j = (uint32_t)match[i];
                OwnersA(N, i, P, j, out);
                LiveA(N, i, P, j, out, g_pscr[tid]);
                SegmentA(N, i, P, j, out, g_pscr[tid]);
            }
        };
        g_infoMiss = 0;
        g_infoRO = g_workers > 0;
        ParallelFor(nChunks, stepA);
        g_jumped.clear();
        g_infoRO = false;
        if (g_infoMiss) Log("pairing: shader table miss while pairing constants");
        // step B: commit in draw order
        const uint8_t* bn = N.buf.data();
        const uint8_t* bp = P.buf.data();
        for (int ch = 0; ch < nChunks; ch++) {
            PairChunk& in = chunks[ch];
            St.snaps += in.snaps;
            St.upSnaps += in.upSnaps;
            for (const PRec& r : in.recs) {
                if (r.type == R_OWNER) {
                    const Cmd* c = (const Cmd*)(bn + r.oc);
                    auto* q = (const PConst*)(c + 1);
                    if (g_pktPaired[q->ord]) continue;  // an earlier owner decided
                    uint32_t found = r.found;
                    const float* fp = found != NONE ? (const float*)((const PConst*)((const Cmd*)(bp + found) + 1) + 1) : nullptr;
                    if (found != NONE && q->count == 4) {
                        if (r.flags & F_CROSS) { St.crossing++; found = NONE; }  // a different object
                        else NoteSweep(r.draw, 'm', fp, (const float*)(q + 1));
                    } else if (found != NONE && q->count >= 3 && q->count % 3 == 0 && (r.flags & F_BONEJUMP)) {
                        // bone palette (world space): a root bone that jumps further than anything moves in one tick
                        // means two different objects were paired; don't blend between them
                        const float* b = (const float*)(q + 1);
                        St.crossing++;
#ifdef PAIRBENCH
                        if (g_debugJumps)
                            printf("  bone jump: draw %u (key %016llx) <- %d: root (%.0f %.0f %.0f) -> (%.0f %.0f %.0f), packet c%u x%u\n", r.draw, (unsigned long long)N.draws[r.draw].key,
                                   g_match[r.draw], fp[3], fp[7], fp[11], b[3], b[7], b[11], q->start, q->count);
#endif
                        if (C.retro) {
                            if (g_sweeps.size() != 2048) g_sweeps.assign(2048, Sweep{});
                            g_sweeps[g_sweepPos++ % 2048] = {g_tickNo, r.draw, 'b', fp[3], fp[7], fp[11], b[3], b[7], b[11]};
                        }
                        g_drawFlag[r.draw] = 1;
                        g_pktPaired[q->ord] = 1;  // keep its current pose (t=1) rather than blending from another object
                        continue;
                    }
                    if (found == NONE) {  // no counterpart: try the camera-motion estimate later
                        g_needSynth.push_back({r.draw, r.oc});
                        continue;
                    }
                    g_pktPaired[q->ord] = 1;
                    if (!C.interp_vs) continue;
                    PRec rr = r;
                    if (rr.flags & F_DEFER) OwnerLerp(N, P, rr, in.masks);  // an earlier owner had no counterpart
                    const PRec& r = rr;
                    St.steadyLerps += r.ls.steady, St.slotJumps += r.ls.jump;
                    if (r.ls.jump) g_jumped.push_back(r.oc);
                    g_posPkts += r.ls.pos;
                    g_bigPkts += r.ls.big;
                    if (!(r.flags & F_LERP)) continue;  // identical values
                    uint32_t mi = (uint32_t)g_pairMask.size();
                    g_pairMask.push_back(in.masks[r.mask]);
                    g_pairs.push_back({r.oc, found, mi, NONE});
                    g_pairs.back().boneBase = r.boneBase;
                    if (q->count == 4) g_pairs.back().camKey = CamKey(N.draws[r.draw].rt, q->start);
                    if (q->count == 4) AddCameraCandidatePre(N.draws[r.draw].rt, q->start, fp, (const float*)(q + 1), r.camHash, (r.flags & F_INV) != 0);
                } else if (r.type == R_CONST) {
                    St.steadyLerps += r.ls.steady, St.slotJumps += r.ls.jump;
                    if (r.ls.jump) g_jumped.push_back(r.oc);
                    g_posPkts += r.ls.pos;
                    g_bigPkts += r.ls.big;
                    if (!(r.flags & F_LERP)) continue;  // identical values: nothing to lerp
                    uint32_t mi = (uint32_t)g_pairMask.size();
                    g_pairMask.push_back(in.masks[r.mask]);
                    g_pairs.push_back({r.oc, r.found, mi, NONE});
                    if (r.flags & F_LIVE) St.livePairs++;
                } else {
                    g_pairs.push_back({r.oc, r.found, NONE, NONE});
                }
            }
        }
    }
    if (C.slot_rows) {
        SlotSwaps(N, P);
        SlotRows(N, P);
    }
    Phase(6);
    // new / re-LOD'd objects and uploads without counterpart: interpolate them with the measured camera motion
    SolveCameras();
    if (C.debug_cam && (g_checkTick++ % 60) == 0) {
        size_t unm = 0;
        for (int m : match) unm += m < 0;
        Log("cam: %zu contexts, %zu needSynth, %zu unmatched draws", g_cam.size(), g_needSynth.size(), unm);
        for (auto& kv : g_cam) {
            Log("  ctx %016llx: %zu candidates, solved %d", (unsigned long long)kv.first, kv.second.cand.size(), kv.second.has);
            for (auto& cand : kv.second.cand)
                if (M4 d = CandMatrix(cand); true) Log("    D row0 %.4f %.4f %.4f %.4f row1 %.4f %.4f %.4f %.4f", d.m[0][0], d.m[0][1], d.m[0][2], d.m[0][3], d.m[1][0],
                    d.m[1][1], d.m[1][2], d.m[1][3]);
        }
    }
    CameraPaths();
    // this tick's final estimates, for the next tick's groups that pair poorly (a flick turns about as fast from one
    // tick to the next)
    g_camPrev.clear();
    for (const auto& kv : g_cam)
        if (kv.second.has || kv.second.hasA) {
            CamPrev& cp = g_camPrev[kv.first];
            cp.has = kv.second.has, cp.D = kv.second.D, cp.hasA = kv.second.hasA;
            memcpy(cp.A, kv.second.A, sizeof cp.A);
        }
    g_camPrevSerial = N.serial;
    for (auto& ns : g_needSynth)
        if (!SynthesizePrev(N, ns.draw, ns.oc)) { St.unpairedPos++; g_drawFlag[ns.draw] = 1; }
    for (size_t i = 0; i < N.draws.size(); i++) {
        if (match[i] >= 0) continue;
        bool ok = g_ownN.count[i] > 0;
        for (uint32_t k = 0; k < g_ownN.count[i]; k++)
            if (!SynthesizePrev(N, (uint32_t)i, g_ownN.flat[g_ownN.begin[i] + k])) ok = false;
        if (!ok) g_drawFlag[i] = 1;
    }
    Phase(7);
    PlacePendingText(N, P);
    BuildScreenSynth(N);
    Phase(8);
    // replay walks the command stream in order and expects pairs sorted by their command offset
    std::sort(g_pairs.begin(), g_pairs.end(), [](const Pair& a, const Pair& b) { return a.cur < b.cur; });
    Phase(9);
    St.pairs += g_pairs.size();
    g_cut = g_bigPkts > 16 && g_bigPkts * 10 > g_posPkts * 4;
    if (g_cut) St.cuts++;
    if (C.retro) {  // per-tick statistics for the rolling recorder
        if (g_tickStats.size() != 256) g_tickStats.assign(256, TickStat{});
        uint32_t unm = 0;
        for (int m : match) unm += m < 0;
        g_tickStats[g_tickStatPos++ % 256] = {g_tickNo, (uint32_t)N.draws.size(), unm, g_tkRejected, g_tkSynth, (uint32_t)g_pairs.size(), g_cut};
    }
    g_tkRejected = g_tkSynth = 0;
}

// ------------------------------------------------------------------------------------------------ replay
static std::vector<float> g_scr;
static std::vector<uint8_t> g_vscr;

static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static void LerpVerts(uint8_t* dst, const uint8_t* a, UINT bytes, UINT stride, int off, int n, float t) {
    for (UINT v = 0; v + stride <= bytes; v += stride) {
        float* d = (float*)(dst + v + off);
        const float* x = (const float*)(a + v + off);
        for (int i = 0; i < n; i++) d[i] = Lerp(x[i], d[i], t);
    }
}

// Bone matrices (3 rows of [R | t]): blending rotations linearly shrinks them mid-way (cos(angle/2): 3.4% at a
// 30 degree step), which reads as twitching limbs. Rotation is slerped as a quaternion, scale and translation
// lerped. Falls back to plain lerp for anything that isn't a clean rotation times uniform scale.
static void QuatFromRot(const double R[3][3], double q[4]) {
    double tr = R[0][0] + R[1][1] + R[2][2];
    if (tr > 0) {
        double s = std::sqrt(tr + 1.0) * 2;
        q[3] = 0.25 * s; q[0] = (R[2][1] - R[1][2]) / s; q[1] = (R[0][2] - R[2][0]) / s; q[2] = (R[1][0] - R[0][1]) / s;
    } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
        double s = std::sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2;
        q[3] = (R[2][1] - R[1][2]) / s; q[0] = 0.25 * s; q[1] = (R[0][1] + R[1][0]) / s; q[2] = (R[0][2] + R[2][0]) / s;
    } else if (R[1][1] > R[2][2]) {
        double s = std::sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2;
        q[3] = (R[0][2] - R[2][0]) / s; q[0] = (R[0][1] + R[1][0]) / s; q[1] = 0.25 * s; q[2] = (R[1][2] + R[2][1]) / s;
    } else {
        double s = std::sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2;
        q[3] = (R[1][0] - R[0][1]) / s; q[0] = (R[0][2] + R[2][0]) / s; q[1] = (R[1][2] + R[2][1]) / s; q[2] = 0.25 * s;
    }
}
static bool RotScale(const float* m, double R[3][3], double& s) {
    s = 0;
    for (int i = 0; i < 3; i++) s += std::sqrt((double)m[i * 4] * m[i * 4] + (double)m[i * 4 + 1] * m[i * 4 + 1] + (double)m[i * 4 + 2] * m[i * 4 + 2]);
    s /= 3;
    if (!(s > 1e-6)) return false;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R[i][j] = m[i * 4 + j] / s;
    for (int i = 0; i < 3; i++)  // orthonormal enough?
        for (int j = 0; j < 3; j++) {
            double d = R[i][0] * R[j][0] + R[i][1] * R[j][1] + R[i][2] * R[j][2];
            if (std::fabs(d - (i == j ? 1.0 : 0.0)) > 0.05) return false;
        }
    double det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                 R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
    return det > 0;
}
// What of a bone's slerp does not depend on t (BonePrep), and the rest (BoneFinish) - the same operations as one
// function, split: the in-between frames of a tick share the first part, and the passes that draw the same character
// (shadow cascades, depth, main pass: about six uploads of the same palette) share all of it (SlerpBone's cache).
struct BonePre {
    uint8_t mode;  // 0 = plain lerp (not a clean rotation times uniform scale), 1 = linear weights, 2 = slerp
    double qa[4], qb[4], sa, sb, th, st;
};
static void BonePrep(const float* a, const float* b, BonePre& p) {
    double Ra[3][3], Rb[3][3];
    if (!RotScale(a, Ra, p.sa) || !RotScale(b, Rb, p.sb)) { p.mode = 0; return; }
    QuatFromRot(Ra, p.qa);
    QuatFromRot(Rb, p.qb);
    double dot = p.qa[0] * p.qb[0] + p.qa[1] * p.qb[1] + p.qa[2] * p.qb[2] + p.qa[3] * p.qb[3];
    if (dot < 0) { for (double& x : p.qb) x = -x; dot = -dot; }
    if (dot > 0.9995) { p.mode = 1; return; }
    p.mode = 2;
    p.th = std::acos(dot);
    p.st = std::sin(p.th);
}
static void BoneFinish(const BonePre& p, const float* a, const float* b, float t, float* out) {
    if (p.mode == 0) {
        for (int i = 0; i < 12; i++) out[i] = a[i] + (b[i] - a[i]) * t;
        return;
    }
    const double *qa = p.qa, *qb = p.qb;
    double sa = p.sa, sb = p.sb;
    double q[4], wa, wb;
    if (p.mode == 1) { wa = 1 - t; wb = t; }
    else {
        wa = std::sin((1 - t) * p.th) / p.st;
        wb = std::sin(t * p.th) / p.st;
    }
    double n = 0;
    for (int i = 0; i < 4; i++) { q[i] = wa * qa[i] + wb * qb[i]; n += q[i] * q[i]; }
    n = 1.0 / std::sqrt(n);
    double x = q[0] * n, y = q[1] * n, z = q[2] * n, w = q[3] * n;
    double R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                      {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                      {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    double s = sa + (sb - sa) * t;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) out[i * 4 + j] = (float)(R[i][j] * s);
        out[i * 4 + 3] = a[i * 4 + 3] + (b[i * 4 + 3] - a[i * 4 + 3]) * t;
    }
}
// Cache by content (compared exactly): BonePre per tick (g_boneTick), the result per in-between frame (g_boneFrame).
struct BoneSlot {
    float a[12], b[12];
    uint32_t h, tick, frame;
    BonePre pre;
    float out[12];
};
static std::vector<BoneSlot> g_bones;
static uint32_t g_boneTick = 1, g_boneFrame = 1;
static void SlerpBone(const float* a, const float* b, float t, float* out) {
    if (g_bones.empty()) g_bones.resize(1 << 14);
    uint32_t h = 2166136261u;
    const uint32_t* ua = (const uint32_t*)a;
    const uint32_t* ub = (const uint32_t*)b;
    for (int i = 0; i < 12; i++) h = (h ^ ua[i]) * 16777619u;
    for (int i = 0; i < 12; i++) h = (h ^ ub[i]) * 16777619u;
    BoneSlot* victim = nullptr;
    for (uint32_t k = 0; k < 4; k++) {
        BoneSlot& s = g_bones[(h + k) & ((1u << 14) - 1)];
        if (s.tick == g_boneTick && s.h == h && !memcmp(s.a, a, 48) && !memcmp(s.b, b, 48)) {
            if (s.frame != g_boneFrame) { BoneFinish(s.pre, a, b, t, s.out); s.frame = g_boneFrame; }
            memcpy(out, s.out, 48);
            return;
        }
        if (!victim && s.tick != g_boneTick) victim = &s;
    }
    BoneSlot& s = victim ? *victim : g_bones[h & ((1u << 14) - 1)];
    memcpy(s.a, a, 48);
    memcpy(s.b, b, 48);
    s.h = h;
    s.tick = g_boneTick;
    BonePrep(a, b, s.pre);
    BoneFinish(s.pre, a, b, t, s.out);
    s.frame = g_boneFrame;
    memcpy(out, s.out, 48);
}

// The shader constant registers as an in-between frame last set them. The game uploads most constants again for
// every draw (in a busy town 70-90% of the uploads write what the registers already hold); in an in-between frame
// those calls only cost driver time, so one that changes no register is skipped - the GPU state stays the same.
// Unknown after the frame's start state block and after any state block apply (ConstShadowReset).
static float g_cvVS[256][4], g_cvPS[256][4];
static uint8_t g_cokVS[256], g_cokPS[256];
// The same for the other states an in-between frame sets (about 2/3 of the shader binds, 4/5 of the vertex
// declarations and nearly all FVF sets repeat what is already set). D3D9 rules kept: SetFVF and
// SetVertexDeclaration replace each other, a UP draw resets stream 0 (and the indices for DrawIndexedPrimitiveUP),
// a state block apply changes anything.
struct StateShadow {
    DWORD rs[256];
    DWORD ss[21][14];
    DWORD tss[8][33];
    void* tex[21];
    void *vs, *ps, *decl, *idx;
    DWORD fvf;
    struct { void* vb; UINT off, stride; } src[16];
    uint8_t rsOk[256], ssOk[21][14], tssOk[8][33], texOk[21], srcOk[16];
    uint8_t vsOk, psOk, declOk, idxOk, fvfOk;
};
static StateShadow g_st;
static void StateShadowReset() {
    memset(g_st.rsOk, 0, sizeof g_st.rsOk);
    memset(g_st.ssOk, 0, sizeof g_st.ssOk);
    memset(g_st.tssOk, 0, sizeof g_st.tssOk);
    memset(g_st.texOk, 0, sizeof g_st.texOk);
    memset(g_st.srcOk, 0, sizeof g_st.srcOk);
    g_st.vsOk = g_st.psOk = g_st.declOk = g_st.idxOk = g_st.fvfOk = 0;
}
static inline int SamplerSlot(DWORD s) { return s < 16 ? (int)s : s >= 256 && s <= 260 ? 16 + (int)(s - 256) : -1; }
template <class T>
static inline bool SameAs(bool on, uint8_t& ok, T& slot, T v) {  // true: already set (skip); else records it
    if (on && ok && slot == v) return true;
    slot = v;
    ok = on;
    return false;
}
static void ConstShadowReset() {
    memset(g_cokVS, 0, sizeof g_cokVS);
    memset(g_cokPS, 0, sizeof g_cokPS);
    StateShadowReset();
}
static bool ConstUnchanged(bool vs, UINT start, const float* v, UINT count) {  // and records the values when not
    if (!C.replay_filter || start + count > 256) {
        if (start < 256) memset((vs ? g_cokVS : g_cokPS) + start, 0, std::min<UINT>(count, 256 - start));
        return false;
    }
    float (*reg)[4] = vs ? g_cvVS : g_cvPS;
    uint8_t* ok = vs ? g_cokVS : g_cokPS;
    UINT r = 0;
    while (r < count && ok[start + r] && memcmp(reg[start + r], v + r * 4, 16) == 0) r++;
    if (r == count) return true;
    memcpy(reg[start + r], v + r * 4, (count - r) * 16);
    memset(ok + start + r, 1, count - r);
    return false;
}

static const Mask256* g_curFrz;  // (RunCmd: registers of this command that must not be interpolated)
static const CamCtx* g_curCam;   // (RunCmd: this command's camera path)
static int g_curCamMode;
// A constant packet blended: registers in lm lerped (bone matrices from boneBase on slerped), frozen ones (frz) and
// the rest taken from v. cache: bones through SlerpBone's table (render thread only) - the same values either way.
struct CamCtx;
static void LerpConsts(const PConst* q, const float* v, const float* a, const Mask256* lm, const Mask256* frz, int boneBase, float t,
                       float* out, bool cache, const CamCtx* cam = nullptr, int camMode = 1) {
    // a positional 4x4 whose camera moved along a path: the object's own motion blended in the new frame's camera
    // (D^-1 * prev: where it was, seen by this camera), then carried along the camera's path to t
    if (cam && cam->path && q->count == 4 && lm && lm->test(0) && lm->test(1) && lm->test(2) && lm->test(3) && !frz) {
        M4 Ap = FromRegs(a), B = FromRegs(v), o;
        if (camMode == 2) {  // a camera-facing card: axes blended as they are (on the screen), the anchor along the path
            o = Comb(Ap, 1.0 - t, B, t);
            double an[4];
            for (int r = 0; r < 4; r++) {
                double pa = 0;
                for (int k = 0; k < 4; k++) pa += cam->Dinv.m[r][k] * Ap.m[k][3];
                an[r] = pa * (1.0 - t) + B.m[r][3] * t;
            }
            for (int r = 0; r < 4; r++) {
                double s = 0;
                for (int k = 0; k < 4; k++) s += cam->Ct.m[r][k] * an[k];
                o.m[r][3] = s;
            }
        } else {
            M4 A = Mul(cam->Dinv, Ap);
            o = Mul(cam->Ct, Comb(A, 1.0 - t, B, t));
        }
        bool ok = true;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) ok &= std::isfinite(o.m[r][c]);
        if (ok) {
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) out[r * 4 + c] = (float)o.m[r][c];
            return;
        }
    }
    for (UINT r = 0; r < q->count; r++) {
        int R = (int)(q->start + r);
        bool lerp = lm && lm->test((int)r) && !(frz && frz->test(R));
        if (C.slerp_bones && boneBase >= 0 && R >= boneBase && (R - boneBase) % 3 == 0 && r + 2 < q->count && lerp &&
            lm->test((int)r + 1) && lm->test((int)r + 2) && !(frz && (frz->test(R + 1) || frz->test(R + 2)))) {
            if (cache) {
                SlerpBone(a + r * 4, v + r * 4, t, out + r * 4);
            } else {
                BonePre pre;
                BonePrep(a + r * 4, v + r * 4, pre);
                BoneFinish(pre, a + r * 4, v + r * 4, t, out + r * 4);
            }
            r += 2;
            continue;
        }
        for (int k = 0; k < 4; k++) {
            size_t i = r * 4 + k;
            out[i] = lerp ? Lerp(a[i], v[i], t) : v[i];
        }
    }
}
static bool g_subOn;             // shadow map reuse: after the skipped shadow passes, sample the copies (g_subFrom -> To)
static int g_subN;
static void *g_subFrom[8], *g_subTo[8];
// this tick's reuse (PrepareShadowReuse): the surfaces whose draws are skipped, from where the copies are sampled
static bool g_reuseOn;
static uint32_t g_reusePoint;
static int g_rN;
static void* g_rSurf[8];
static inline bool Reused(void* rt0) {
    for (int i = 0; i < g_rN; i++)
        if (g_rSurf[i] == rt0) return true;
    return false;
}
static void Exec(const Cmd* c, const Cmd* pc, float t, const Mask256* lm, const float* synthPrev, int boneBase, const float* pre = nullptr) {
    Dev* d = g_dev;
    const uint8_t* p = (const uint8_t*)(c + 1);
    const uint8_t* pp = pc ? (const uint8_t*)(pc + 1) : nullptr;
    const bool flt = C.replay_filter != 0;
    switch (c->op) {
    case OP_RS: {
        auto* x = (const P2*)p;
        if (x->a < 256 ? SameAs(flt, g_st.rsOk[x->a], g_st.rs[x->a], x->b) : false) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetRenderState(d, (D3DRENDERSTATETYPE)x->a, x->b);
        break;
    }
    case OP_SS: {
        auto* x = (const P3*)p;
        int s = SamplerSlot(x->a);
        if (s >= 0 && x->b < 14 ? SameAs(flt, g_st.ssOk[s][x->b], g_st.ss[s][x->b], x->c) : false) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetSamplerState(d, x->a, (D3DSAMPLERSTATETYPE)x->b, x->c);
        break;
    }
    case OP_TSS: {
        auto* x = (const P3*)p;
        if (x->a < 8 && x->b < 33 ? SameAs(flt, g_st.tssOk[x->a][x->b], g_st.tss[x->a][x->b], x->c) : false) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetTextureStageState(d, x->a, (D3DTEXTURESTAGESTATETYPE)x->b, x->c);
        break;
    }
    case OP_TEX: {
        auto* x = (const PPtr*)p;
        void* tx = x->p;
        if (g_subOn)
            for (int i = 0; i < g_subN; i++)
                if (tx == g_subFrom[i]) { tx = g_subTo[i]; break; }
        int s = SamplerSlot(x->a);
        if (s >= 0 ? SameAs(flt, g_st.texOk[s], g_st.tex[s], tx) : false) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetTexture(d, x->a, (IDirect3DBaseTexture9*)tx);
        break;
    }
    case OP_VS: {
        void* v = ((const PPtr*)p)->p;
        if (SameAs(flt, g_st.vsOk, g_st.vs, v)) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetVertexShader(d, (IDirect3DVertexShader9*)v);
        break;
    }
    case OP_PS: {
        void* v = ((const PPtr*)p)->p;
        if (SameAs(flt, g_st.psOk, g_st.ps, v)) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetPixelShader(d, (IDirect3DPixelShader9*)v);
        break;
    }
    case OP_DECL: {
        void* v = ((const PPtr*)p)->p;
        g_st.fvfOk = 0;  // (a declaration replaces the FVF)
        if (SameAs(flt, g_st.declOk, g_st.decl, v)) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetVertexDeclaration(d, (IDirect3DVertexDeclaration9*)v);
        break;
    }
    case OP_FVF: {
        DWORD v = ((const P2*)p)->a;
        g_st.declOk = 0;  // (an FVF replaces the declaration)
        if (SameAs(flt, g_st.fvfOk, g_st.fvf, v)) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetFVF(d, v);
        break;
    }
    case OP_VSCF:
    case OP_PSCF: {
        auto* q = (const PConst*)p;
        const float* v = (const float*)(q + 1);
        if (pre) {
            v = pre;  // (PrecomputeConsts)
        } else if (pp || synthPrev) {
            const float* a = synthPrev ? synthPrev : (const float*)((const PConst*)pp + 1);
            size_t n = q->count * 4;
            if (g_scr.size() < n) g_scr.resize(n);
            LerpConsts(q, v, a, lm, g_curFrz, boneBase, t, g_scr.data(), true, g_curCam, g_curCamMode);
            v = g_scr.data();
        }
        if (ConstUnchanged(c->op == OP_VSCF, q->start, v, q->count)) { St.constSkipped++; break; }
        St.constSent++;
        if (c->op == OP_VSCF) o_SetVertexShaderConstantF(d, q->start, v, q->count);
        else o_SetPixelShaderConstantF(d, q->start, v, q->count);
        break;
    }
    case OP_TEXSUB: {  // the texture was rewritten here: what samples its copy from before samples it as it is now
        auto* x = (const PTexSub*)p;
        for (int s = 0; s < 21; s++) {
            DWORD st = s < 16 ? (DWORD)s : 256u + (DWORD)(s - 16);
            IDirect3DBaseTexture9* cur = nullptr;
            if (FAILED(o_GetTexture(d, st, &cur)) || !cur) continue;
            if ((void*)cur == (void*)x->clone) { o_SetTexture(d, st, x->next); g_st.texOk[s] = 0; }
            cur->Release();
        }
        break;
    }
    case OP_VSCI: { auto* q = (const PConst*)p; o_SetVertexShaderConstantI(d, q->start, (const int*)(q + 1), q->count); break; }
    case OP_VSCB: { auto* q = (const PConst*)p; o_SetVertexShaderConstantB(d, q->start, (const BOOL*)(q + 1), q->count); break; }
    case OP_PSCI: { auto* q = (const PConst*)p; o_SetPixelShaderConstantI(d, q->start, (const int*)(q + 1), q->count); break; }
    case OP_PSCB: { auto* q = (const PConst*)p; o_SetPixelShaderConstantB(d, q->start, (const BOOL*)(q + 1), q->count); break; }
    case OP_SSRC: {
        auto* x = (const PSsrc*)p;
        if (x->stream < 16 && flt && g_st.srcOk[x->stream] && g_st.src[x->stream].vb == (void*)x->vb && g_st.src[x->stream].off == x->off &&
            g_st.src[x->stream].stride == x->stride) { St.stateSkipped++; break; }
        if (x->stream < 16) { g_st.src[x->stream] = {(void*)x->vb, x->off, x->stride}; g_st.srcOk[x->stream] = flt; }
        St.stateSent++;
        o_SetStreamSource(d, x->stream, x->vb, x->off, x->stride);
        break;
    }
    case OP_SSFREQ: { auto* x = (const P2*)p; o_SetStreamSourceFreq(d, x->a, x->b); break; }
    case OP_IDX: {
        void* v = ((const PPtr*)p)->p;
        if (SameAs(flt, g_st.idxOk, g_st.idx, v)) { St.stateSkipped++; break; }
        St.stateSent++;
        o_SetIndices(d, (IDirect3DIndexBuffer9*)v);
        break;
    }
    case OP_RT: { auto* x = (const PPtr*)p; o_SetRenderTarget(d, x->a, (IDirect3DSurface9*)x->p); break; }
    case OP_DS: o_SetDepthStencilSurface(d, (IDirect3DSurface9*)((const PPtr*)p)->p); break;
    case OP_VP: o_SetViewport(d, (const D3DVIEWPORT9*)p); break;
    case OP_SCISSOR: o_SetScissorRect(d, (const RECT*)p); break;
    case OP_XFORM: {
        auto* x = (const PXform*)p;
        if (pp) {
            D3DMATRIX m;
            const float* a = &((const PXform*)pp)->m._11;
            const float* b = &x->m._11;
            for (int i = 0; i < 16; i++) (&m._11)[i] = Lerp(a[i], b[i], t);
            o_SetTransform(d, (D3DTRANSFORMSTATETYPE)x->state, &m);
        } else o_SetTransform(d, (D3DTRANSFORMSTATETYPE)x->state, &x->m);
        break;
    }
    case OP_MULXFORM: { auto* x = (const PXform*)p; o_MultiplyTransform(d, (D3DTRANSFORMSTATETYPE)x->state, &x->m); break; }
    case OP_MAT: o_SetMaterial(d, (const D3DMATERIAL9*)p); break;
    case OP_LIGHT: { auto* x = (const PLight*)p; o_SetLight(d, x->idx, &x->l); break; }
    case OP_LENABLE: { auto* x = (const P2*)p; o_LightEnable(d, x->a, x->b); break; }
    case OP_CLIP: { auto* x = (const PClip*)p; o_SetClipPlane(d, x->idx, x->p); break; }
    case OP_CLEAR: {
        auto* x = (const PClear*)p;
        o_Clear(d, x->count, x->count ? (const D3DRECT*)(x + 1) : nullptr, x->flags, x->color, x->z, x->stencil);
        break;
    }
    case OP_DP: { auto* x = (const PDP*)p; o_DrawPrimitive(d, x->type, x->start, x->count); break; }
    case OP_DIP: { auto* x = (const PDIP*)p; o_DrawIndexedPrimitive(d, x->type, x->base, x->minIdx, x->numV, x->startIdx, x->primCount); break; }
    case OP_DPUP: {
        auto* x = (const PDPUP*)p;
        const uint8_t* v = (const uint8_t*)(x + 1);
        if (pp) {
            if (g_vscr.size() < x->vbytes) g_vscr.resize(x->vbytes);
            memcpy(g_vscr.data(), v, x->vbytes);
            LerpVerts(g_vscr.data(), (const uint8_t*)((const PDPUP*)pp + 1), x->vbytes, x->stride, x->posOff, x->posN, t);
            v = g_vscr.data();
        }
        o_DrawPrimitiveUP(d, x->type, x->primCount, v, x->stride);
        g_st.srcOk[0] = 0;  // (D3D9 resets stream 0 after a UP draw)
        break;
    }
    case OP_DIPUP: {
        auto* x = (const PDIPUP*)p;
        const uint8_t* ib = (const uint8_t*)(x + 1);
        UINT ibA = (x->ibytes + 3) & ~3u;
        const uint8_t* v = ib + ibA;
        if (pp) {
            auto* y = (const PDIPUP*)pp;
            if (g_vscr.size() < x->vbytes) g_vscr.resize(x->vbytes);
            memcpy(g_vscr.data(), v, x->vbytes);
            LerpVerts(g_vscr.data(), (const uint8_t*)(y + 1) + ibA, x->vbytes, x->stride, x->posOff, x->posN, t);
            v = g_vscr.data();
        }
        o_DrawIndexedPrimitiveUP(d, x->type, x->minIdx, x->numV, x->primCount, ib, x->fmt, v, x->stride);
        g_st.srcOk[0] = 0;  // (and the indices)
        g_st.idxOk = 0;
        break;
    }
    case OP_STRETCH: {
        auto* x = (const PStretch*)p;
        o_StretchRect(d, x->src, x->hasSr ? &x->sr : nullptr, x->dst, x->hasDr ? &x->dr : nullptr, x->f);
        break;
    }
    case OP_COLORFILL: { auto* x = (const PFill*)p; o_ColorFill(d, x->s, x->hasR ? &x->r : nullptr, x->c); break; }
    case OP_BEGIN: o_BeginScene(d); break;
    case OP_END: o_EndScene(d); break;
    case OP_SWVP: o_SetSoftwareVertexProcessing(d, ((const P2*)p)->a); break;
    case OP_NPATCH: { float f; memcpy(&f, p, 4); o_SetNPatchMode(d, f); break; }
    case OP_SBAPPLY: o_SBApply((IDirect3DStateBlock9*)((const PPtr*)p)->p); ConstShadowReset(); break;
    case OP_SBCAPTURE: o_SBCapture((IDirect3DStateBlock9*)((const PPtr*)p)->p); break;
    }
}

static void SetRTs(IDirect3DSurface9* const* rt, IDirect3DSurface9* ds) {
    if (rt[0]) o_SetRenderTarget(g_dev, 0, rt[0]);
    for (int i = 1; i < 4; i++) o_SetRenderTarget(g_dev, i, rt[i]);
    o_SetDepthStencilSurface(g_dev, ds);
}

// Debug view: draws whose position is not interpolated are rendered with a solid magenta pixel shader.
static IDirect3DPixelShader9* g_dbgPS;
static bool g_dbgPSTried;
typedef HRESULT(WINAPI* AsmShader_t)(LPCSTR, UINT, const void*, void*, DWORD, IUnknown**, IUnknown**);
static IDirect3DPixelShader9* DebugPS() {
    if (g_dbgPS || g_dbgPSTried) return g_dbgPS;
    g_dbgPSTried = true;
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    auto as = dx ? (AsmShader_t)GetProcAddress(dx, "D3DXAssembleShader") : nullptr;
    const char* src = "ps_2_0\ndef c0, 1, 0, 1, 1\nmov oC0, c0\n";
    IUnknown* code = nullptr;
    if (as && SUCCEEDED(as(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
        auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)code)[3]);  // ID3DXBuffer::GetBufferPointer
        g_dev->CreatePixelShader((const DWORD*)getPtr(code), &g_dbgPS);
        code->Release();
    }
    Log("debug pixel shader: %p", (void*)g_dbgPS);
    return g_dbgPS;
}
static inline bool IsDraw(uint16_t op) { return op == OP_DP || op == OP_DIP || op == OP_DPUP || op == OP_DIPUP; }

// --- regrouping: analysis (see Bind)
static inline bool Has(const std::vector<void*>& v, void* x) {
    for (void* y : v)
        if (y == x) return true;
    return false;
}
static void OwnedTextures(const Bind& b, std::vector<void*>& out) {  // the textures whose surfaces b renders to
    for (void* s : b.s) {
        if (!s) continue;
        void* tx = nullptr;
        if (SUCCEEDED(((IDirect3DSurface9*)s)->GetContainer(__uuidof(IDirect3DBaseTexture9), &tx)) && tx) {
            out.push_back(tx);
            ((IUnknown*)tx)->Release();
        }
    }
}
static void RgClose(Regroup& r) {
    if (r.open && r.w.nb >= 2 && r.w.passes >= (uint32_t)r.w.nb + 2) {
        r.wins.push_back(r.w);
        (r.live ? St.rgLWins : St.rgWins)++;
        (r.live ? St.rgLIn : St.rgIn) += r.w.passes;
        (r.live ? St.rgLOut : St.rgOut) += r.w.nb;
    }
    r.open = false;
}
static void RgBarrier(Regroup& r, uint32_t at) {  // nothing before `at` may move past it
    RgClose(r);
    r.lastEnd = at;
    r.lastBind = r.cur;
}
static void RgEvent(Regroup& r, uint32_t end, bool draw) {
    const Bind B = r.cur;
    if (r.dead) { r.lastEnd = end; r.lastBind = B; return; }
    void* T[24];  // textures this event samples
    int nt = 0;
    if (draw) {
        uint16_t m = PsSamplers(r.ps);
        for (int s = 0; s < 16; s++)
            if (((m >> s) & 1) && r.tex[s]) T[nt++] = r.tex[s];
        for (int s = 16; s < 21; s++)  // vertex texture samplers: in use whenever something is bound
            if (r.tex[s]) T[nt++] = r.tex[s];
    }
    if (r.open) {
        int k = -1;
        for (int i = 0; i < r.w.nb; i++)
            if (r.w.b[i] == B) { k = i; break; }
        size_t ownedBefore = r.owned.size();
        bool ok = true;
        int why = 0;  // 1 = a 5th target, 2 = shares a surface, 3 = renders to what was sampled, 4 = samples what is rendered
        if (k < 0) {
            ok = r.w.nb < 4;
            if (!ok) why = 1;
            for (int i = 0; ok && i < r.w.nb; i++) ok = r.w.b[i].Disjoint(B);
            if (!ok && !why) why = 2;
            if (ok) {
                OwnedTextures(B, r.owned);
                for (size_t i = ownedBefore; ok && i < r.owned.size(); i++) ok = !Has(r.sampled, r.owned[i]);
                if (!ok) why = 3;
            }
        }
        for (int i = 0; ok && i < nt; i++) ok = !Has(r.owned, T[i]);
        if (!ok && !why) why = 4;
        if (!ok && !r.live && r.w.nb >= 2) St.rgEnd[why]++;
        if (ok) {
            if (k < 0) { k = r.w.nb++; r.w.b[k] = B; }
            if (B != r.prevEv) r.w.passes++;
            r.prevEv = B;
            r.w.events++;
            r.w.end = r.w.lastEv[k] = end;
            for (int i = 0; i < nt; i++)
                if (r.sampled.empty() || r.sampled.back() != T[i]) r.sampled.push_back(T[i]);
            r.lastEnd = end;
            r.lastBind = B;
            return;
        }
        r.owned.resize(ownedBefore);
        RgClose(r);
    }
    // a new window, from just after the previous event
    r.owned.clear();
    r.sampled.clear();
    OwnedTextures(B, r.owned);
    bool feedback = false;
    for (int i = 0; i < nt; i++) feedback |= Has(r.owned, T[i]);
    if (feedback && !r.live) St.rgEnd[5]++;
    if (!feedback) {
        r.open = true;
        r.w.beg = r.lastEnd;
        r.w.start = r.lastBind;
        r.w.b[0] = B;
        r.w.nb = 1;
        r.w.passes = r.w.events = 1;
        r.w.end = r.w.lastEv[0] = end;
        r.prevEv = B;
        for (int i = 0; i < nt; i++) r.sampled.push_back(T[i]);
    }
    r.lastEnd = end;
    r.lastBind = B;
}
static void RgAnalyze(const Frame& f, Regroup& r, uint32_t to) {
    const uint8_t* b = f.buf.data();
    while (r.off < to) {
        const Cmd* c = (const Cmd*)(b + r.off);
        const uint8_t* p = (const uint8_t*)(c + 1);
        uint32_t end = r.off + c->size;
        switch (c->op) {
        case OP_RT: {
            auto* x = (const PPtr*)p;
            if (x->a < 4 && (x->a != 0 || x->p)) r.cur.s[x->a] = x->p;
            break;
        }
        case OP_DS: r.cur.s[4] = ((const PPtr*)p)->p; break;
        case OP_TEX: {
            auto* x = (const PPtr*)p;
            int s = SamplerSlot(x->a);
            if (s >= 0) r.tex[s] = x->p;
            break;
        }
        case OP_PS: r.ps = ((const PPtr*)p)->p; break;
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP: RgEvent(r, end, true); break;
        case OP_CLEAR: RgEvent(r, end, false); break;
        case OP_SBAPPLY: RgBarrier(r, end); r.dead = true; break;  // (what is bound is unknown from here on)
        case OP_STRETCH: case OP_COLORFILL: case OP_SBCAPTURE: case OP_BEGIN: case OP_END: case OP_TEXSUB: RgBarrier(r, end); break;
        }
        r.off = end;
    }
}
static void RgFinish(const Frame& f, Regroup& r) {
    if (!f.valid) return;
    uint32_t n = (uint32_t)f.buf.size();
    if (r.off < n || r.open) {
        RgAnalyze(f, r, n);
        RgBarrier(r, n);
    }
}

// --- execution of a recording: in-between frames (p = the previous frame, interpolated) and the deferred game frame
static uint64_t g_runCmds;  // commands executed (stats)
// shadow map reuse (an in-between frame): constants that place shadow lookups keep the game frame's values
static bool g_frzOn;
static std::vector<uint8_t> g_frzHas;                 // per 4 bytes of the recording: a command with frozen registers
static std::unordered_map<uint32_t, Mask256> g_frzMask;  // its registers (absolute)
struct RunCtx {
    const Frame* f;
    const Frame* p;
    float t;
    size_t pi, di;  // next pair, draw index
    IDirect3DPixelShader9 *dbg, *curPS;
    void* mainRT;
    bool live = false;  // the deferred game frame (else an in-between frame)
    void* rt0 = nullptr;  // render target 0 (shadow map reuse: draws into a reused surface are skipped)
    void* ds = nullptr;   // (the game frame: depth-stencil)
};
// In-between frames: every interpolated constant packet of the frame blended first, spread over the worker threads
// (bone slerps are most of it in a crowd); the replay then only sends them. g_pairsGen: the matching they belong to.
static uint64_t g_pvGen;
static std::vector<uint32_t> g_pvOff;   // per pair: where its values are in g_pv (NONE: not a constant packet)
static std::vector<uint32_t> g_pvItems;  // the pairs that have some
static std::vector<float> g_pv;
static bool g_pvReady;                  // computed for the replay that runs
static std::vector<const CamCtx*> g_pairCam;  // per pair: its camera context, when that has a path
static std::vector<uint8_t> g_pairCamMode;    // 1 the whole matrix follows it, 2 a camera-facing card: its anchor only
// A camera matrix uploaded once per pass and read by many draws (MHF's view-projection): one witness, so no context
// estimate - but it is the camera itself, so its own motion (prev * cur^-1) is the camera's and it follows that path
// (g_pktCam, declared with CameraPathsAt).
static uint64_t g_pairCamGen;
// A camera-facing card (flame, glow, foliage billboard) is turned to the camera every frame: its local axes map to
// clip x / y only (the test SynthesizePrev uses). Its anchor sits in the world, its orientation stays on the screen.
static bool CameraFacing(const M4& m) {
    double sx = std::fabs(m.m[0][0]);
    return sx > 1e-6 && std::fabs(m.m[0][2]) < 1e-3 * sx && std::fabs(m.m[1][0]) < 1e-3 * sx && std::fabs(m.m[3][0]) < 1e-3 * sx;
}
// Only what sits still under the estimate (prev = D * cur) follows the path - exactly, then. Anything that moves by
// itself keeps the plain blend (an estimate taken over by a group of props moving together then misleads nothing).
static void PairCameras(const Frame& f, const Frame& p, float t) {  // (each replay: the paths at t)
    if (g_pairCamGen != g_pairsGen) {
        g_pairCamGen = g_pairsGen;
        g_pairCam.assign(g_pairs.size(), nullptr);
        g_pairCamMode.assign(g_pairs.size(), 0);
        g_pktCam.clear();
        // readers of each positional upload (by its ordinal): a shared camera matrix has many. Draws that share one
        // owner list (nothing uploaded between them) count through it once per draw.
        static std::vector<uint16_t> readers;
        readers.assign(C.cam_paths ? f.nvscf : 0, 0);
        if (C.cam_paths) {
            uint32_t lastBegin = NONE, ords[16];
            uint8_t n = 0;
            for (size_t d = 0; d < f.draws.size() && d < g_ownN.count.size(); d++) {
                if (g_ownN.begin[d] != lastBegin || g_ownN.count[d] != n) {  // (else the same list as the draw before)
                    lastBegin = g_ownN.begin[d];
                    n = (uint8_t)std::min<uint32_t>(g_ownN.count[d], 16);
                    for (uint32_t k = 0; k < n; k++)
                        ords[k] = g_ownN.info[lastBegin + k].ord;
                }
                for (uint32_t k = 0; k < n; k++)
                    if (ords[k] < readers.size() && readers[ords[k]] < 0xFFFF) readers[ords[k]]++;
            }
        }
        for (size_t i = 0; i < g_pairs.size(); i++) {
            const Pair& pr = g_pairs[i];
            if (!pr.camKey || pr.upSynth != NONE) continue;
            auto it = g_cam.find(pr.camKey);
            bool ctxFits = false;  // (the context's path: for what sits still under its estimate, or a camera-facing card)
            if (it != g_cam.end() && it->second.path) {
                const float* a = pr.synth != NONE ? &g_synth[pr.synth]
                                                  : (const float*)((const PConst*)((const Cmd*)(p.buf.data() + pr.prev) + 1) + 1);
                const float* v = (const float*)((const PConst*)((const Cmd*)(f.buf.data() + pr.cur) + 1) + 1);
                M4 Mp = FromRegs(a), Mn = FromRegs(v);
                ctxFits = NormInf(Comb(Mul(it->second.D, Mn), 1, Mp, -1)) <= 2e-3 * std::max(1e-6, NormInf(Mp)) ||
                          (CameraFacing(Mp) && CameraFacing(Mn));
            }
            if (!ctxFits) {  // a widely shared matrix (the camera itself: its context may be another object's) follows its own path
                uint32_t ord = ((const PConst*)((const Cmd*)(f.buf.data() + pr.cur) + 1))->ord;
                if (pr.prev == NONE || pr.synth != NONE || ord >= readers.size() || readers[ord] < 6) continue;
                const float* a = (const float*)((const PConst*)((const Cmd*)(p.buf.data() + pr.prev) + 1) + 1);
                const float* v = (const float*)((const PConst*)((const Cmd*)(f.buf.data() + pr.cur) + 1) + 1);
                CamCtx cx;
                M4 inv;
                if (!Inverse(FromRegs(v), inv)) continue;
                cx.D = Mul(FromRegs(a), inv);
                double nd = NormInf(Comb(cx.D, 1, Ident4(), -1));
                double cosA = (cx.D.m[0][0] + cx.D.m[1][1] + cx.D.m[2][2] + cx.D.m[3][3] - 2.0) * 0.5;
                if (nd < 1e-7 || !(cosA > -0.2 && cosA < 1.0001)) continue;
                if (!Inverse(cx.D, cx.Dinv) || !LogM4(cx.D, cx.LogD)) continue;
                if (NormInf(Comb(ExpM4(cx.LogD, 1.0), 1, cx.D, -1)) > 1e-7 * std::max(1.0, NormInf(cx.D))) continue;
                cx.has = cx.path = true;
                g_pktCam.push_back(std::move(cx));
                g_pairCam[i] = &g_pktCam.back();
                g_pairCamMode[i] = 1;
                St.camPathPairs++;
                St.camShared++;
                continue;
            }
            const float* a = pr.synth != NONE ? &g_synth[pr.synth]
                                              : (const float*)((const PConst*)((const Cmd*)(p.buf.data() + pr.prev) + 1) + 1);
            const float* v = (const float*)((const PConst*)((const Cmd*)(f.buf.data() + pr.cur) + 1) + 1);
            M4 Mp = FromRegs(a), Mn = FromRegs(v);
            M4 diff = Comb(Mul(it->second.D, Mn), 1, Mp, -1);
            uint8_t mode = 0;
            if (NormInf(diff) <= 2e-3 * std::max(1e-6, NormInf(Mp))) mode = 1;
            else if (CameraFacing(Mp) && CameraFacing(Mn)) {  // the anchor (4th column) still under the estimate?
                double e = 0, s = 0;
                for (int r = 0; r < 4; r++) {
                    double dv = 0;
                    for (int k = 0; k < 4; k++) dv += it->second.D.m[r][k] * Mn.m[k][3];
                    e = std::max(e, std::fabs(dv - Mp.m[r][3]));
                    s = std::max(s, std::fabs(Mp.m[r][3]));
                }
                if (e <= 2e-3 * std::max(1e-6, s)) mode = 2;
            }
            if (!mode) continue;
            g_pairCam[i] = &it->second;
            g_pairCamMode[i] = mode;
            St.camPathPairs++;
        }
        // Whatever else sits exactly still under one of those paths (prev = D cur) follows it too: geometry that moves
        // as one must stay one in the in-between frames. Without a camera estimate (MHF's per-object depth mapping spoils
        // the vote), only the shared camera matrices followed their own path while the per-object matrices of the same
        // static scene blended: the two differ halfway, and a decal lying on the ground drawn the other way (the grass
        // fringe along a path) lost the depth test in patches - a flicker along the seams whenever the camera moved.
        // (Of 270 such matrices, 267 fit the shared matrix's motion exactly.) Also another target's pass of the same
        // scene, which has no estimate of its own.
        static std::vector<const CamCtx*> paths;
        paths.clear();
        for (const CamCtx& cx : g_pktCam) paths.push_back(&cx);
        for (const auto& kv : g_cam)
            if (kv.second.path) paths.push_back(&kv.second);
        if (!paths.empty())
            for (size_t i = 0; i < g_pairs.size(); i++) {
                const Pair& pr = g_pairs[i];
                if (!pr.camKey || g_pairCam[i] || pr.upSynth != NONE || pr.prev == NONE || pr.synth != NONE) continue;
                M4 Mp = FromRegs((const float*)((const PConst*)((const Cmd*)(p.buf.data() + pr.prev) + 1) + 1));
                M4 Mn = FromRegs((const float*)((const PConst*)((const Cmd*)(f.buf.data() + pr.cur) + 1) + 1));
                double tol = 2e-3 * std::max(1e-6, NormInf(Mp));
                for (const CamCtx* cx : paths) {
                    if (NormInf(Comb(Mul(cx->D, Mn), 1, Mp, -1)) > tol) continue;
                    g_pairCam[i] = cx;
                    g_pairCamMode[i] = 1;
                    St.camPathPairs++;
                    St.camJoined++;
                    break;
                }
            }
    }
    CameraPathsAt(t);
}
static void PrecomputeConsts(const Frame& f, const Frame& p, float t) {
    if (g_pvGen != g_pairsGen) {
        g_pvGen = g_pairsGen;
        g_pvOff.assign(g_pairs.size(), NONE);
        g_pvItems.clear();
        size_t n = 0;
        for (size_t i = 0; i < g_pairs.size(); i++) {
            const Pair& pr = g_pairs[i];
            if (pr.upSynth != NONE || (pr.synth == NONE && pr.prev == NONE)) continue;
            const Cmd* c = (const Cmd*)(f.buf.data() + pr.cur);
            if (c->op != OP_VSCF && c->op != OP_PSCF) continue;
            g_pvOff[i] = (uint32_t)n;
            g_pvItems.push_back((uint32_t)i);
            n += ((const PConst*)(c + 1))->count * 4;
        }
        g_pv.resize(n);
    }
    const int kChunk = 32;
    auto job = [&](int ch, int) {
        for (size_t k = (size_t)ch * kChunk; k < g_pvItems.size() && k < (size_t)(ch + 1) * kChunk; k++) {
            uint32_t i = g_pvItems[k];
            const Pair& pr = g_pairs[i];
            auto* q = (const PConst*)((const Cmd*)(f.buf.data() + pr.cur) + 1);
            const float* a = pr.synth != NONE ? &g_synth[pr.synth] : (const float*)((const PConst*)((const Cmd*)(p.buf.data() + pr.prev) + 1) + 1);
            LerpConsts(q, (const float*)(q + 1), a, pr.mi != NONE ? &g_pairMask[pr.mi] : nullptr, nullptr, pr.boneBase, t, &g_pv[g_pvOff[i]], false,
                       g_pairCam[i], g_pairCamMode[i]);
        }
    };
    ParallelFor((int)((g_pvItems.size() + kChunk - 1) / kChunk), job);
    g_pvReady = true;
}
static void RunCmd(RunCtx& x, uint32_t off, bool exec) {
    const Cmd* c = (const Cmd*)(x.f->buf.data() + off);
    const Cmd* pc = nullptr;
    const Mask256* lm = nullptr;
    const float* sp = nullptr;
    const float* pre = nullptr;
    int bb = -1;
    if (x.p) {
        // a duplicate or out-of-order pair must not stall the cursor (everything after it would replay un-blended)
        while (x.pi < g_pairs.size() && g_pairs[x.pi].cur < off) x.pi++;
        if (x.pi < g_pairs.size() && g_pairs[x.pi].cur == off) {
            if (g_pvReady && g_pvOff[x.pi] != NONE) pre = &g_pv[g_pvOff[x.pi]];
            g_curCam = x.pi < g_pairCam.size() ? g_pairCam[x.pi] : nullptr;
            g_curCamMode = x.pi < g_pairCamMode.size() ? g_pairCamMode[x.pi] : 0;
            const Pair& pr = g_pairs[x.pi++];
            bb = pr.boneBase;
            if (pr.synth != NONE) sp = &g_synth[pr.synth];
            else if (pr.upSynth != NONE) pc = (const Cmd*)(g_upSynth.data() + pr.upSynth);
            else pc = (const Cmd*)(x.p->buf.data() + pr.prev);
            if (pr.mi != NONE) lm = &g_pairMask[pr.mi];
            if (x.mainRT && x.di < x.f->draws.size() && ((g_variant == 5) == (x.f->draws[x.di].rt != x.mainRT))) { pc = nullptr; sp = nullptr; }
            if (!pc && !sp) pre = nullptr;
        }
    }
    bool draw = IsDraw(c->op);
    g_runCmds += exec;
    g_curFrz = nullptr;
    if (!pc && !sp) g_curCam = nullptr;
    if (g_frzOn && (c->op == OP_VSCF || c->op == OP_PSCF) && (off >> 2) < g_frzHas.size() && g_frzHas[off >> 2]) {
        auto it = g_frzMask.find(off);
        if (it != g_frzMask.end()) { g_curFrz = &it->second; pre = nullptr; }  // (computed without frozen registers)
    }
    if (exec) {
        if (c->op == OP_PS) x.curPS = (IDirect3DPixelShader9*)((const PPtr*)(c + 1))->p;
        if (x.dbg && draw && x.di < g_drawFlag.size() && g_drawFlag[x.di]) {
            o_SetPixelShader(g_dev, x.dbg);
            Exec(c, pc, x.t, lm, sp, bb, pre);
            o_SetPixelShader(g_dev, x.curPS);
        } else {
            Exec(c, pc, x.t, lm, sp, bb, pre);
        }
    }
    if (draw) {
        if (exec && g_trSide > 0) TraceAfterDraw(g_trSide);
        x.di++;
    }
}

// One window, target after target. Each pass starts from the state the window started with (a state block) and
// runs its list: its own draws and clears, and of the state commands before them only those whose value one of them
// still sees (BuildPassLists). The target switches are not executed - the pass's target stays bound - but their side
// effect is, where it matters: setting render target 0 resets the viewport and scissor rect to the whole target
// (D3D9 rule). The last pass also runs every state command whose value outlasts the window, so afterwards the state
// is what the window leaves in recorded order; then the final targets, viewport and scissor rect, and the stream 0 /
// index buffer resets of UP draws the last pass skipped.
static IDirect3DStateBlock9* g_rgSB;
static void BindTargets(const Bind& b) {
    if (b.s[0]) o_SetRenderTarget(g_dev, 0, (IDirect3DSurface9*)b.s[0]);
    for (int i = 1; i < 4; i++) o_SetRenderTarget(g_dev, i, (IDirect3DSurface9*)b.s[i]);
    o_SetDepthStencilSurface(g_dev, (IDirect3DSurface9*)b.s[4]);
}

// State keys for the dead-command scan: a command is dead for a pass when every state it sets is set again before
// the pass's next draw or clear.
enum : int {
    kKeyRS = 0, kKeySS = 256, kKeyTSS = kKeySS + 21 * 14, kKeyTex = kKeyTSS + 8 * 33, kKeyVS = kKeyTex + 21, kKeyPS,
    kKeyVDecl, kKeyIdx, kKeyVP, kKeySc, kKeyMat, kKeySwvp, kKeyNPatch, kKeySrc, kKeyFreq = kKeySrc + 16,
    kKeyXf = kKeyFreq + 16, kKeyLight = kKeyXf + 512, kKeyLEn = kKeyLight + 64, kKeyClip = kKeyLEn + 64,
    kKeyVSCF = kKeyClip + 32, kKeyPSCF = kKeyVSCF + 256, kKeyVSCI = kKeyPSCF + 256, kKeyVSCB = kKeyVSCI + 16,
    kKeyPSCI = kKeyVSCB + 16, kKeyPSCB = kKeyPSCI + 16, kKeys = kKeyPSCB + 16
};
// What a state command sets (StateKeyOf), decoded once; whether it is live for a pass (LiveFor: per pass, the keys
// set again since its next event, as stamps - a new stamp at each of its events - and the float constants as bits).
struct SKey {
    uint8_t type;  // 0 always runs, 1 one key, 2/3 VS/PS float constants [start, start+count), 4 int/bool constants
                   // (key = base, 16 of them), 5 reads the key's earlier value (MultiplyTransform): that one runs too
    uint32_t key, start, count;
};
static inline SKey StateKeyOf(const Cmd* c) {
    const uint8_t* p = (const uint8_t*)(c + 1);
    auto one = [](uint32_t k) { return SKey{1, k, 0, 0}; };
    const SKey always{0, 0, 0, 0};
    switch (c->op) {
    case OP_RS: { auto* x = (const P2*)p; return x->a < 256 ? one(kKeyRS + x->a) : always; }
    case OP_SS: { auto* x = (const P3*)p; int s = SamplerSlot(x->a); return s >= 0 && x->b < 14 ? one(kKeySS + s * 14 + x->b) : always; }
    case OP_TSS: { auto* x = (const P3*)p; return x->a < 8 && x->b < 33 ? one(kKeyTSS + x->a * 33 + x->b) : always; }
    case OP_TEX: { int s = SamplerSlot(((const PPtr*)p)->a); return s >= 0 ? one(kKeyTex + s) : always; }
    case OP_VS: return one(kKeyVS);
    case OP_PS: return one(kKeyPS);
    case OP_DECL: case OP_FVF: return one(kKeyVDecl);  // (each replaces the other)
    case OP_IDX: return one(kKeyIdx);
    case OP_VP: return one(kKeyVP);
    case OP_SCISSOR: return one(kKeySc);
    case OP_MAT: return one(kKeyMat);
    case OP_SWVP: return one(kKeySwvp);
    case OP_NPATCH: return one(kKeyNPatch);
    case OP_SSRC: { UINT s = ((const PSsrc*)p)->stream; return s < 16 ? one(kKeySrc + s) : always; }
    case OP_SSFREQ: { DWORD s = ((const P2*)p)->a; return s < 16 ? one(kKeyFreq + s) : always; }
    case OP_XFORM: { DWORD s = ((const PXform*)p)->state; return s < 512 ? one(kKeyXf + s) : always; }
    case OP_MULXFORM: { DWORD s = ((const PXform*)p)->state; return s < 512 ? SKey{5, kKeyXf + s, 0, 0} : always; }
    case OP_LIGHT: { DWORD i = ((const PLight*)p)->idx; return i < 64 ? one(kKeyLight + i) : always; }
    case OP_LENABLE: { DWORD i = ((const P2*)p)->a; return i < 64 ? one(kKeyLEn + i) : always; }
    case OP_CLIP: { DWORD i = ((const PClip*)p)->idx; return i < 32 ? one(kKeyClip + i) : always; }
    case OP_VSCF: { auto* q = (const PConst*)p; return SKey{2, 0, q->start, q->count}; }
    case OP_PSCF: { auto* q = (const PConst*)p; return SKey{3, 0, q->start, q->count}; }
    case OP_VSCI: { auto* q = (const PConst*)p; return SKey{4, kKeyVSCI, q->start, q->count}; }
    case OP_VSCB: { auto* q = (const PConst*)p; return SKey{4, kKeyVSCB, q->start, q->count}; }
    case OP_PSCI: { auto* q = (const PConst*)p; return SKey{4, kKeyPSCI, q->start, q->count}; }
    case OP_PSCB: { auto* q = (const PConst*)p; return SKey{4, kKeyPSCB, q->start, q->count}; }
    default: return always;
    }
}
static uint32_t g_pStamp[4][kKeys], g_pNow[4], g_pGen;
static uint64_t g_pRegs[4][2][4];
static inline void PassEvent(int k) {  // an event of pass k reads everything: nothing is set again after it yet
    g_pNow[k] = ++g_pGen;
    memset(g_pRegs[k], 0, sizeof g_pRegs[k]);
}
static bool LiveFor(int k, const SKey& s) {
    uint32_t* st = g_pStamp[k];
    uint32_t now = g_pNow[k];
    switch (s.type) {
    case 1: {
        bool live = st[s.key] != now;
        st[s.key] = now;
        return live;
    }
    case 2: case 3: {
        if (s.start >= 256) return true;
        uint64_t* set = g_pRegs[k][s.type - 2];
        UINT end = std::min<UINT>(256, s.start + s.count);
        bool live = s.start + s.count > 256;  // (out of range: always run)
        for (UINT w = s.start >> 6; w <= (end - 1) >> 6; w++) {
            UINT lo = std::max<UINT>(s.start, w * 64) - w * 64, hi = std::min<UINT>(end, w * 64 + 64) - w * 64;
            uint64_t m = (hi - lo == 64) ? ~0ull : (((1ull << (hi - lo)) - 1) << lo);
            live |= (m & ~set[w]) != 0;
            set[w] |= m;
        }
        return live;
    }
    case 4: {
        if (s.start >= 16 || s.count > 16 - s.start) {
            for (UINT r = s.start; r < 16 && r < s.start + s.count; r++) st[s.key + r] = now;
            return true;
        }
        bool live = false;
        for (UINT r = s.start; r < s.start + s.count; r++) {
            live |= st[s.key + r] != now;
            st[s.key + r] = now;
        }
        return live;
    }
    case 5:
        st[s.key] = 0;
        return true;
    default:
        return true;
    }
}
static void BuildPassLists(const Frame& f, const RWin& w) {
    int64_t t0 = Qpc();
    // every command of the window, and what it is: -1 state, -2 not run (other targets, depth-stencil),
    // -3 render target 0 switch, k >= 0 a draw or clear of pass k
    static std::vector<uint32_t> offs;
    static std::vector<int8_t> kind;
    offs.clear();
    kind.clear();
    offs.reserve((w.end - w.beg) / 16);
    kind.reserve((w.end - w.beg) / 16);
    const uint8_t* buf = f.buf.data();
    Bind cur = w.start;
    for (uint32_t off = w.beg; off < w.end;) {
        const Cmd* c = (const Cmd*)(buf + off);
        const uint8_t* p = (const uint8_t*)(c + 1);
        int8_t k = -1;
        switch (c->op) {
        case OP_RT: {
            auto* q = (const PPtr*)p;
            k = -2;
            if (q->a < 4 && (q->a != 0 || q->p)) {
                cur.s[q->a] = q->p;
                if (q->a == 0) k = -3;
            }
            break;
        }
        case OP_DS: cur.s[4] = ((const PPtr*)p)->p; k = -2; break;
        case OP_CLEAR: case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP:
            k = -2;  // (an event of no pass cannot happen: windows are built from these bindings)
            for (int i = 0; i < w.nb; i++)
                if (w.b[i] == cur) { k = (int8_t)i; break; }
            break;
        }
        offs.push_back(off);
        kind.push_back(k);
        off += c->size;
    }
    w.endBind = cur;
    // all passes in one walk backwards: a pass's event reads everything; a state command is live for a pass unless
    // its states are set again before that pass's next event. (Pass k only sees commands before its last event, but
    // the last pass sees them all: it also sets the state the window leaves.)
    size_t stop[4];
    for (int k = 0; k < w.nb; k++) {
        w.list[k].clear();
        stop[k] = k == w.nb - 1 ? offs.size() : (size_t)(std::lower_bound(offs.begin(), offs.end(), w.lastEv[k]) - offs.begin());
        PassEvent(k);
    }
    for (size_t i = offs.size(); i-- > 0;) {
        int8_t kd = kind[i];
        if (kd >= 0) {
            if (i < stop[kd]) { w.list[kd].push_back(offs[i]); PassEvent(kd); }
            continue;
        }
        if (kd == -2) continue;
        if (kd == -3) {  // resets viewport and scissor rect
            for (int k = 0; k < w.nb; k++) {
                if (i >= stop[k]) continue;
                bool live = g_pStamp[k][kKeyVP] != g_pNow[k] || g_pStamp[k][kKeySc] != g_pNow[k];
                g_pStamp[k][kKeyVP] = g_pStamp[k][kKeySc] = g_pNow[k];
                if (live) w.list[k].push_back(offs[i] | 1);
            }
            continue;
        }
        SKey sk = StateKeyOf((const Cmd*)(buf + offs[i]));
        for (int k = 0; k < w.nb; k++)
            if (i < stop[k] && LiveFor(k, sk)) w.list[k].push_back(offs[i]);
    }
    for (int k = 0; k < w.nb; k++) std::reverse(w.list[k].begin(), w.list[k].end());
    {  // the stream 0 / index buffer resets of UP draws when none of a pass's draws run (shadow map reuse)
        w.s0ResetAll = w.idxResetAll = false;
        for (size_t i = offs.size(); i-- > 0;) {
            uint16_t op = ((const Cmd*)(buf + offs[i]))->op;
            if (op == OP_SSRC && ((const PSsrc*)((const Cmd*)(buf + offs[i]) + 1))->stream == 0) break;
            if (op == OP_DPUP || op == OP_DIPUP) { w.s0ResetAll = true; break; }
        }
        for (size_t i = offs.size(); i-- > 0;) {
            uint16_t op = ((const Cmd*)(buf + offs[i]))->op;
            if (op == OP_IDX) break;
            if (op == OP_DIPUP) { w.idxResetAll = true; break; }
        }
    }
    // the stream 0 / index buffer resets of UP draws the last pass does not run
    w.s0Reset = w.idxReset = false;
    int last = w.nb - 1;
    for (size_t i = offs.size(); i-- > 0;) {
        uint16_t op = ((const Cmd*)(buf + offs[i]))->op;
        if (op == OP_SSRC && ((const PSsrc*)((const Cmd*)(buf + offs[i]) + 1))->stream == 0) break;
        if (op == OP_DPUP || op == OP_DIPUP) { w.s0Reset = kind[i] != last; break; }
    }
    for (size_t i = offs.size(); i-- > 0;) {
        uint16_t op = ((const Cmd*)(buf + offs[i]))->op;
        if (op == OP_IDX) break;
        if (op == OP_DIPUP) { w.idxReset = kind[i] != last; break; }
    }
    w.built = true;
    St.rgBuilds++;
    St.rgBuildMs += Ms(Qpc() - t0);
}
static size_t DrawIdx(const Frame& f, uint32_t off) {  // index of the first draw at or after off
    return std::lower_bound(f.draws.begin(), f.draws.end(), off, [](const DrawRec& d, uint32_t o) { return d.off < o; }) - f.draws.begin();
}
static void RunWindow(RunCtx& x, const RWin& w) {
    const uint8_t* buf = x.f->buf.data();
    if (!g_rgSB && FAILED(o_CreateStateBlock(g_dev, D3DSBT_ALL, &g_rgSB))) g_rgSB = nullptr;
    if (!g_rgSB) {  // (no state block: in recorded order)
        for (uint32_t off = w.beg; off < w.end; off += ((const Cmd*)(buf + off))->size) RunCmd(x, off, true);
        return;
    }
    if (!w.built) BuildPassLists(*x.f, w);
    bool needDi = x.dbg || x.mainRT || g_trSide > 0;  // (debug features that look draws up by index)
    o_SBCapture(g_rgSB);
    D3DVIEWPORT9 vp0, vp;
    RECT sc0, sc;
    g_dev->GetViewport(&vp0);
    g_dev->GetScissorRect(&sc0);
    const RunCtx x0 = x;
    bool reuse = g_reuseOn && !x.live && x.p;
    for (int k = 0; k < w.nb; k++) {
        bool last = k == w.nb - 1;
        // shadow map reuse: this surface's draws are skipped (the last pass still sets the state the window leaves)
        bool skipEv = reuse && w.beg < g_reusePoint && Reused(w.b[k].s[0]);
        if (skipEv && !last) continue;
        if (k > 0) {
            o_SBApply(g_rgSB);
            ConstShadowReset();
        }
        BindTargets(w.b[k]);
        o_SetViewport(g_dev, &vp0);
        o_SetScissorRect(g_dev, &sc0);
        x = x0;
        vp = vp0;
        sc = sc0;
        for (uint32_t e : w.list[k]) {
            uint32_t off = e & ~1u;
            const Cmd* c = (const Cmd*)(buf + off);
            const uint8_t* p = (const uint8_t*)(c + 1);
            if (e & 1) {
                D3DSURFACE_DESC d;
                if (SUCCEEDED(((IDirect3DSurface9*)((const PPtr*)p)->p)->GetDesc(&d))) {
                    vp = {0, 0, d.Width, d.Height, 0.0f, 1.0f};
                    sc = {0, 0, (LONG)d.Width, (LONG)d.Height};
                    o_SetViewport(g_dev, &vp);
                    o_SetScissorRect(g_dev, &sc);
                }
                continue;
            }
            if (c->op == OP_VP) vp = *(const D3DVIEWPORT9*)p;
            else if (c->op == OP_SCISSOR) sc = *(const RECT*)p;
            else if (needDi && IsDraw(c->op)) x.di = DrawIdx(*x.f, off);
            RunCmd(x, off, !(skipEv && (IsDraw(c->op) || c->op == OP_CLEAR)));
        }
        if (skipEv) {  // (its UP draws did not reset stream 0 / the indices)
            if (w.s0ResetAll) { o_SetStreamSource(g_dev, 0, nullptr, 0, 0); g_st.srcOk[0] = 0; }
            if (w.idxResetAll) { o_SetIndices(g_dev, nullptr); g_st.idxOk = 0; }
        }
    }
    if (!(reuse && w.beg < g_reusePoint && Reused(w.b[w.nb - 1].s[0]))) {
        if (w.s0Reset) { o_SetStreamSource(g_dev, 0, nullptr, 0, 0); g_st.srcOk[0] = 0; }
        if (w.idxReset) { o_SetIndices(g_dev, nullptr); g_st.idxOk = 0; }
    }
    BindTargets(w.endBind);
    o_SetViewport(g_dev, &vp);
    o_SetScissorRect(g_dev, &sc);
    x.di = DrawIdx(*x.f, w.end);
    x.rt0 = w.endBind.s[0];
    x.ds = w.endBind.s[4];
}
// The in-between frames' windows that are the game frame's too take its lists (built while it ran).
static void ShareLists(Frame& f) {
    size_t j = 0;
    for (RWin& w : f.rgR.wins) {
        while (j < f.rg.wins.size() && f.rg.wins[j].beg < w.beg) j++;
        if (j < f.rg.wins.size() && f.rg.wins[j].built && f.rg.wins[j].beg == w.beg && f.rg.wins[j].end == w.end && f.rg.wins[j].nb == w.nb) {
            const RWin& s = f.rg.wins[j];
            for (int k = 0; k < w.nb; k++) w.list[k] = s.list[k];
            w.s0Reset = s.s0Reset;
            w.idxReset = s.idxReset;
            w.endBind = s.endBind;
            w.built = true;
        }
    }
}
// --- shadow map reuse
// A window drawn into 2-4 square textures of one size and format (MHF: the three shadow cascades, 3440 x 3440 at
// 3440 pixels wide) is a candidate. In the game frame its targets are copied after it (TakeSnapshot: one copy per
// surface, refreshed after every such window; g_snapPoint = the end of the last one). An in-between frame then
// skips every draw and clear into those surfaces (all of them come before that point, PrepareShadowReuse checks),
// and from that point on samples the copies - the game frame's shadows - with the constants that place those
// lookups at the game frame's values. Shadows of moving objects update with the game (30 times a second).
// the window's targets that look like shadow cascades (a bit per pass): square textures of one size and format, at
// least two of them (other targets in the same window are left alone)
static int ShadowTargets(const RWin& w) {
    if (w.shadowCand >= 0) return w.shadowCand;
    int best = 0, bestN = 0;
    D3DSURFACE_DESC dk[4] = {};
    bool ok[4] = {};
    for (int k = 0; k < w.nb; k++) {
        if (!w.b[k].s[0] || w.b[k].s[1] || w.b[k].s[2] || w.b[k].s[3]) continue;
        if (FAILED(((IDirect3DSurface9*)w.b[k].s[0])->GetDesc(&dk[k])) || dk[k].Width != dk[k].Height || dk[k].Width < 256) continue;
        void* tx = nullptr;
        if (FAILED(((IDirect3DSurface9*)w.b[k].s[0])->GetContainer(__uuidof(IDirect3DTexture9), &tx)) || !tx) continue;
        ((IUnknown*)tx)->Release();
        ok[k] = true;
    }
    for (int k = 0; k < w.nb; k++) {  // the largest group of one size and format
        if (!ok[k]) continue;
        int m = 0, n = 0;
        for (int j = 0; j < w.nb; j++)
            if (ok[j] && dk[j].Width == dk[k].Width && dk[j].Format == dk[k].Format) { m |= 1 << j; n++; }
        if (n > bestN) { best = m; bestN = n; }
    }
    w.shadowCand = (int8_t)(bestN >= 2 ? best : 0);
    return w.shadowCand;
}
struct ShadowSnap {
    IDirect3DTexture9* tex = nullptr;  // the copy
    IDirect3DSurface9* surf = nullptr;
    void *src = nullptr, *srcTex = nullptr, *ds = nullptr;  // the game's surface, its texture, its depth-stencil
    UINT size = 0;
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    UINT levels = 1;      // the game's texture's mip levels (copied each), or
    bool autogen = false; // generated by the driver from the top level (D3DUSAGE_AUTOGENMIPMAP): the copy too
    uint32_t serial = 0;  // the frame it belongs to
    bool dirty = false;   // drawn since the last copy: copied before anything else changes it (SnapCopyBefore)
};
static ShadowSnap g_snap[8];
static uint32_t g_snapSerial, g_snapPoint;
static bool g_snapFailed;
static void ReleaseSnaps() {
    for (ShadowSnap& s : g_snap) {
        if (s.surf) s.surf->Release();
        if (s.tex) s.tex->Release();
        s = ShadowSnap();
    }
    g_snapSerial = g_snapPoint = 0;
}
static void SnapCopy(ShadowSnap& s) {
    s.dirty = false;
    HRESULT hr = o_StretchRect(g_dev, (IDirect3DSurface9*)s.src, nullptr, s.surf, nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(hr) && s.autogen) s.tex->GenerateMipSubLevels();
    for (UINT l = 1; SUCCEEDED(hr) && !s.autogen && l < s.levels; l++) {  // (mips the game draws itself)
        IDirect3DSurface9 *from = nullptr, *to = nullptr;
        if (SUCCEEDED(((IDirect3DTexture9*)s.srcTex)->GetSurfaceLevel(l, &from)) && SUCCEEDED(s.tex->GetSurfaceLevel(l, &to)))
            hr = o_StretchRect(g_dev, from, nullptr, to, nullptr, D3DTEXF_NONE);
        if (from) from->Release();
        if (to) to->Release();
    }
    if (FAILED(hr)) {
        Log("shadow reuse: copying %ux%u format %d failed - off", s.size, s.size, s.fmt);
        g_snapFailed = true;
        s.serial = 0;
        return;
    }
    St.srSnaps++;
}
// before the game frame changes a shadow map it drew (clears it, draws into it outside such a window, copies to it)
static void SnapCopyBefore(const void* s0, const void* ds) {
    for (ShadowSnap& s : g_snap)
        if (s.dirty && (s.src == s0 || (s.ds && s.ds == ds) || s.src == ds)) SnapCopy(s);
}
static void SnapCopyPending() {  // at the end of the game frame
    for (ShadowSnap& s : g_snap)
        if (s.dirty) SnapCopy(s);
}
// after a window drew shadow maps in the game frame
static void TakeSnapshot(const Frame& f, const RWin& w, int mask) {
    if (g_snapFailed) return;
    if (g_snapSerial != f.serial) {
        g_snapPoint = 0;
        for (ShadowSnap& s : g_snap) s.dirty = false;
    }
    for (int k = 0; k < w.nb; k++) {
        if (!((mask >> k) & 1)) continue;
        auto* src = (IDirect3DSurface9*)w.b[k].s[0];
        ShadowSnap* s = nullptr;
        for (ShadowSnap& x : g_snap)
            if (x.src == src) s = &x;
        for (ShadowSnap& x : g_snap)
            if (!s && x.serial != f.serial) s = &x;  // (a slot not in use in this frame)
        if (!s) return;
        D3DSURFACE_DESC d;
        src->GetDesc(&d);
        // the copy is the texture as the game's shaders sample it: the same mip levels (or generated the same way)
        IDirect3DTexture9* gt = nullptr;
        src->GetContainer(__uuidof(IDirect3DTexture9), (void**)&gt);
        UINT levels = gt ? gt->GetLevelCount() : 1;
        bool autogen = (d.Usage & D3DUSAGE_AUTOGENMIPMAP) != 0;
        if (gt) gt->Release();
        if (s->tex && (s->size != d.Width || s->fmt != d.Format || s->levels != levels || s->autogen != autogen)) {
            s->surf->Release();
            s->tex->Release();
            *s = ShadowSnap();
        }
        if (!s->tex) {
            if (FAILED(g_dev->CreateTexture(d.Width, d.Height, autogen ? 0 : levels, D3DUSAGE_RENDERTARGET | (autogen ? D3DUSAGE_AUTOGENMIPMAP : 0),
                                            d.Format, D3DPOOL_DEFAULT, &s->tex, nullptr)) ||
                FAILED(s->tex->GetSurfaceLevel(0, &s->surf))) {
                Log("shadow reuse: no copy texture (%ux%u format %d) - off", d.Width, d.Height, d.Format);
                if (s->tex) s->tex->Release();
                *s = ShadowSnap();
                g_snapFailed = true;
                return;
            }
            s->size = d.Width;
            s->fmt = d.Format;
            s->levels = levels;
            s->autogen = autogen;
            Log("shadow reuse: copy texture %ux%u format %d, %u mip levels%s, usage %lx", d.Width, d.Height, d.Format, levels,
                autogen ? " (generated)" : "", d.Usage);
        }
        void* tx = nullptr;
        src->GetContainer(__uuidof(IDirect3DTexture9), &tx);
        if (tx) ((IUnknown*)tx)->Release();
        s->src = src;
        s->srcTex = tx;
        s->ds = w.b[k].s[4];
        s->serial = f.serial;
        s->dirty = true;
    }
    g_snapSerial = f.serial;
    g_snapPoint = std::max(g_snapPoint, w.end);
}
static void EndSubstitution(void* surf) {  // the game frame cleared that shadow map: from here on it is used as it is
    void* src = nullptr;
    for (const ShadowSnap& s : g_snap)
        if (s.src == surf) src = s.srcTex;
    if (!src) return;
    void* copy = nullptr;
    for (int i = 0; i < g_subN; i++)
        if (g_subFrom[i] == src) { copy = g_subTo[i]; g_subFrom[i] = nullptr; }
    if (!copy || !g_subOn) return;
    for (int s = 0; s < 21; s++) {
        DWORD st = s < 16 ? (DWORD)s : 256u + (DWORD)(s - 16);
        IDirect3DBaseTexture9* tx = nullptr;
        if (FAILED(g_dev->GetTexture(st, &tx)) || !tx) continue;
        if ((void*)tx == copy) {
            o_SetTexture(g_dev, st, (IDirect3DBaseTexture9*)src);
            g_st.tex[s] = src;
            g_st.texOk[s] = 1;
        }
        tx->Release();
    }
}
static void StartSubstitution() {  // from here on the copies are sampled - also where a shadow map is still bound
    if (g_subOn) return;
    g_subOn = g_frzOn = true;
    for (int s = 0; s < 21; s++) {
        DWORD st = s < 16 ? (DWORD)s : 256u + (DWORD)(s - 16);
        IDirect3DBaseTexture9* tx = nullptr;
        if (FAILED(g_dev->GetTexture(st, &tx)) || !tx) continue;
        for (int i = 0; i < g_subN; i++)
            if ((void*)tx == g_subFrom[i]) {
                o_SetTexture(g_dev, st, (IDirect3DBaseTexture9*)g_subTo[i]);
                g_st.tex[s] = g_subTo[i];
                g_st.texOk[s] = 1;
            }
        tx->Release();
    }
}

// Commands [from, to) in recorded order, except the regrouping windows (wins, from index wi on) that start there.
static void ExecRange(RunCtx& x, uint32_t from, uint32_t to, const std::vector<RWin>* wins, size_t& wi) {
    const uint8_t* b = x.f->buf.data();
    bool reuse = g_reuseOn && !x.live && x.p;
    for (uint32_t off = from; off < to;) {
        while (wins && wi < wins->size() && (*wins)[wi].beg < off) wi++;
        if (wins && wi < wins->size() && (*wins)[wi].beg == off && (*wins)[wi].end <= to) {
            const RWin& w = (*wins)[wi++];
            if (reuse && w.end > g_reusePoint && (off >= g_reusePoint || w.beg < g_reusePoint)) StartSubstitution();
            int mask = x.live && C.shadow_reuse && C.mode == 2 ? ShadowTargets(w) : 0;
            bool clearsAfter = reuse && w.beg >= g_reusePoint;  // (its shadow-map passes can only clear: PrepareShadowReuse)
            if (x.live)  // (a shadow map this window changes without being one of its shadow targets: copy it first)
                for (int k = 0; k < w.nb; k++)
                    if (!((mask >> k) & 1)) SnapCopyBefore(w.b[k].s[0], w.b[k].s[4]);
            RunWindow(x, w);
            if (mask) TakeSnapshot(*x.f, w, mask);
            if (clearsAfter)
                for (int k = 0; k < w.nb; k++)
                    if (Reused(w.b[k].s[0])) EndSubstitution(w.b[k].s[0]);
            off = w.end;
            continue;
        }
        const Cmd* c = (const Cmd*)(b + off);
        bool exec = true;
        if (x.live) {
            if (c->op == OP_RT) {
                auto* q = (const PPtr*)(c + 1);
                if (q->a == 0 && q->p) x.rt0 = q->p;
            } else if (c->op == OP_DS) {
                x.ds = ((const PPtr*)(c + 1))->p;
            } else if (IsDraw(c->op) || c->op == OP_CLEAR) {
                SnapCopyBefore(x.rt0, x.ds);
            } else if (c->op == OP_STRETCH) {
                SnapCopyBefore(((const PStretch*)(c + 1))->dst, nullptr);
            } else if (c->op == OP_COLORFILL) {
                SnapCopyBefore(((const PFill*)(c + 1))->s, nullptr);
            }
        }
        if (reuse) {
            if (off >= g_reusePoint) StartSubstitution();
            if (c->op == OP_RT) {
                auto* q = (const PPtr*)(c + 1);
                if (q->a == 0 && q->p) x.rt0 = q->p;
            } else if ((IsDraw(c->op) || c->op == OP_CLEAR) && off < g_reusePoint && Reused(x.rt0)) {
                exec = false;
            }
        }
        RunCmd(x, off, exec);
        if (reuse && c->op == OP_CLEAR && off >= g_reusePoint && Reused(x.rt0)) EndSubstitution(x.rt0);
        off += c->size;
    }
}

// Commands from `from` on are about to run: a texture bound now that a deferred lock copied later in the frame is
// what the commands up to that lock sampled - its first copy from there on.
static void LockSubBegin(const Frame& f, uint32_t from) {
    void* done[8];
    int nd = 0;
    for (uint32_t off : f.subs) {
        if (off < from) continue;
        auto* x = (const PTexSub*)((const Cmd*)(f.buf.data() + off) + 1);
        bool seen = false;
        for (int i = 0; i < nd; i++) seen |= done[i] == (void*)x->tex;
        if (seen) continue;
        if (nd < 8) done[nd++] = x->tex;
        for (int s = 0; s < 21; s++) {
            DWORD st = s < 16 ? (DWORD)s : 256u + (DWORD)(s - 16);
            IDirect3DBaseTexture9* cur = nullptr;
            if (FAILED(o_GetTexture(g_dev, st, &cur)) || !cur) continue;
            if ((void*)cur == (void*)x->tex) { o_SetTexture(g_dev, st, x->clone); g_st.texOk[s] = 0; }
            cur->Release();
        }
    }
}
static bool g_rgOff;  // regroup check: replay in recorded order
static void Replay(const Frame& f, const Frame* p, float t) {
    PhaseScope ps(2);
    g_boneFrame++;  // (the slerp and packet caches: per in-between frame)
    SetRTs(f.rt, f.ds);
    if (f.sb) o_SBApply(f.sb);
    ConstShadowReset();
    LockSubBegin(f, 0);
    RunCtx x{&f, p, t, 0, 0, nullptr, (IDirect3DPixelShader9*)f.startPS, nullptr};
    x.rt0 = f.rt[0];
    x.dbg = (C.debug_unlerped && p && g_drawFlag.size() == f.draws.size()) ? DebugPS() : nullptr;
    // diagnostic variants 5/6: the main scene image = the render target with the most draws
    if (p && (g_variant == 5 || g_variant == 6)) {
        std::unordered_map<void*, int> cnt;
        int best = 0;
        for (const DrawRec& dr : f.draws)
            if (++cnt[dr.rt] > best) { best = cnt[dr.rt]; x.mainRT = dr.rt; }
    }
    bool rg = C.regroup >= 1 && !g_rgOff && g_trSide <= 0;
    size_t wi = 0;
    uint64_t n0 = g_runCmds;
    g_subOn = g_frzOn = false;
    g_pvReady = false;
    if (p) PairCameras(f, *p, t);
    if (p && C.precompute && C.interp_vs) {
        int64_t c0 = Qpc();
        PrecomputeConsts(f, *p, t);
        St.pvMs += Ms(Qpc() - c0);
    }
    ExecRange(x, 0, (uint32_t)f.buf.size(), rg ? &f.RgReplay().wins : nullptr, wi);
    g_pvReady = false;
    g_subOn = g_frzOn = false;
    St.replayCmds += g_runCmds - n0;
}

// Shadow map reuse for this tick's in-between frames. One walk over frame N decides, and finds the frozen constants:
// for each draw after the snapshot point that samples a copied surface's texture, the vertex shader outputs its
// pixel shader takes those coordinates from, the constants reaching them (but not the position: geometry still
// moves) and the pixel shader constants in them - and for each of those registers the command that last set it.
// Not reused when, after that point, anything draws with those surfaces or samples one after clearing it, when a
// draw samples one between its first draw and that point, or when a sampling shader cannot be analysed.
static const VSInfo* FindVSInfo(void* vs) {  // (no insert: the background matching may be reading the table)
    auto it = g_vsInfo.find(vs);
    return it != g_vsInfo.end() ? &it->second : nullptr;
}
static int PrepareShadowReuse(const Frame& N) {  // 0 = reused, else why not (stats)
    g_reuseOn = false;
    g_rN = g_subN = 0;
    if (!C.shadow_reuse || C.regroup < 2 || C.mode != 2) return 1;
    if (g_snapSerial != N.serial || !g_snapPoint) return 2;
    void* tgtTex[8];
    void* tgtDS[8];
    bool cleared[8] = {};
    int nds = 0;
    for (const ShadowSnap& s : g_snap) {
        if (s.serial != N.serial || !s.tex || s.dirty) continue;
        tgtTex[g_rN] = s.srcTex;
        g_rSurf[g_rN++] = s.src;
        if (s.ds) tgtDS[nds++] = s.ds;
        g_subFrom[g_subN] = s.srcTex;
        g_subTo[g_subN++] = s.tex;
    }
    if (!g_rN) return 2;
    const std::vector<RWin>& wins = N.RgReplay().wins;
    if (g_frzHas.size() < N.buf.size() / 4 + 1) g_frzHas.resize(N.buf.size() / 4 + 1);
    std::fill(g_frzHas.begin(), g_frzHas.begin() + (N.buf.size() / 4 + 1), 0);
    g_frzMask.clear();
    uint32_t lastVS[256], lastPS[256];
    for (int i = 0; i < 256; i++) lastVS[i] = lastPS[i] = NONE;
    // which sampler slots hold a copied surface's texture (bit per slot), and which one (texIdx)
    uint32_t texMask = 0;
    int8_t texIdx[21];
    auto setTex = [&](int s, void* tx) {
        texMask &= ~(1u << s);
        texIdx[s] = -1;
        for (int i = 0; i < g_rN && tx; i++)
            if (tx == tgtTex[i]) { texMask |= 1u << s; texIdx[s] = (int8_t)i; }
    };
    for (int s = 0; s < 21; s++) setTex(s, N.startTex[s]);
    void *vs = N.startVS, *ps = N.startPS, *lastPsKey = (void*)1;
    uint16_t psMask = 0;
    Bind cur{};
    for (int i = 0; i < 4; i++) cur.s[i] = N.rt[i];
    cur.s[4] = N.ds;
    struct Cached { void *vs, *ps; uint16_t smp; Mask256 v, p; bool ok; };
    static std::vector<Cached> cache;
    cache.clear();
    const uint8_t* b = N.buf.data();
    uint32_t frozen = 0;
    bool touched = false;  // a copied surface has been drawn into
    size_t wi = 0;
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(b + off);
        const uint8_t* p = (const uint8_t*)(c + 1);
        uint32_t next = off + c->size;
        bool after = off >= g_snapPoint;
        switch (c->op) {
        case OP_RT: { auto* x = (const PPtr*)p; if (x->a < 4 && (x->a != 0 || x->p)) cur.s[x->a] = x->p; break; }
        case OP_DS: cur.s[4] = ((const PPtr*)p)->p; break;
        case OP_TEX: { auto* x = (const PPtr*)p; int s = SamplerSlot(x->a); if (s >= 0) setTex(s, x->p); break; }
        case OP_VS: vs = ((const PPtr*)p)->p; break;
        case OP_PS: ps = ((const PPtr*)p)->p; break;
        case OP_VSCF: case OP_PSCF: {
            if (!after) break;  // (only what is set after the copies matters: freezing starts there)
            auto* q = (const PConst*)p;
            uint32_t* last = c->op == OP_VSCF ? lastVS : lastPS;
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) last[r] = off;
            break;
        }
        case OP_CLEAR:
            if (after)
                for (int i = 0; i < g_rN; i++)
                    if (cur.s[0] == g_rSurf[i]) {
                        // a clear the in-between frames execute: not inside a window that started before the copies
                        while (wi < wins.size() && wins[wi].end <= off) wi++;
                        if (wi < wins.size() && wins[wi].beg <= off && wins[wi].beg < g_snapPoint) return 9;
                        cleared[i] = true;
                    }
            break;
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP: {
            if (!after && Reused(cur.s[0])) { touched = true; break; }  // (skipped in the in-between frames)
            if (after)
                for (void* s : cur.s) {
                    if (!s) continue;
                    for (int i = 0; i < g_rN; i++)
                        if (s == g_rSurf[i]) return 4;  // drawn into later
                    for (int i = 0; i < nds; i++)
                        if (s == tgtDS[i]) return 4;
                }
            if (!texMask) break;
            if (ps != lastPsKey) { psMask = PsSamplers(ps); lastPsKey = ps; }
            uint32_t hit = texMask & ((uint32_t)psMask | 0x1F0000u);
            if (!hit) break;
            if (hit & 0x1F0000u) return 5;  // (a vertex texture)
            if (!after) {
                if (touched) return 8;  // sees a half-drawn shadow map in the game frame
                break;                   // (an old, cleared one: the same in the in-between frames)
            }
            uint16_t smp = 0;
            for (int s = 0; s < 16; s++)
                if (((hit >> s) & 1) && !cleared[texIdx[s]]) smp |= (uint16_t)(1u << s);  // (a cleared one is sampled as it is)
            if (!smp) break;
            const Cached* ce = nullptr;
            for (const Cached& x : cache)
                if (x.vs == vs && x.ps == ps && x.smp == smp) ce = &x;
            if (!ce) {
                Cached n{vs, ps, smp, {}, {}, false};
                const VsOutInfo* vo = ShaderLookup<VsOutInfo, IDirect3DVertexShader9>(g_vsOut, vs, AnalyzeVsOut);
                const PsLookInfo* pl = ShaderLookup<PsLookInfo, IDirect3DPixelShader9>(g_psLook, ps, AnalyzePsLook);
                const VSInfo* vi = vs ? FindVSInfo(vs) : nullptr;
                if (vo && vo->ok && pl && pl->ok && vi && !vi->all) {
                    n.ok = true;
                    for (int s = 0; s < 16; s++) {
                        if (!((smp >> s) & 1)) continue;
                        n.p.orr(pl->c[s]);
                        for (int sem = 0; sem < 256; sem++)
                            if (pl->in[s].test(sem))
                                if (const Mask256* cm = vo->find(sem)) n.v.orr(*cm);
                    }
                    for (int w = 0; w < 4; w++) n.v.b[w] &= ~vi->pos.b[w];  // (what places the geometry still moves)
                }
                cache.push_back(n);
                ce = &cache.back();
            }
            if (!ce->ok) return 7;
            for (int r = 0; r < 256; r++) {
                if (ce->v.test(r) && lastVS[r] != NONE) {
                    Mask256& fm = g_frzMask[lastVS[r]];
                    if (!fm.test(r)) { fm.set(r); frozen++; }
                    g_frzHas[lastVS[r] >> 2] = 1;
                }
                if (ce->p.test(r) && lastPS[r] != NONE) {
                    Mask256& fm = g_frzMask[lastPS[r]];
                    if (!fm.test(r)) { fm.set(r); frozen++; }
                    g_frzHas[lastPS[r] >> 2] = 1;
                }
            }
            break;
        }
        }
        off = next;
    }
    g_reusePoint = g_snapPoint;
    g_reuseOn = true;
    St.srFrozen += frozen;
    return 0;
}

// --- the deferred game frame
static uint32_t g_flushFrom;  // recorded commands before this have been executed
static size_t g_flushWin;     // windows before this have been executed
static bool g_flushing;
static uint32_t g_flushCuts;  // this frame's flushes other than at EndScene that executed something
static void *g_liveRT0, *g_liveDS;  // what the game frame's execution has bound
static void FlushDeferred(int why) {
    if (!g_defer || g_flushing || GetCurrentThreadId() != g_tid) return;
    Frame& f = g_fr[g_cur];
    uint32_t to = (uint32_t)f.buf.size();
    if (to <= g_flushFrom) return;
    int64_t t0 = Qpc();
    g_flushing = true;
    g_defer = 0;
    PhaseScope ps(1);
    if (why >= 0 && why < kWhyCount) St.rgWhy[why]++;
    St.rgFlushes++;
    if (why != kWhyEndScene) g_flushCuts++;
    // Windows' d3d9 applies a state block through the device's own methods (our hooks): nothing that runs here may be
    // recorded or taken for the game's state, as in an in-between frame
    bool wasInterp = g_inInterp;
    g_inInterp = true;
    RgAnalyze(f, f.rg, to);
    RgBarrier(f.rg, to);
    ConstShadowReset();
    RunCtx x{&f, nullptr, 1.0f, 0, 0, nullptr, nullptr, nullptr, true};
    x.rt0 = g_liveRT0;
    x.ds = g_liveDS;
    uint64_t cs = St.constSent, ck = St.constSkipped, ss = St.stateSent, sk = St.stateSkipped;  // (those count in-between frames)
    LockSubBegin(f, g_flushFrom);
    ExecRange(x, g_flushFrom, to, C.regroup >= 1 ? &f.rg.wins : nullptr, g_flushWin);
    St.constSent = cs, St.constSkipped = ck, St.stateSent = ss, St.stateSkipped = sk;
    g_liveRT0 = x.rt0;
    g_liveDS = x.ds;
    g_inInterp = wasInterp;
    g_flushFrom = to;
    g_pendGen++;  // (the device holds every state now)
    // what deferred draws can sample from here on: what is bound now, and what gets bound
    g_pendDraws = 0;
    g_pendTex.clear();
    g_pendTexAll = f.rg.dead;
    for (void* tx : f.rg.tex)
        if (tx) g_pendTex.push_back(tx);
    if (why == kWhyEndScene) St.endFlushMs += Ms(Qpc() - t0);
    if (why >= 0 && why < kWhyCount) St.rgWhyMs[why] += Ms(Qpc() - t0);
    g_defer = 1;
    g_flushing = false;
}
// a recorded call that runs as called (after a flush): not to be executed again
static void LiveAfterFlush() {
    if (g_defer) g_flushFrom = (uint32_t)g_fr[g_cur].buf.size();
}

// Everything else that can touch the device - its other methods, resource locks, query issues - first executes
// what is deferred: a thunk in front of each (vtable slot -> g_thunkOrig[slot]).
extern "C" {
void* g_thunkOrig[208];
void __cdecl FlushFromThunk(int slot) { FlushDeferred(slot); }
}
#define THUNK(i)                                      \
    __asm__(".globl _FlushThunk" #i "\n"              \
            "_FlushThunk" #i ":\n"                    \
            "  cmpb $0, _g_defer\n"                   \
            "  je 1f\n"                               \
            "  pushl $" #i "\n"                       \
            "  call _FlushFromThunk\n"                \
            "  addl $4, %esp\n"                       \
            "1: jmp *_g_thunkOrig+" #i "*4\n");      \
    extern "C" void FlushThunk##i();
#define THUNK10(n) THUNK(n##0) THUNK(n##1) THUNK(n##2) THUNK(n##3) THUNK(n##4) THUNK(n##5) THUNK(n##6) THUNK(n##7) THUNK(n##8) THUNK(n##9)
THUNK(0) THUNK(1) THUNK(2) THUNK(3) THUNK(4) THUNK(5) THUNK(6) THUNK(7) THUNK(8) THUNK(9)
THUNK10(1) THUNK10(2) THUNK10(3) THUNK10(4) THUNK10(5) THUNK10(6) THUNK10(7) THUNK10(8) THUNK10(9) THUNK10(10)
THUNK10(11) THUNK10(12) THUNK10(13) THUNK10(14) THUNK10(15) THUNK10(16) THUNK10(17) THUNK10(18) THUNK10(19)
#define TADDR(i) (void*)&FlushThunk##i,
#define TADDR10(n) TADDR(n##0) TADDR(n##1) TADDR(n##2) TADDR(n##3) TADDR(n##4) TADDR(n##5) TADDR(n##6) TADDR(n##7) TADDR(n##8) TADDR(n##9)
static void* const g_thunks[200] = {TADDR(0) TADDR(1) TADDR(2) TADDR(3) TADDR(4) TADDR(5) TADDR(6) TADDR(7) TADDR(8) TADDR(9)
    TADDR10(1) TADDR10(2) TADDR10(3) TADDR10(4) TADDR10(5) TADDR10(6) TADDR10(7) TADDR10(8) TADDR10(9) TADDR10(10)
    TADDR10(11) TADDR10(12) TADDR10(13) TADDR10(14) TADDR10(15) TADDR10(16) TADDR10(17) TADDR10(18) TADDR10(19)};
static void ThunkVT(void** vt, int idx, int slot) {
    void* cur = vt[idx];
    if (cur == g_thunks[slot]) return;
    g_thunkOrig[slot] = cur;  // (before the vtable points at the thunk)
    HookVT(vt, idx, g_thunks[slot]);
}
// resource methods: slots 119+ per distinct (vtable, method) - Windows' d3d9 has different classes per pool/usage
static const char* g_resName[200];
static int g_resSlots = 119;
static void ThunkRes(IUnknown* obj, int idx, const char* name) {
    if (!obj) return;
    void** vt = *(void***)obj;
    for (int s = 119; s < g_resSlots; s++)
        if (vt[idx] == g_thunks[s]) return;  // (this vtable's method is ours already)
    if (g_resSlots >= 200) return;
    g_resName[g_resSlots] = name;
    ThunkVT(vt, idx, g_resSlots++);
}
typedef HRESULT(WINAPI* TexLock_t)(IDirect3DTexture9*, UINT, D3DLOCKED_RECT*, const RECT*, DWORD);
static D3DSURFACE_DESC g_tlLast;
static DWORD g_tlLastFlags;
static uint32_t g_tlLastAt;
static void** g_tlVt[8];
static TexLock_t g_tlOrig[8];
static int g_tlN;
// A managed texture the game is about to rewrite while deferred draws still sample it: copied as it is (every level),
// the deferred commands since its last copy that bind it bind the copy instead, and a marker (OP_TEXSUB) where the
// rewrite happens switches back. In-between frames replay the same commands: each draw sees the texture as the
// game frame's draw did.
static HRESULT WINAPI hk_TexLockRect(IDirect3DTexture9* tx, UINT level, D3DLOCKED_RECT* lr, const RECT* rc, DWORD fl);
struct LockClone { IDirect3DTexture9* tex; UINT w, h, levels; D3DFORMAT fmt; DWORD usage; };
static std::vector<LockClone> g_lockClones;
static Dev* g_lockClonesDev;
static bool LockCopy(IDirect3DTexture9* tx, TexLock_t lockFn) {
    Frame& f = g_fr[g_cur];
    D3DSURFACE_DESC d;
    if (!lockFn || !f.valid || f.clones.size() >= 16 || FAILED(tx->GetLevelDesc(0, &d)) || d.Pool != D3DPOOL_MANAGED) return false;
    UINT levels = tx->GetLevelCount();
    bool autogen = (d.Usage & D3DUSAGE_AUTOGENMIPMAP) != 0;
    if (g_lockClonesDev != g_dev) { g_lockClones.clear(); g_lockClonesDev = g_dev; }  // (another device's: not used again)
    IDirect3DTexture9* cl = nullptr;
    for (const LockClone& c : g_lockClones) {  // a free one of this kind: no recorded frame refers to it
        if (c.w != d.Width || c.h != d.Height || c.fmt != d.Format || c.levels != levels || c.usage != d.Usage) continue;
        bool used = false;
        for (const Frame& fr : g_fr)
            for (void* u : fr.clones) used |= u == (void*)c.tex;
        if (!used) { cl = c.tex; break; }
    }
    if (!cl) {
        if (g_lockClones.size() >= 48) return false;
        uint8_t keep = g_defer;
        g_defer = 0;  // (our own call: no flush)
        HRESULT hr = g_dev->CreateTexture(d.Width, d.Height, autogen ? 0 : levels, d.Usage, d.Format, D3DPOOL_MANAGED, &cl, nullptr);
        g_defer = keep;
        if (FAILED(hr) || !cl) return false;
        g_lockClones.push_back({cl, d.Width, d.Height, levels, d.Format, d.Usage});
        if (g_lockClones.size() <= 4)
            Log("texture copies at deferred locks: %ux%u format %d, %u levels, usage %lx (%zu copies)", d.Width, d.Height, d.Format, levels,
                d.Usage, g_lockClones.size());
    }
    TexLock_t clLock = (TexLock_t)(*(void***)cl)[19];  // (the copy's own LockRect, not our hook)
    for (int i = 0; i < g_tlN; i++)
        if (g_tlVt[i] == *(void***)cl) clLock = g_tlOrig[i];
    if ((void*)clLock == (void*)&hk_TexLockRect) return false;
    for (UINT l = 0; l < levels; l++) {
        D3DSURFACE_DESC ld;
        D3DLOCKED_RECT a, b;
        if (FAILED(tx->GetLevelDesc(l, &ld)) || FAILED(lockFn(tx, l, &a, nullptr, D3DLOCK_READONLY))) return false;
        if (FAILED(clLock(cl, l, &b, nullptr, 0))) { tx->UnlockRect(l); return false; }
        bool bc = ld.Format == D3DFMT_DXT1 || ld.Format == D3DFMT_DXT2 || ld.Format == D3DFMT_DXT3 || ld.Format == D3DFMT_DXT4 ||
                  ld.Format == D3DFMT_DXT5;
        UINT rows = bc ? (ld.Height + 3) / 4 : ld.Height;
        UINT bytes = (UINT)std::min(a.Pitch, b.Pitch);
        for (UINT y = 0; y < rows; y++) memcpy((uint8_t*)b.pBits + y * b.Pitch, (const uint8_t*)a.pBits + y * a.Pitch, bytes);
        cl->UnlockRect(l);
        tx->UnlockRect(l);
    }
    // the commands since this texture's last copy (or the frame's start) that bind it - also those executed already, for
    // the in-between frames; the game frame's own execution from here on starts on the copy (LockSubBegin)
    uint32_t from = 0;
    int prev = -1;
    for (int i = (int)f.subs.size() - 1; i >= 0; i--) {
        auto* x = (const PTexSub*)((const Cmd*)(f.buf.data() + f.subs[i]) + 1);
        if (x->tex == tx) { prev = i; from = std::max(from, f.subs[i]); break; }
    }
    for (size_t i = f.texCmds.size(); i-- > 0;) {
        uint32_t o = f.texCmds[i];
        if (o < from) break;
        auto* x = (PPtr*)((Cmd*)(f.buf.data() + o) + 1);
        if (x->p == (IUnknown*)tx) x->p = (IUnknown*)cl;
    }
    if (prev >= 0) ((PTexSub*)((Cmd*)(f.buf.data() + f.subs[prev]) + 1))->next = cl;
    Ref(cl);
    f.clones.push_back(cl);
    *(PTexSub*)Push(OP_TEXSUB, sizeof(PTexSub)) = {tx, cl, tx};
    f.subs.push_back(g_lastOff);
    return true;
}
static HRESULT WINAPI hk_TexLockRect(IDirect3DTexture9* tx, UINT level, D3DLOCKED_RECT* lr, const RECT* rc, DWORD fl) {
    void** vt = *(void***)tx;
    if (!(fl & D3DLOCK_READONLY))
        for (LONG i = 0; i < g_atlasN; i++)
            if (g_atlasPtr[i] == (void*)tx) { InterlockedIncrement(&g_texLockGen); break; }
    TexLock_t o = nullptr;
    for (int i = 0; i < g_tlN; i++)
        if (g_tlVt[i] == vt) { o = g_tlOrig[i]; break; }
    if (g_defer && GetCurrentThreadId() == g_tid && g_pendDraws && (g_pendTexAll || Has(g_pendTex, tx))) {
        // (diagnostics: which textures, and how far into the frame)
        D3DSURFACE_DESC d{};
        tx->GetLevelDesc(level, &d);
        g_tlLast = d;
        g_tlLastFlags = fl;
        g_tlLastAt = (uint32_t)g_fr[g_cur].draws.size();
        if (C.lock_copy && LockCopy(tx, o)) {
            St.lockCopies++;
        } else {
            if (C.lock_copy && d.Pool == D3DPOOL_MANAGED) St.lockCopyFails++;
            St.tlFlushes++;
            FlushDeferred(kWhyTexLock);
        }
    }
    return o ? o(tx, level, lr, rc, fl) : D3DERR_INVALIDCALL;
}
// Glyph atlases: small managed 32-bit textures that screen-space quads sample. A CPU copy of level 0, made when a glyph
// draw uses the texture and no copy since the last texture lock exists - so about once per rewrite (MHF rewrites its
// text atlas a few times a frame). The copy is shared by the frames whose glyphs sampled that version.
struct AtlasEntry { std::shared_ptr<const AtlasSnap> snap; LONG gen = -1; int8_t atlas = -1; };
static std::unordered_map<void*, AtlasEntry> g_atlases;
static std::shared_ptr<const AtlasSnap> AtlasSnapshot(IDirect3DTexture9* tx) {
    if (g_atlases.size() > 256) g_atlases.clear();
    AtlasEntry& e = g_atlases[tx];
    LONG gen = g_texLockGen;
    if (e.atlas == 0) return nullptr;
    if (e.snap && e.gen == gen) return e.snap;
    D3DSURFACE_DESC d;
    if (FAILED(tx->GetLevelDesc(0, &d)) || d.Pool != D3DPOOL_MANAGED || (d.Format != D3DFMT_A8R8G8B8 && d.Format != D3DFMT_X8R8G8B8) || d.Width > 1024 ||
        d.Height > 1024) {
        e.atlas = 0;
        return nullptr;
    }
    if (e.atlas != 1) {  // (its locks now count)
        bool known = false;
        for (LONG i = 0; i < g_atlasN; i++) known |= g_atlasPtr[i] == (void*)tx;
        if (!known && g_atlasN < 8) g_atlasPtr[g_atlasN] = tx, InterlockedIncrement(&g_atlasN);
    }
    e.atlas = 1;
    TexLock_t o = (TexLock_t)(*(void***)tx)[19];  // (the original LockRect, not our hook)
    for (int i = 0; i < g_tlN; i++)
        if (g_tlVt[i] == *(void***)tx) o = g_tlOrig[i];
    if ((void*)o == (void*)&hk_TexLockRect) return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(o(tx, 0, &lr, nullptr, D3DLOCK_READONLY))) return nullptr;
    auto s = std::make_shared<AtlasSnap>();
    static uint32_t serial;
    s->w = d.Width, s->h = d.Height, s->serial = ++serial;
    s->px.resize((size_t)d.Width * d.Height);
    for (UINT y = 0; y < d.Height; y++) memcpy(&s->px[(size_t)y * d.Width], (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch, d.Width * 4);
    tx->UnlockRect(0);
    e.snap = s;
    e.gen = gen;
    St.atlasSnaps++;
    return e.snap;
}
static void HookTexLock(IDirect3DTexture9* tx) {
    void** vt = *(void***)tx;
    if (vt[19] == (void*)&hk_TexLockRect || g_tlN >= 8) return;
    g_tlVt[g_tlN] = vt;
    g_tlOrig[g_tlN] = (TexLock_t)vt[19];
    g_tlN++;
    HookVT(vt, 19, (void*)&hk_TexLockRect);
}
static void HookResourceVTs(Dev* dev) {
    const D3DPOOL pools[] = {D3DPOOL_DEFAULT, D3DPOOL_MANAGED, D3DPOOL_SYSTEMMEM};
    for (D3DPOOL pool : pools)
        for (DWORD use : {0ul, (DWORD)(D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY)}) {
            if (pool == D3DPOOL_MANAGED && use) continue;
            IDirect3DVertexBuffer9* vb = nullptr;
            if (SUCCEEDED(dev->CreateVertexBuffer(64, use, 0, pool, &vb, nullptr)) && vb) { ThunkRes(vb, 11, "VB Lock"); vb->Release(); }
            IDirect3DIndexBuffer9* ib = nullptr;
            if (SUCCEEDED(dev->CreateIndexBuffer(64, use, D3DFMT_INDEX16, pool, &ib, nullptr)) && ib) { ThunkRes(ib, 11, "IB Lock"); ib->Release(); }
            for (DWORD tu : {use, pool == D3DPOOL_DEFAULT && !use ? (DWORD)D3DUSAGE_RENDERTARGET : use}) {
                IDirect3DTexture9* tx = nullptr;
                if (SUCCEEDED(dev->CreateTexture(4, 4, 1, tu & ~(DWORD)D3DUSAGE_WRITEONLY, D3DFMT_A8R8G8B8, pool, &tx, nullptr)) && tx) {
                    HookTexLock(tx);
                    IDirect3DSurface9* s = nullptr;
                    if (SUCCEEDED(tx->GetSurfaceLevel(0, &s)) && s) { ThunkRes(s, 13, "surface LockRect"); ThunkRes(s, 15, "surface GetDC"); s->Release(); }
                    tx->Release();
                }
            }
            IDirect3DCubeTexture9* cu = nullptr;
            if (SUCCEEDED(dev->CreateCubeTexture(4, 1, use & ~(DWORD)D3DUSAGE_WRITEONLY, D3DFMT_A8R8G8B8, pool, &cu, nullptr)) && cu) {
                ThunkRes(cu, 19, "cube LockRect");
                IDirect3DSurface9* s = nullptr;
                if (SUCCEEDED(cu->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X, 0, &s)) && s) { ThunkRes(s, 13, "surface LockRect"); ThunkRes(s, 15, "surface GetDC"); s->Release(); }
                cu->Release();
            }
            IDirect3DVolumeTexture9* vo = nullptr;
            if (!use && SUCCEEDED(dev->CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, pool, &vo, nullptr)) && vo) {
                ThunkRes(vo, 19, "volume texture LockBox");
                IDirect3DVolume9* v = nullptr;
                if (SUCCEEDED(vo->GetVolumeLevel(0, &v)) && v) { ThunkRes(v, 9, "volume LockBox"); v->Release(); }
                vo->Release();
            }
        }
    IDirect3DSurface9* s = nullptr;
    for (D3DPOOL pool : {D3DPOOL_DEFAULT, D3DPOOL_SYSTEMMEM})
        if (SUCCEEDED(dev->CreateOffscreenPlainSurface(4, 4, D3DFMT_A8R8G8B8, pool, &s, nullptr)) && s) { ThunkRes(s, 13, "surface LockRect"); ThunkRes(s, 15, "surface GetDC"); s->Release(); s = nullptr; }
    if (SUCCEEDED(dev->CreateRenderTarget(4, 4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &s, nullptr)) && s) { ThunkRes(s, 13, "surface LockRect"); ThunkRes(s, 15, "surface GetDC"); s->Release(); s = nullptr; }
    if (SUCCEEDED(dev->CreateDepthStencilSurface(4, 4, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr)) && s) { ThunkRes(s, 13, "surface LockRect"); s->Release(); s = nullptr; }
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &s)) && s) { ThunkRes(s, 13, "surface LockRect"); ThunkRes(s, 15, "surface GetDC"); s->Release(); s = nullptr; }
    for (D3DQUERYTYPE qt : {D3DQUERYTYPE_EVENT, D3DQUERYTYPE_OCCLUSION, D3DQUERYTYPE_TIMESTAMP}) {
        IDirect3DQuery9* q = nullptr;
        if (SUCCEEDED(dev->CreateQuery(qt, &q)) && q) { ThunkRes(q, 6, "query Issue"); q->Release(); }
    }
    Log("regroup: %d resource methods guarded", g_resSlots - 119);
}
static const char* const kDevMethod[119] = {"QueryInterface", "AddRef", "Release", "TestCooperativeLevel",
    "GetAvailableTextureMem", "EvictManagedResources", "GetDirect3D", "GetDeviceCaps", "GetDisplayMode",
    "GetCreationParameters", "SetCursorProperties", "SetCursorPosition", "ShowCursor", "CreateAdditionalSwapChain",
    "GetSwapChain", "GetNumberOfSwapChains", "Reset", "Present", "GetBackBuffer", "GetRasterStatus", "SetDialogBoxMode",
    "SetGammaRamp", "GetGammaRamp", "CreateTexture", "CreateVolumeTexture", "CreateCubeTexture", "CreateVertexBuffer",
    "CreateIndexBuffer", "CreateRenderTarget", "CreateDepthStencilSurface", "UpdateSurface", "UpdateTexture",
    "GetRenderTargetData", "GetFrontBufferData", "StretchRect", "ColorFill", "CreateOffscreenPlainSurface",
    "SetRenderTarget", "GetRenderTarget", "SetDepthStencilSurface", "GetDepthStencilSurface", "BeginScene", "EndScene",
    "Clear", "SetTransform", "GetTransform", "MultiplyTransform", "SetViewport", "GetViewport", "SetMaterial",
    "GetMaterial", "SetLight", "GetLight", "LightEnable", "GetLightEnable", "SetClipPlane", "GetClipPlane",
    "SetRenderState", "GetRenderState", "CreateStateBlock", "BeginStateBlock", "EndStateBlock", "SetClipStatus",
    "GetClipStatus", "GetTexture", "SetTexture", "GetTextureStageState", "SetTextureStageState", "GetSamplerState",
    "SetSamplerState", "ValidateDevice", "SetPaletteEntries", "GetPaletteEntries", "SetCurrentTexturePalette",
    "GetCurrentTexturePalette", "SetScissorRect", "GetScissorRect", "SetSoftwareVertexProcessing",
    "GetSoftwareVertexProcessing", "SetNPatchMode", "GetNPatchMode", "DrawPrimitive", "DrawIndexedPrimitive",
    "DrawPrimitiveUP", "DrawIndexedPrimitiveUP", "ProcessVertices", "CreateVertexDeclaration", "SetVertexDeclaration",
    "GetVertexDeclaration", "SetFVF", "GetFVF", "CreateVertexShader", "SetVertexShader", "GetVertexShader",
    "SetVertexShaderConstantF", "GetVertexShaderConstantF", "SetVertexShaderConstantI", "GetVertexShaderConstantI",
    "SetVertexShaderConstantB", "GetVertexShaderConstantB", "SetStreamSource", "GetStreamSource", "SetStreamSourceFreq",
    "GetStreamSourceFreq", "SetIndices", "GetIndices", "CreatePixelShader", "SetPixelShader", "GetPixelShader",
    "SetPixelShaderConstantF", "GetPixelShaderConstantF", "SetPixelShaderConstantI", "GetPixelShaderConstantI",
    "SetPixelShaderConstantB", "GetPixelShaderConstantB", "DrawRectPatch", "DrawTriPatch", "DeletePatch", "CreateQuery"};
static const char* WhyName(int i) {
    static const char* const why[] = {"EndScene", "Present", "Sleep", "StretchRect/ColorFill", "update/readback", "state block", "Reset", "texture LockRect"};
    if (i < 119) return kDevMethod[i];
    if (i < 200) return g_resName[i] ? g_resName[i] : "resource";
    return i - 200 < (int)(sizeof why / sizeof *why) ? why[i - 200] : "?";
}

// ------------------------------------------------------------------------------------------------ end-of-frame state save/restore
static IDirect3DStateBlock9* g_sbEnd;
static IDirect3DSurface9 *g_saved, *g_bb, *g_endRT[4], *g_endDS;
static D3DVIEWPORT9 g_endVP;
static RECT g_endSc;
static RECT g_src, g_dst;
static bool g_hasSrc, g_hasDst;
static HWND g_presentWnd;

static void ReleaseEnd() {
    for (auto*& r : g_endRT) if (r) { r->Release(); r = nullptr; }
    if (g_endDS) { g_endDS->Release(); g_endDS = nullptr; }
    if (g_bb) { g_bb->Release(); g_bb = nullptr; }
}

static bool SaveEnd() {
    if (!g_sbEnd && FAILED(o_CreateStateBlock(g_dev, D3DSBT_ALL, &g_sbEnd))) return false;
    o_SBCapture(g_sbEnd);
    for (int i = 0; i < 4; i++) if (FAILED(g_dev->GetRenderTarget(i, &g_endRT[i]))) g_endRT[i] = nullptr;
    if (FAILED(g_dev->GetDepthStencilSurface(&g_endDS))) g_endDS = nullptr;
    g_dev->GetViewport(&g_endVP);
    g_dev->GetScissorRect(&g_endSc);
    if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_bb))) return false;
    D3DSURFACE_DESC bd;
    g_bb->GetDesc(&bd);
    if (g_saved) {
        D3DSURFACE_DESC sd;
        g_saved->GetDesc(&sd);
        if (sd.Width != bd.Width || sd.Height != bd.Height || sd.Format != bd.Format) { g_saved->Release(); g_saved = nullptr; }
    }
    if (!g_saved && FAILED(g_dev->CreateRenderTarget(bd.Width, bd.Height, bd.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &g_saved, nullptr))) {
        Log("CreateRenderTarget for backbuffer copy failed (%ux%u fmt %d)", bd.Width, bd.Height, bd.Format);
        return false;
    }
    return SUCCEEDED(o_StretchRect(g_dev, g_bb, nullptr, g_saved, nullptr, D3DTEXF_NONE));
}

static void RestoreEnd() {
    SetRTs(g_endRT, g_endDS);
    if (g_saved && g_bb) o_StretchRect(g_dev, g_saved, nullptr, g_bb, nullptr, D3DTEXF_NONE);
    o_SBApply(g_sbEnd);
    o_SetViewport(g_dev, &g_endVP);
    o_SetScissorRect(g_dev, &g_endSc);
    ReleaseEnd();
}

// regroup_check: the same frame drawn regrouped and in recorded order must be the same image, pixel for pixel
static IDirect3DSurface9 *g_rcRT, *g_rcSys[3];
static int64_t g_rcLast;
static void ReleaseRegroupCheck() {
    if (g_rcRT) { g_rcRT->Release(); g_rcRT = nullptr; }
    for (auto*& s : g_rcSys) if (s) { s->Release(); s = nullptr; }
}
static bool RcGrab(int k, IDirect3DSurface9* src) {  // src (or the back buffer) -> g_rcSys[k]
    WhereScope ws("RcGrab");
    IDirect3DSurface9* bb = src;
    if (bb) bb->AddRef();
    else if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return false;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    auto fit = [&](IDirect3DSurface9*& s, bool sys) {
        if (s) {
            D3DSURFACE_DESC e;
            s->GetDesc(&e);
            if (e.Width != d.Width || e.Height != d.Height || e.Format != d.Format) { s->Release(); s = nullptr; }
        }
        if (!s) {
            if (sys) g_dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &s, nullptr);
            else g_dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr);
        }
        return s != nullptr;
    };
    bool ok = (d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8) && fit(g_rcRT, false) && fit(g_rcSys[k], true) &&
              SUCCEEDED(o_StretchRect(g_dev, bb, nullptr, g_rcRT, nullptr, D3DTEXF_NONE)) &&
              SUCCEEDED(o_GetRenderTargetData(g_dev, g_rcRT, g_rcSys[k]));
    bb->Release();
    return ok;
}
static long RcDiff(int a, int b) {  // pixels that differ (-1: no comparison)
    if (!g_rcSys[a] || !g_rcSys[b]) return -1;
    D3DSURFACE_DESC d;
    g_rcSys[a]->GetDesc(&d);
    D3DLOCKED_RECT la, lb;
    if (FAILED(g_rcSys[a]->LockRect(&la, nullptr, D3DLOCK_READONLY))) return -1;
    if (FAILED(g_rcSys[b]->LockRect(&lb, nullptr, D3DLOCK_READONLY))) { g_rcSys[a]->UnlockRect(); return -1; }
    long n = 0;
    for (UINT y = 0; y < d.Height; y++) {
        const uint32_t* pa = (const uint32_t*)((const uint8_t*)la.pBits + y * la.Pitch);
        const uint32_t* pb = (const uint32_t*)((const uint8_t*)lb.pBits + y * lb.Pitch);
        if (memcmp(pa, pb, d.Width * 4) == 0) continue;
        for (UINT x = 0; x < d.Width; x++) n += (pa[x] & 0xFFFFFF) != (pb[x] & 0xFFFFFF);
    }
    g_rcSys[a]->UnlockRect();
    g_rcSys[b]->UnlockRect();
    return n;
}
static uint64_t RcHash(int k) {  // FNV of the grabbed image's colour (debug_replayhash)
    if (!g_rcSys[k]) return 0;
    D3DSURFACE_DESC d;
    g_rcSys[k]->GetDesc(&d);
    D3DLOCKED_RECT l;
    if (FAILED(g_rcSys[k]->LockRect(&l, nullptr, D3DLOCK_READONLY))) return 0;
    uint64_t h = 1469598103934665603ull;
    for (UINT y = 0; y < d.Height; y++) {
        const uint32_t* p = (const uint32_t*)((const uint8_t*)l.pBits + y * l.Pitch);
        for (UINT x = 0; x < d.Width; x++) h = (h ^ (p[x] & 0xFFFFFF)) * 1099511628211ull;
    }
    g_rcSys[k]->UnlockRect();
    return h;
}
// The in-between frame in recorded order, regrouped, and in recorded order again (a frame drawn on top of its own
// output can differ from itself: that is the baseline). With regroup=2 also the game's own frame as it was shown
// (regrouped) against its recording replayed in recorded order. Ends with the regrouped in-between frame drawn.
static void RegroupCheck(Frame& N, const Frame* P, float t) {
    bool skipKeep = g_reuseOn;
    g_reuseOn = false;
    long live = -2;
    if (C.regroup >= 2 && g_saved) {
        g_rgOff = true;
        Replay(N, nullptr, 1.0f);
        g_rgOff = false;
        if (RcGrab(0, g_saved) && RcGrab(1, nullptr)) live = RcDiff(0, 1);
    }
    g_rgOff = true;
    Replay(N, P, t);
    bool a = RcGrab(0, nullptr);
    g_rgOff = false;
    Replay(N, P, t);
    bool b = RcGrab(1, nullptr);
    g_rgOff = true;
    Replay(N, P, t);
    bool c = RcGrab(2, nullptr);
    g_rgOff = false;
    long d1 = a && b ? RcDiff(0, 1) : -1, d2 = a && c ? RcDiff(0, 2) : -1, dr = -2;
    if (skipKeep) {  // with shadow reuse: differs only where shadows of moving things fall (nothing, if nothing moves)
        g_reuseOn = skipKeep;
        Replay(N, P, t);
        dr = b && RcGrab(2, nullptr) ? RcDiff(1, 2) : -1;
        static int saved;
        if (dr > 2000 && saved < 3) {
            typedef HRESULT(WINAPI * Save_t)(LPCSTR, DWORD, IDirect3DSurface9*, const PALETTEENTRY*, const RECT*);
            static Save_t save = nullptr;
            if (!save) {
                HMODULE dx = LoadLibraryA("d3dx9_43.dll");
                save = dx ? (Save_t)GetProcAddress(dx, "D3DXSaveSurfaceToFileA") : nullptr;
            }
            char path[MAX_PATH];
            for (int k = 1; k <= 2 && save; k++) {
                snprintf(path, sizeof path, "%s\\shadowcheck_%d_%s.png", g_dir, saved, k == 1 ? "drawn" : "reused");
                save(path, 3 /*D3DXIFF_PNG*/, g_rcSys[k], nullptr, nullptr);
            }
            saved++;
        }
    }
    uint32_t in = 0, out = 0;
    for (const RWin& w : N.RgReplay().wins) { in += w.passes; out += (uint32_t)w.nb; }
    Log("regroup check: %zu windows, %u target switches -> %u | in-between frame regrouped vs in order: %ld pixels differ "
        "(in order vs in order: %ld) | game frame as shown vs its recording in order: %ld%s | shadow reuse vs drawn: %ld",
        N.RgReplay().wins.size(), in, out, d1, d2, live, live == -2 ? " (not deferred)" : "", dr);
    g_reuseOn = skipKeep;
}

static void FlushCaptures(bool save);
static void ReleaseRegroupCheck();
static void ReleaseSnaps();
static void RetroRelease();
static void ReleaseCheckSurfaces();
static IDirect3DQuery9* g_evq;  // event query used to wait for the GPU to finish our frames
static IDirect3DQuery9* g_frameQ[2];  // per in-between frame (see WaitQuery)
// GPU time of a stretch of work, from timestamp queries read a tick later (never waits): the game's own frame (from
// the previous real Present to this one) and the first in-between frame of a tick (its replay).
struct GpuTimer {
    IDirect3DQuery9 *disjoint = nullptr, *t0 = nullptr, *t1 = nullptr, *freq = nullptr;
    bool open = false, pending = false;
};
static GpuTimer g_gtReal, g_gtInterp;
static void GpuTimerRelease(GpuTimer& g);
static bool g_frameQIssued[2];
static void OvRelease();
static void CarryRelease();
static void ReleaseAll() {
    PairJoin();
    ReleaseCheckSurfaces();
    CarryRelease();
    for (auto& f : g_fr) {
        f.Clear();
        if (f.sb) { f.sb->Release(); f.sb = nullptr; }
        if (f.img) { f.img->Release(); f.img = nullptr; }
    }
    FlushCaptures(false);  // default-pool surfaces must go before a Reset
    RetroRelease();
    TraceReleaseAll();
    if (g_evq) { g_evq->Release(); g_evq = nullptr; }
    for (auto& q : g_frameQ) if (q) { q->Release(); q = nullptr; }
    GpuTimerRelease(g_gtReal);
    GpuTimerRelease(g_gtInterp);
    ReleaseEnd();
    if (g_sbEnd) { g_sbEnd->Release(); g_sbEnd = nullptr; }
    if (g_rgSB) { g_rgSB->Release(); g_rgSB = nullptr; }
    ReleaseRegroupCheck();
    ReleaseSnaps();
    g_reuseOn = false;
    g_rN = g_subN = 0;
    g_defer = 0;
    if (g_saved) { g_saved->Release(); g_saved = nullptr; }
    OvRelease();
}

// ------------------------------------------------------------------------------------------------ timing
typedef VOID(WINAPI* Sleep_t)(DWORD);
static Sleep_t o_Sleep;
static HANDLE g_timer;
static int64_t g_lastReal;      // QPC just before the last real Present
static int64_t g_frameStart;    // QPC just after the last real Present
static double g_replayCost;     // QPC ticks, EMA of replay+present (CPU side)
static double g_gpuTail;        // QPC ticks, EMA of how long the GPU still needs after our last Present returns
static int64_t g_backoffUntil;  // the game ran late recently: interpolate less until then
static bool g_sleptThisFrame;

// Wait until the GPU has finished everything submitted so far.
static void GpuTimerBegin(GpuTimer& g) {
    if (!g_dev || g.pending || g.open) return;
    if (!g.disjoint) {
        if (FAILED(g_dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &g.disjoint)) || FAILED(g_dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &g.t0)) ||
            FAILED(g_dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &g.t1)) || FAILED(g_dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &g.freq)))
            return;
    }
    g.disjoint->Issue(D3DISSUE_BEGIN);
    g.t0->Issue(D3DISSUE_END);
    g.open = true;
}
static void GpuTimerEnd(GpuTimer& g) {
    if (!g.open) return;
    g.t1->Issue(D3DISSUE_END);
    g.freq->Issue(D3DISSUE_END);
    g.disjoint->Issue(D3DISSUE_END);
    g.open = false;
    g.pending = true;
}
static bool GpuTimerRead(GpuTimer& g, double& ms) {  // a finished measurement, without waiting
    if (!g.pending) return false;
    BOOL dis = TRUE;
    UINT64 a = 0, b = 0, f = 0;
    if (g.disjoint->GetData(&dis, sizeof dis, 0) != S_OK || g.t0->GetData(&a, 8, 0) != S_OK || g.t1->GetData(&b, 8, 0) != S_OK ||
        g.freq->GetData(&f, 8, 0) != S_OK)
        return false;
    g.pending = false;
    if (dis || !f || b < a) return false;
    ms = (double)(b - a) * 1000.0 / (double)f;
    return true;
}
static void GpuTimerRelease(GpuTimer& g) {
    for (IDirect3DQuery9** q : {&g.disjoint, &g.t0, &g.t1, &g.freq})
        if (*q) { (*q)->Release(); *q = nullptr; }
    g.open = g.pending = false;
}

static void WaitGpu() {
    if (!g_evq) return;
    WhereScope ws("WaitGpu");
    g_evq->Issue(D3DISSUE_END);
    for (int i = 0; i < 1000000 && g_evq->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE; i++) SwitchToThread();
}
// One event per in-between frame, issued after its Present, so a frame can wait for the one two before it: the GPU
// keeps drawing the previous frame while the CPU builds the next (waiting for everything each time left the GPU
// idle during every replay and the CPU idle during every GPU frame).
static void WaitQuery(IDirect3DQuery9* q) {
    WhereScope ws("WaitQuery");
    for (int i = 0; i < 1000000 && q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE; i++) SwitchToThread();
}
// ...the same with a time limit: false if the GPU wasn't done by then. No further in-between frame waits for the GPU past
// the game's deadline: the game's next tick - when it reads its input - must not start late. (On light screens - menus,
// the area map - presentation backs up, and an unbounded wait ran past the deadline on 15-40 ticks in 10 seconds.)
// The drain after the last in-between frame stays unbounded (see Interp).
static bool WaitQueryUntil(IDirect3DQuery9* q, int64_t until) {
    WhereScope ws("WaitQueryUntil");
    for (;;) {
        if (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) != S_FALSE) return true;
        if (Qpc() >= until) return false;
        SwitchToThread();
    }
}
static bool WaitGpuUntil(int64_t until) {
    if (!g_evq) return true;
    g_evq->Issue(D3DISSUE_END);
    return WaitQueryUntil(g_evq, until);
}

// ------------------------------------------------------------------------------------------------ input latch
// MHF reads keyboard, mouse and gamepads through DirectInput 8 in immediate mode: GetDeviceState once per tick. A press
// that starts and ends between two of those reads (a quick tap, under ~34 ms) is never seen; in a menu that reads as
// "pressed, nothing happened". With input_latch=1 the mod reads the same devices again while the game sleeps in its
// limiter (on the game's own thread, every millisecond or so between in-between frames) and while it draws, and a
// press that went down and up again unseen is reported held at the game's next read - once. Presses the game sees
// anyway are left as they are, and nothing is delayed. Only DirectInput's own objects (in dinput8.dll) are hooked.
static const GUID kIID_DI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID kIID_DI8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID kGUID_SysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
typedef HRESULT(WINAPI* DiGetState_t)(void* self, DWORD cb, void* data);       // IDirectInputDevice8::GetDeviceState (9)
typedef HRESULT(WINAPI* DiPoll_t)(void* self);                                  // IDirectInputDevice8::Poll (25)
typedef HRESULT(WINAPI* DiCreateDev_t)(void* self, REFGUID g, void** dev, void* outer);  // IDirectInput8::CreateDevice (3)
struct DiVt { void** vt; void* orig; };
static DiVt g_diStateVt[8], g_diCreateVt[4];  // (filled before the slot is patched: a call through it finds its original)
static volatile LONG g_diStateVtN, g_diCreateVtN;
struct LatchDev {
    void* dev; DWORD cb; DiGetState_t get; DiPoll_t poll;
    uint8_t seen[272];   // what the samples since the game's last read saw down (bit 7)
    uint8_t gave[272];   // what the game got at its last read
    uint32_t povSeen, povGave;  // first POV hat: a direction sampled since the last read / what the game got (0xFFFFxxxx: centred)
    bool sampled;
};
static LatchDev g_ld[8];
static int g_ldN;
static SRWLOCK g_ldLock = SRWLOCK_INIT;
static int64_t g_ldLast;
static bool g_diHooked;

// Which bytes of a device state are buttons, by the standard data formats' sizes: c_dfDIKeyboard (256 keys),
// c_dfDIMouse/2 (buttons from 12), c_dfDIJoystick/2 (POV hats at 32, buttons from 48). Other formats: not latched.
static bool LatchLayout(DWORD cb, int& b0, int& b1, int& pov) {
    pov = -1;
    if (cb == 256) { b0 = 0; b1 = 256; return true; }
    if (cb == 16 || cb == 20) { b0 = 12; b1 = (int)cb; return true; }
    if (cb == 80) { b0 = 48; b1 = 80; pov = 32; return true; }
    if (cb == 272) { b0 = 48; b1 = 176; pov = 32; return true; }
    return false;
}
static void* DiOrig(DiVt* t, volatile LONG& n, void* self) {
    void** vt = *(void***)self;
    for (LONG i = 0, e = n; i < e; i++) if (t[i].vt == vt) return t[i].orig;
    return nullptr;
}
static HRESULT WINAPI hk_DiGetState(void* self, DWORD cb, void* data) {
    auto get = (DiGetState_t)DiOrig(g_diStateVt, g_diStateVtN, self);
    if (!get) return E_FAIL;
    HRESULT hr = get(self, cb, data);
    int b0, b1, pov;
    if (FAILED(hr) || !data || !LatchLayout(cb, b0, b1, pov)) return hr;
    AcquireSRWLockExclusive(&g_ldLock);
    LatchDev* d = nullptr;
    for (int i = 0; i < g_ldN; i++) if (g_ld[i].dev == self && g_ld[i].cb == cb) d = &g_ld[i];
    if (!d && g_ldN < 8) {
        d = &g_ld[g_ldN++];
        memset(d, 0, sizeof *d);
        d->dev = self, d->cb = cb, d->get = get, d->poll = (DiPoll_t)(*(void***)self)[25];
        d->povSeen = d->povGave = 0xFFFFFFFF;
        Log("input: the game reads a DirectInput device %p (%lu-byte state, thread %lu%s)", self, cb, GetCurrentThreadId(),
            GetCurrentThreadId() == g_tid ? ", the render thread" : "");
    }
    if (d) {
        uint8_t* s = (uint8_t*)data;
        if (C.input_latch && d->sampled) {
            int added = 0;
            for (int k = b0; k < b1; k++)
                if ((d->seen[k] & 0x80) && !(s[k] & 0x80) && !(d->gave[k] & 0x80)) s[k] |= 0x80, added++;
            uint32_t* p = pov >= 0 ? (uint32_t*)(s + pov) : nullptr;
            if (p && LOWORD(d->povSeen) != 0xFFFF && LOWORD(*p) == 0xFFFF && LOWORD(d->povGave) == 0xFFFF) *p = d->povSeen, added++;
            St.inputLatched += added;
        }
        memcpy(d->gave, s, std::min<size_t>(cb, sizeof d->gave));
        if (pov >= 0) d->povGave = *(uint32_t*)(s + pov);
        memset(d->seen, 0, sizeof d->seen);
        d->povSeen = 0xFFFFFFFF;
        d->sampled = false;
    }
    ReleaseSRWLockExclusive(&g_ldLock);
    return hr;
}
// Reads the game's devices (at most once a millisecond). Called on the game's thread while it sleeps and draws.
static void InputSample() {
    if (!C.input_latch || !g_ldN) return;
    int64_t now = Qpc();
    if (now - g_ldLast < g_qpf.QuadPart / 1000) return;
    g_ldLast = now;
    uint8_t buf[272];
    AcquireSRWLockExclusive(&g_ldLock);
    for (int i = 0; i < g_ldN; i++) {
        LatchDev& d = g_ld[i];
        int b0, b1, pov;
        if (!LatchLayout(d.cb, b0, b1, pov)) continue;
        if (pov >= 0) d.poll(d.dev);  // (gamepads are polled devices: their state only moves on Poll)
        if (FAILED(d.get(d.dev, d.cb, buf))) continue;
        for (int k = b0; k < b1; k++) d.seen[k] |= buf[k] & 0x80;
        if (pov >= 0 && LOWORD(*(uint32_t*)(buf + pov)) != 0xFFFF && LOWORD(d.povSeen) == 0xFFFF) d.povSeen = *(uint32_t*)(buf + pov);
        d.sampled = true;
        St.inputSamples++;
    }
    ReleaseSRWLockExclusive(&g_ldLock);
}
static void DiHookState(void** vt) {
    for (LONG i = 0; i < g_diStateVtN; i++) if (g_diStateVt[i].vt == vt) return;
    if (g_diStateVtN >= 8 || vt[9] == (void*)&hk_DiGetState) return;
    g_diStateVt[g_diStateVtN] = {vt, vt[9]};
    InterlockedIncrement(&g_diStateVtN);
    HookVT(vt, 9, (void*)&hk_DiGetState);
}
static HRESULT WINAPI hk_DiCreateDevice(void* self, REFGUID g, void** dev, void* outer) {
    auto create = (DiCreateDev_t)DiOrig(g_diCreateVt, g_diCreateVtN, self);
    if (!create) return E_FAIL;
    HRESULT hr = create(self, g, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev) DiHookState(*(void***)*dev);  // (another implementation's devices, e.g. a dinput8 wrapper)
    return hr;
}
// Hooks GetDeviceState in DirectInput's device class (shared by every device, the game's included, even those made
// before) through a keyboard device of our own, for both the ANSI and the Unicode interface.
static void InputHooks() {
    if (g_diHooked) return;
    g_diHooked = true;
    HMODULE m = LoadLibraryW(L"dinput8.dll");  // (the game's: a wrapper in its folder comes first, as for the game)
    typedef HRESULT(WINAPI* Create_t)(HINSTANCE, DWORD, REFIID, void**, void*);
    auto create = m ? (Create_t)(void*)GetProcAddress(m, "DirectInput8Create") : nullptr;
    if (!create) { Log("input: no dinput8.dll - input_latch does nothing"); return; }
    for (int w = 0; w < 2; w++) {
        void* di = nullptr;
        if (FAILED(create(GetModuleHandleW(nullptr), 0x0800, w ? kIID_DI8W : kIID_DI8A, &di, nullptr)) || !di) continue;
        void** divt = *(void***)di;
        void* dev = nullptr;
        if (SUCCEEDED(((DiCreateDev_t)divt[3])(di, kGUID_SysKeyboard, &dev, nullptr)) && dev) {
            DiHookState(*(void***)dev);
            ((ULONG(WINAPI*)(void*))(*(void***)dev)[2])(dev);
        }
        bool known = false;
        for (LONG i = 0; i < g_diCreateVtN; i++) known |= g_diCreateVt[i].vt == divt;
        if (!known && g_diCreateVtN < 4 && divt[3] != (void*)&hk_DiCreateDevice) {
            g_diCreateVt[g_diCreateVtN] = {divt, divt[3]};
            InterlockedIncrement(&g_diCreateVtN);
            HookVT(divt, 3, (void*)&hk_DiCreateDevice);
        }
        ((ULONG(WINAPI*)(void*))divt[2])(di);
    }
    Log("input: DirectInput hooked for input_latch (%ld device tables)", (long)g_diStateVtN);
}

// ------------------------------------------------------------------------------------------------ camera probe
// Developer tool for the vertical camera limit (docs/CAMERA.md), MezeLounge's 2019 client (mhfo-hd.dll) only. For
// camprobe_s seconds, hardware write breakpoints (debug registers DR0/DR1, set on every thread of the process) on the
// camera object's eye.x and target.x note which game code writes them, and the pitch range the camera reaches is
// logged: tilt the camera against its limits while it runs. It only reads game memory; nothing in the game is written.
// Start: key_camprobe (Ctrl+Shift+F6, developer keys) or a new camprobe_trigger value.
#include <tlhelp32.h>
static void Toast(const char* fmt, ...);
struct CpHit { uintptr_t ip; volatile LONG n; int dr; };
static CpHit g_cpHit[48];
static volatile LONG g_cpHitN, g_cpOn, g_cpRunning;
static uintptr_t g_cpCam;
static char g_cpMsg[120];  // (a message for the render thread to show: Toast isn't thread-safe)
static volatile LONG g_cpMsgReady;
static void CpToast(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(g_cpMsg, sizeof g_cpMsg, fmt, a);
    va_end(a);
    InterlockedExchange(&g_cpMsgReady, 1);
}

static LONG CALLBACK CamProbeVeh(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !g_cpOn) return EXCEPTION_CONTINUE_SEARCH;
    DWORD dr6 = e->ContextRecord->Dr6;
    if (!(dr6 & 3)) return EXCEPTION_CONTINUE_SEARCH;  // (not ours: a single step of someone else)
    uintptr_t ip = (uintptr_t)e->ExceptionRecord->ExceptionAddress;  // (the instruction after the write)
    int dr = (dr6 & 1) ? 0 : 1;
    LONG n = std::min<LONG>((LONG)g_cpHitN, 48), i = 0;
    while (i < n && !(g_cpHit[i].ip == ip && g_cpHit[i].dr == dr)) i++;
    if (i == n) {
        i = InterlockedIncrement(&g_cpHitN) - 1;
        if (i < 48) g_cpHit[i].ip = ip, g_cpHit[i].dr = dr;
    }
    if (i < 48) InterlockedIncrement(&g_cpHit[i].n);
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
// Sets (or clears) DR0/DR1 on every other thread of the process; returns how many took it.
static int CpDebugRegs(bool on) {
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    int n = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c)) {
                c.Dr7 &= ~0x00FF000Ful;  // (L0/G0/L1/G1 and their conditions)
                if (on) {
                    c.Dr0 = g_cpCam + 0x80, c.Dr1 = g_cpCam + 0x8c;
                    c.Dr7 |= 1 | 4 | (0xDul << 16) | (0xDul << 20);  // local, write (RW 01), 4 bytes (LEN 11)
                } else c.Dr0 = c.Dr1 = 0;
                if (SetThreadContext(th, &c)) n++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    return n;
}
static bool CpReadable(uintptr_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return a + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
// the camera's pitch in degrees (up positive), or NAN
static double CpPitch() {
    if (!CpReadable(g_cpCam + 0x80, 24)) return NAN;
    const float* eye = (const float*)(g_cpCam + 0x80);
    const float* tgt = (const float*)(g_cpCam + 0x8c);
    double dx = tgt[0] - eye[0], dy = tgt[1] - eye[1], dz = tgt[2] - eye[2];
    double h = sqrt(dx * dx + dz * dz);
    return h > 0 || dy != 0 ? atan2(dy, h) * 57.29577951308232 : NAN;
}
static DWORD WINAPI CamProbeThread(void*) {
    uintptr_t base = 0;
    if (C.camprobe_test) g_cpCam = (uintptr_t)&g_heartbeat - 0x80;
    else {
        HMODULE gm = GetModuleHandleA("mhfo-hd.dll");
        base = (uintptr_t)gm;
        uintptr_t slot = base + (0x1edaad60 - 0x10000000);
        if (!gm || !CpReadable(slot, 4) || !CpReadable(*(uintptr_t*)slot + 0x80, 0x24)) {
            Log("camera probe: no camera found (it needs MezeLounge's 2019 client, mhfo-hd.dll)");
            CpToast("camera probe: works on MezeLounge only");
            g_cpRunning = 0;
            return 0;
        }
        g_cpCam = *(uintptr_t*)slot;
    }
    static PVOID veh;
    if (!veh) veh = AddVectoredExceptionHandler(1, CamProbeVeh);
    g_cpHitN = 0;
    memset((void*)g_cpHit, 0, sizeof g_cpHit);
    g_cpOn = 1;
    int threads = CpDebugRegs(true);
    Log("camera probe: camera object %p, watching its eye and target on %d threads for %d s (pitch now %.1f deg)",
        (void*)g_cpCam, threads, C.camprobe_s, C.camprobe_test ? 0.0 : CpPitch());
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < C.camprobe_s * 50; i++) {
        Sleep(20);
        double p = C.camprobe_test ? NAN : CpPitch();
        if (p == p) lo = std::min(lo, p), hi = std::max(hi, p);
    }
    CpDebugRegs(false);
    Sleep(50);  // (a hit in flight on another thread)
    g_cpOn = 0;
    if (lo <= hi) Log("camera probe: pitch went from %.2f to %.2f deg", lo, hi);
    LONG n = std::min<LONG>((LONG)g_cpHitN, 48);
    for (LONG i = 0; i < n; i++) {
        char where[160];
        ModOf(g_cpHit[i].ip, where, sizeof where);
        uintptr_t ip = g_cpHit[i].ip;
        if (base && ip >= base && ip < base + 0x10000000)
            Log("camera probe: %s writes, %ld times, just before %s = static 0x%08lx", g_cpHit[i].dr ? "target" : "eye",
                (long)g_cpHit[i].n, where, (unsigned long)(ip - base + 0x10000000));
        else Log("camera probe: %s writes, %ld times, just before %s", g_cpHit[i].dr ? "target" : "eye", (long)g_cpHit[i].n, where);
    }
    if (!n) Log("camera probe: no writes seen");
    CpToast("camera probe done: %ld write sites (see mhfz-fps.log)", (long)n);
    g_cpRunning = 0;
    return 0;
}
static void CamProbeStart() {
    if (InterlockedExchange(&g_cpRunning, 1)) return;
    Toast("camera probe: tilt the camera up and down to its limits for %d s", C.camprobe_s);
    HANDLE h = CreateThread(nullptr, 0, CamProbeThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h); else g_cpRunning = 0;
}

static void WaitQ(int64_t target) {
    for (;;) {
        InputSample();
        int64_t rem = target - Qpc();
        if (rem <= 0) return;
        double ms = Ms(rem);
        if (ms > 1.6) {
            if (g_timer) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(((C.input_latch ? std::min(ms, 3.0) : ms) - 1.0) * 10000.0);  // (latch: sample every ~2 ms)
                SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(g_timer, 50);
            } else o_Sleep(1);
        } else YieldProcessor();
    }
}
// End the limiter wait. Default: finish with a real Sleep for the remaining whole milliseconds, so the wake-up
// lands on the same timer granularity as the vanilla Sleep(ms) and the game ticks at exactly its vanilla rate
// (~29.2 Hz on this PC, because Sleep oversleeps). precise_tick=1 wakes exactly on the deadline (30.3 Hz).
static void WaitDeadline(int64_t deadlineQ, DWORD tgDeadline) {
    WhereScope ws("WaitDeadline");
    if (C.precise_tick) {
        WaitQ(deadlineQ - (int64_t)(g_qpf.QuadPart / 2000));
        while ((int32_t)(MsClock() - tgDeadline) < 0) YieldProcessor();
        return;
    }
    int32_t rem = (int32_t)(tgDeadline - MsClock());
    if (rem > 0) o_Sleep((DWORD)rem);
}

static double TargetFps() {
    if (C.target_fps > 0) return C.target_fps;
    return std::max(60.0, g_refresh * 0.97);
}

// Debug capture: copies the next N displayed frames (interpolated and real) into video memory (cheap, so the
// game keeps its normal pacing), then writes them all as PNGs via d3dx9_43 once the sequence is complete.
static int g_capLeft, g_capIdx;
static bool g_capFlush;
struct CapShot { IDirect3DSurface9* s; char name[48]; };
static std::vector<CapShot> g_capShots;
typedef HRESULT(WINAPI* SaveSurf_t)(LPCSTR, DWORD, IDirect3DSurface9*, const PALETTEENTRY*, const RECT*);
// Rolling recorder: the last retro_frames displayed frames (half resolution) stay in video memory; the capture
// key saves them, so a glitch can be caught after it has been seen. Per-tick matching statistics go along.
struct RetroShot { IDirect3DSurface9* s = nullptr; uint32_t tick = 0; float t = 0; bool real = false; };
static std::vector<RetroShot> g_retro;
static size_t g_retroPos;
static void RetroGrab(bool real, float t) {
    if (!C.retro || !g_dev) return;
    if (g_retro.size() != (size_t)std::max(C.retro_frames, 8)) {
        for (auto& r : g_retro) if (r.s) r.s->Release();
        g_retro.assign(std::max(C.retro_frames, 8), RetroShot());
        g_retroPos = 0;
    }
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    UINT w = std::max<UINT>(d.Width / 2, 8), h = std::max<UINT>(d.Height / 2, 8);
    RetroShot& r = g_retro[g_retroPos % g_retro.size()];
    if (r.s) {
        D3DSURFACE_DESC rd;
        r.s->GetDesc(&rd);
        if (rd.Width != w || rd.Height != h || rd.Format != d.Format) { r.s->Release(); r.s = nullptr; }
    }
    if (r.s || SUCCEEDED(g_dev->CreateRenderTarget(w, h, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &r.s, nullptr))) {
        o_StretchRect(g_dev, bb, nullptr, r.s, nullptr, D3DTEXF_LINEAR);
        r.tick = g_tickNo;
        r.t = t;
        r.real = real;
        g_retroPos++;
    }
    bb->Release();
}
static void RetroRelease() {
    for (auto& r : g_retro) if (r.s) { r.s->Release(); r.s = nullptr; }
    g_retroPos = 0;
}
// ... and the recordings of the last retro_ticks frames (copied when each is complete, at the limiter's Sleep), saved
// next to them as ticks.seq: the matching of the ticks a glitch was seen in runs again offline (pairbench -seq).
struct TickRec {
    uint32_t tick = 0, serial = 0, nvscf = 0;
    void *startVS = nullptr, *startPS = nullptr;
    std::vector<uint8_t> buf;
    std::vector<DrawRec> draws;
    std::vector<uint64_t> ckeys;  // (its glyphs by their pixels, when made)
};
static std::vector<TickRec> g_retroTicks;
static size_t g_retroTickPos;
static void RetroKeepTick(const Frame& N) {
    if (!C.retro || C.retro_ticks <= 0) return;
    if (g_retroTicks.size() != (size_t)C.retro_ticks) {
        g_retroTicks.assign(C.retro_ticks, TickRec());
        g_retroTickPos = 0;
    }
    TickRec& r = g_retroTicks[g_retroTickPos++ % g_retroTicks.size()];
    r.tick = g_tickNo;
    r.serial = N.serial;
    r.nvscf = N.nvscf;
    r.startVS = N.startVS;
    r.startPS = N.startPS;
    r.buf.assign(N.buf.begin(), N.buf.end());
    r.draws.assign(N.draws.begin(), N.draws.end());
    if (N.ckeysDone && N.ckeys.size() == N.draws.size()) r.ckeys.assign(N.ckeys.begin(), N.ckeys.end());
    else r.ckeys.clear();
}
static void RetroDropTicks(int keep) {  // (keep: how many from now on; the buffers held so far are given back)
    std::vector<TickRec>().swap(g_retroTicks);
    g_retroTickPos = 0;
    C.retro_ticks = keep;
}
static void RetroSaveTicks(const char* dir) {
    if (g_retroTicks.empty() || !g_retroTickPos) return;
    PairJoin();  // (the shader tables: the matching may add to them)
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\ticks.seq", dir);
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); };
    fwrite("MHSQ", 4, 1, fp);
    w32(2);  // (2: each tick's glyph keys follow its draws)
    w32(sizeof(Config));
    fwrite(&C, sizeof(Config), 1, fp);
    w32((uint32_t)g_vsInfo.size());
    w32(sizeof(VSInfo));
    for (auto& kv : g_vsInfo) { w32((uint32_t)(uintptr_t)kv.first); fwrite(&kv.second, sizeof(VSInfo), 1, fp); }
    w32((uint32_t)g_psInfo.size());
    w32(sizeof(PSEntry));
    for (auto& kv : g_psInfo) { w32((uint32_t)(uintptr_t)kv.first); fwrite(&kv.second, sizeof(PSEntry), 1, fp); }
    w32(sizeof(DrawRec));
    size_t n = std::min(g_retroTickPos, g_retroTicks.size());
    w32((uint32_t)n);
    for (size_t k = 0; k < n; k++) {  // oldest first
        const TickRec& r = g_retroTicks[(g_retroTickPos - n + k) % g_retroTicks.size()];
        w32(r.tick);
        w32(r.serial);
        w32(r.nvscf);
        w32((uint32_t)(uintptr_t)r.startVS);
        w32((uint32_t)(uintptr_t)r.startPS);
        w32((uint32_t)r.buf.size());
        fwrite(r.buf.data(), 1, r.buf.size(), fp);
        w32((uint32_t)r.draws.size());
        fwrite(r.draws.data(), sizeof(DrawRec), r.draws.size(), fp);
        w32((uint32_t)r.ckeys.size());
        fwrite(r.ckeys.data(), 8, r.ckeys.size(), fp);
    }
    fclose(fp);
    Log("retro: saved the recordings of %zu ticks to %s", n, path);
}
static int RetroSave() {  // (returns the clip's number, 0: nothing saved)
    WhereScope ws("RetroSave");
    static int session = 0;
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    auto save = dx ? (SaveSurf_t)GetProcAddress(dx, "D3DXSaveSurfaceToFileA") : nullptr;
    if (!save || g_retro.empty()) return 0;
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\retro", g_dir);
    CreateDirectoryA(dir, nullptr);
    snprintf(dir, sizeof dir, "%s\\retro\\%03d", g_dir, ++session);
    CreateDirectoryA(dir, nullptr);
    size_t n = std::min(g_retroPos, g_retro.size()), saved = 0;
    for (size_t k = 0; k < n; k++) {
        const RetroShot& r = g_retro[(g_retroPos - n + k) % g_retro.size()];
        if (!r.s) continue;
        snprintf(path, sizeof path, "%s\\%04zu_tick%06u_%s_%03d.jpg", dir, k, r.tick, r.real ? "r" : "i", (int)(r.t * 100 + 0.5f));
        if (SUCCEEDED(save(path, 1 /*JPG*/, r.s, nullptr, nullptr))) saved++;
    }
    snprintf(path, sizeof path, "%s\\ticks.txt", dir);
    if (FILE* fp = fopen(path, "w")) {
        fprintf(fp, "tick draws unmatched rejected camera-estimated pairs cut\n");
        size_t m = std::min(g_tickStatPos, g_tickStats.size());
        for (size_t k = 0; k < m; k++) {
            const TickStat& s = g_tickStats[(g_tickStatPos - m + k) % g_tickStats.size()];
            fprintf(fp, "%u %u %u %u %u %u %d\n", s.tick, s.draws, s.unmatched, s.rejected, s.synth, s.pairs, s.cut);
        }
        fclose(fp);
    }
    snprintf(path, sizeof path, "%s\\sweeps.txt", dir);
    if (FILE* fp = fopen(path, "w")) {
        fprintf(fp, "tick draw kind(m=matched s=camera-estimated) prev_ndc_x prev_ndc_y prev_w -> cur_ndc_x cur_ndc_y cur_w\n");
        size_t m = std::min(g_sweepPos, g_sweeps.size());
        for (size_t k = 0; k < m; k++) {
            const Sweep& w = g_sweeps[(g_sweepPos - m + k) % g_sweeps.size()];
            fprintf(fp, "%u %u %c %.3f %.3f %.1f -> %.3f %.3f %.1f\n", w.tick, w.draw, w.kind, w.px, w.py, w.pw, w.cx, w.cy, w.cw);
        }
        fclose(fp);
    }
    Log("retro: saved %zu frames to %s", saved, dir);
    RetroSaveTicks(dir);
    // and the command dump of the next frame pair (what was matched to what), next to the frames
    snprintf(g_dumpTo, sizeof g_dumpTo, "%s\\dump.txt", dir);
    g_dumpNext = true;
    return saved ? session : 0;
}

static void Capture(const char* tag, float t) {
    RetroGrab(tag[0] == 'r', t);
    if (g_capLeft <= 0) return;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    IDirect3DSurface9* s = nullptr;
    if (SUCCEEDED(g_dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr))) {
        o_StretchRect(g_dev, bb, nullptr, s, nullptr, D3DTEXF_NONE);
        CapShot c{s, {}};
        snprintf(c.name, sizeof c.name, "%04d_%s_%03d.png", g_capIdx++, tag, (int)(t * 100 + 0.5f));
        g_capShots.push_back(c);
    }
    bb->Release();
    if (--g_capLeft == 0) g_capFlush = true;
}
static void FlushCaptures(bool save) {
    static SaveSurf_t fn = nullptr;
    if (save && !fn) {
        HMODULE dx = LoadLibraryA("d3dx9_43.dll");
        fn = dx ? (SaveSurf_t)GetProcAddress(dx, "D3DXSaveSurfaceToFileA") : nullptr;
    }
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\captures", g_dir);
    CreateDirectoryA(dir, nullptr);
    for (auto& c : g_capShots) {
        if (save && fn) {
            snprintf(path, sizeof path, "%s\\%s", dir, c.name);
            fn(path, 3 /*D3DXIFF_PNG*/, c.s, nullptr, nullptr);
        }
        c.s->Release();
    }
    if (save) Log("capture written (%zu frames)", g_capShots.size());
    g_capShots.clear();
    g_capFlush = false;
}

// Diagnostic variants for a live A/B test (Ctrl+Shift+F7 cycles; a square on the in-between frames shows which):
// 0 normal, 1 smooth constants switch at the tick (red), 2 pixel-shader constants not blended (green),
// 3 CPU-built quads not interpolated (blue), 4 bones blended linearly (yellow), 5 every pass but the main scene image
// drawn without interpolation (purple), 6 only the main scene image drawn without interpolation (cyan)
static void ApplyVariant() {
    if (g_variant == 1) C.lerp_nonpos = 0;
    if (g_variant == 2) C.interp_ps = 0;
    if (g_variant == 3) C.interp_up = 0;
    if (g_variant == 4) C.slerp_bones = 0;
}
static void Marker(D3DCOLOR col) {
    if (!g_bb || (!C.debug_marker && !g_variant)) return;
    static const D3DCOLOR vc[7] = {0, D3DCOLOR_XRGB(255, 0, 0), D3DCOLOR_XRGB(0, 255, 0), D3DCOLOR_XRGB(0, 64, 255), D3DCOLOR_XRGB(255, 255, 0), D3DCOLOR_XRGB(170, 0, 255), D3DCOLOR_XRGB(0, 255, 255)};
    if (g_variant) col = vc[g_variant];
    o_SetRenderTarget(g_dev, 0, g_bb);
    D3DRECT r{8, 8, 56, 56};
    o_Clear(g_dev, 1, &r, D3DCLEAR_TARGET, col, 1.0f, 0);
}

// latency_trim: the hold after each real frame - the smallest wait for the first in-between slot over the last second
// (measured as if the game had not been held), less 1 ms to spare, at most 8 ms
static double g_ltHold, g_ltHeld;  // (ms: the next hold, this tick's)
static double g_ltWin[30];
static int g_ltN;
static void LatencyNote(double rawSlack) {
    g_ltWin[g_ltN++ % 30] = rawSlack;
    int n = std::min(g_ltN, 30);
    double m = 1e9;
    for (int k = 0; k < n; k++) m = std::min(m, g_ltWin[k]);
    g_ltHold = n >= 10 ? std::min(8.0, std::max(0.0, m - 1.0)) : 0.0;  // (a second of ticks before holding at all)
}
// ------------------------------------------------------------------------------------------------ fps overlay
static double TargetFps();
// Toggle with key_overlay (Insert): the true presented frame rate (game frames + in-between frames), the game's
// own rate, frame-time stats and a graph of the last frame intervals (blue = game frame, green = in-between frame,
// red = a hitch). Drawn into the backbuffer right before every Present with the runtime's own functions (nothing is
// recorded or replayed) and all state put back afterwards.
static const int kOvHist = 512;
static int64_t g_ovT[kOvHist];
static bool g_ovReal[kOvHist];
static uint32_t g_ovN;
static double g_ovPairMs, g_ovLogicMs, g_ovExtra;  // per-tick averages (EMA) for the overlay
static IDirect3DTexture9* g_ovTex;                 // glyph atlas (managed pool: survives Reset)
static IDirect3DStateBlock9* g_ovSB;
static int g_ovCW, g_ovCH;                         // glyph cell size in the atlas
static const int kOvAtlasW = 256, kOvAtlasH = 128;
static char g_ovLine[3][128];
static int64_t g_ovLineAt;
static double g_ovFps, g_ovLow, g_ovGame;  // (shown: frames per second, the slowest 1%'s rate, the game's own rate)

static void OvRecord(bool real) {
    g_ovT[g_ovN % kOvHist] = Qpc();
    g_ovReal[g_ovN % kOvHist] = real;
    g_ovN++;
}

// ASCII 32..126 rendered with GDI into a 16x6 grid; the last cell (127) is solid white for boxes and bars.
static bool OvAtlas() {
    if (g_ovTex) return true;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HFONT f = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32A(dc, "M", 1, &sz);
    g_ovCW = std::min<int>(sz.cx, kOvAtlasW / 16);
    g_ovCH = std::min<int>(sz.cy, kOvAtlasH / 6);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = kOvAtlasW;
    bi.bmiHeader.biHeight = -kOvAtlasH;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bm && bits) {
        HGDIOBJ ob = SelectObject(dc, bm);
        memset(bits, 0, kOvAtlasW * kOvAtlasH * 4);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        for (int ch = 32; ch < 127; ch++) {
            char s = (char)ch;
            TextOutA(dc, ((ch - 32) % 16) * g_ovCW, ((ch - 32) / 16) * g_ovCH, &s, 1);
        }
        GdiFlush();
        uint32_t* px = (uint32_t*)bits;
        int wx = 15 * g_ovCW, wy = 5 * g_ovCH;  // cell of char 127
        for (int y = wy; y < wy + g_ovCH; y++)
            for (int x = wx; x < wx + g_ovCW; x++) px[y * kOvAtlasW + x] = 0xffffff;
        if (SUCCEEDED(g_dev->CreateTexture(kOvAtlasW, kOvAtlasH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_ovTex, nullptr))) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(g_ovTex->LockRect(0, &lr, nullptr, 0))) {
                for (int y = 0; y < kOvAtlasH; y++) {
                    uint32_t* dst = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
                    for (int x = 0; x < kOvAtlasW; x++) {
                        uint32_t v = px[y * kOvAtlasW + x];
                        uint32_t a = std::max(std::max(v & 0xff, (v >> 8) & 0xff), (v >> 16) & 0xff);
                        dst[x] = (a << 24) | 0xffffff;
                    }
                }
                g_ovTex->UnlockRect(0);
                ok = true;
            }
        }
        SelectObject(dc, ob);
        DeleteObject(bm);
    }
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteDC(dc);
    if (!ok && g_ovTex) { g_ovTex->Release(); g_ovTex = nullptr; }
    if (!ok) Log("overlay: glyph atlas failed");
    return ok;
}

static void OvRelease() {
    if (g_ovSB) { g_ovSB->Release(); g_ovSB = nullptr; }
    if (g_ovTex) { g_ovTex->Release(); g_ovTex = nullptr; }
}
struct OvV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
static std::vector<OvV> g_ovV;
static void OvQuad(float x, float y, float w, float h, D3DCOLOR c, int cell = 95) {  // cell 95 = solid white
    float u0 = (float)((cell % 16) * g_ovCW) / kOvAtlasW, v0 = (float)((cell / 16) * g_ovCH) / kOvAtlasH;
    float u1 = u0 + (float)g_ovCW / kOvAtlasW, v1 = v0 + (float)g_ovCH / kOvAtlasH;
    if (cell == 95) { u0 += 1.0f / kOvAtlasW; v0 += 1.0f / kOvAtlasH; u1 -= 1.0f / kOvAtlasW; v1 -= 1.0f / kOvAtlasH; }
    x -= 0.5f;
    y -= 0.5f;
    OvV q[6] = {{x, y, 0, 1, c, u0, v0}, {x + w, y, 0, 1, c, u1, v0}, {x, y + h, 0, 1, c, u0, v1},
                {x + w, y, 0, 1, c, u1, v0}, {x + w, y + h, 0, 1, c, u1, v1}, {x, y + h, 0, 1, c, u0, v1}};
    g_ovV.insert(g_ovV.end(), q, q + 6);
}
static void OvText(float x, float y, float s, const char* t, D3DCOLOR c) {
    for (; *t; t++, x += g_ovCW * s) {
        int ch = (unsigned char)*t;
        if (ch <= 32 || ch > 126) continue;
        OvQuad(x + s, y + s, g_ovCW * s, g_ovCH * s, D3DCOLOR_ARGB(200, 0, 0, 0), ch - 32);  // shadow
        OvQuad(x, y, g_ovCW * s, g_ovCH * s, c, ch - 32);
    }
}

static char g_toast[120];
static int64_t g_toastUntil;
static void Toast(const char* fmt, ...) {  // (also logged)
    va_list a;
    va_start(a, fmt);
    vsnprintf(g_toast, sizeof g_toast, fmt, a);
    va_end(a);
    g_toastUntil = Qpc() + g_qpf.QuadPart * 5 / 2;
    Log("hotkey: %s", g_toast);
}
static void DrawOverlay() {
    int64_t now = Qpc();
    bool toast = g_toastUntil && now < g_toastUntil;
    if ((!C.overlay && !toast) || !g_dev || g_lost || !OvAtlas()) return;
    uint32_t n = std::min<uint32_t>(g_ovN, kOvHist);
    // frame stats over the last second, refreshed 4x per second so the numbers stay readable
    if (Ms(now - g_ovLineAt) >= 250 && n >= 2) {
        g_ovLineAt = now;
        int frames = 0, real = 0;
        double sum = 0, mx = 0, sumsq = 0;
        int64_t newest = g_ovT[(g_ovN - 1) % kOvHist], oldest = newest;
        for (uint32_t k = 1; k < n; k++) {
            uint32_t i = (g_ovN - k) % kOvHist, j = (g_ovN - k - 1) % kOvHist;
            if (Ms(newest - g_ovT[j]) > 1000) break;
            double d = Ms(g_ovT[i] - g_ovT[j]);
            frames++;
            real += g_ovReal[i];
            sum += d;
            sumsq += d * d;
            mx = std::max(mx, d);
            oldest = g_ovT[j];
        }
        double span = Ms(newest - oldest) / 1000.0, avg = frames ? sum / frames : 0;
        double sd = frames ? std::sqrt(std::max(0.0, sumsq / frames - avg * avg)) : 0;
        // "1% low": the frame rate of the slowest 1% of frames over the last few seconds (what a stutter feels like)
        {
            static std::vector<double> iv;
            iv.clear();
            for (uint32_t k = 1; k < n; k++) {
                uint32_t i = (g_ovN - k) % kOvHist, j = (g_ovN - k - 1) % kOvHist;
                if (Ms(newest - g_ovT[j]) > 4000) break;
                iv.push_back(Ms(g_ovT[i] - g_ovT[j]));
            }
            if (iv.size() >= 20) {
                size_t at = iv.size() - 1 - iv.size() / 100;
                std::nth_element(iv.begin(), iv.begin() + at, iv.end());
                g_ovLow = iv[at] > 0 ? 1000.0 / iv[at] : 0;
            }
        }
        g_ovFps = span > 0 ? frames / span : 0.0;
        g_ovGame = span > 0 ? real / span : 0.0;
        snprintf(g_ovLine[0], sizeof g_ovLine[0], "%5.1f fps  1%% low %3.0f  (game %4.1f)  target %.0f", g_ovFps, g_ovLow, g_ovGame, TargetFps());
        snprintf(g_ovLine[1], sizeof g_ovLine[1], "frame %4.2f ms  max %5.2f  jitter %4.2f", avg, mx, sd);
        snprintf(g_ovLine[2], sizeof g_ovLine[2], "%s  pair %4.2f  logic %4.2f  extra %.1f%s", !C.enabled ? "mod OFF" : C.mode == 2 ? "interp " : C.mode == 1 ? "repeat " : "pass   ",
                 g_ovPairMs, g_ovLogicMs, g_ovExtra, C.latency_trim ? "  hold" : "");
        if (C.latency_trim) { size_t l = strlen(g_ovLine[2]); snprintf(g_ovLine[2] + l, sizeof g_ovLine[2] - l, " %.1f", g_ltHeld); }
    }
    IDirect3DSurface9 *bb = nullptr, *rt = nullptr, *ds = nullptr;
    if (FAILED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd;
    bb->GetDesc(&bd);
    float s = C.overlay_scale > 0 ? (float)C.overlay_scale : (float)std::max<int>(1, (int)(bd.Height + 540) / 1080);
    // layout
    const int kBars = 240;
    float pad = 6 * s, lineH = g_ovCH * s, textW = 40 * g_ovCW * s;
    float graphW = std::max(textW, (float)kBars * s), graphH = 80 * s;
    float W = graphW + 2 * pad, H = 3 * lineH + graphH + 3 * pad;
    float X = 8 * s, Y = 8 * s;
    switch (C.overlay_pos) {
        case 1: X = bd.Width - W - 8 * s; break;                                    // top right
        case 2: Y = bd.Height - H - 8 * s; break;                                   // bottom left
        case 3: X = bd.Width - W - 8 * s; Y = bd.Height - H - 8 * s; break;          // bottom right
        case 4: X = (bd.Width - W) / 2; break;                                       // top centre
    }
    g_ovV.clear();
    if (toast) {  // top centre, twice the overlay's size
        float ts = 2 * s, tw = strlen(g_toast) * g_ovCW * ts, th = g_ovCH * ts, tx = (bd.Width - tw) / 2, ty = 40 * s;
        OvQuad(tx - 8 * s, ty - 6 * s, tw + 16 * s, th + 12 * s, D3DCOLOR_ARGB(170, 0, 0, 0));
        OvText(tx, ty, ts, g_toast, D3DCOLOR_XRGB(255, 230, 120));
    }
    if (C.overlay == 1) {  // the fps counter: the rate in large type (green near the target, yellow, red), the slowest 1%'s
                           // rate and the game's own rate under it
        char big[32], small[64];
        snprintf(big, sizeof big, "%.0f FPS", g_ovFps);
        snprintf(small, sizeof small, "1%% low %.0f  game %.0f", g_ovLow, g_ovGame);
        float bs = 2 * s, w1 = strlen(big) * g_ovCW * bs, w2 = strlen(small) * g_ovCW * s;
        float cw = std::max(w1, w2) + 2 * pad, ch = g_ovCH * bs + g_ovCH * s + 2 * pad;
        float cx = 8 * s, cy = 8 * s;
        switch (C.overlay_pos) {
            case 1: cx = bd.Width - cw - 8 * s; break;
            case 2: cy = bd.Height - ch - 8 * s; break;
            case 3: cx = bd.Width - cw - 8 * s; cy = bd.Height - ch - 8 * s; break;
            case 4: cx = (bd.Width - cw) / 2; break;
        }
        double tf = TargetFps(), r = tf > 0 ? g_ovFps / tf : 1;
        D3DCOLOR col = r >= 0.8 ? D3DCOLOR_XRGB(120, 255, 140) : r >= 0.5 ? D3DCOLOR_XRGB(255, 220, 90) : D3DCOLOR_XRGB(255, 110, 90);
        OvQuad(cx, cy, cw, ch, D3DCOLOR_ARGB(140, 0, 0, 0));
        OvText(cx + pad, cy + pad, bs, big, col);
        OvText(cx + pad, cy + pad + g_ovCH * bs, s, small, D3DCOLOR_XRGB(200, 200, 200));
    }
    if (C.overlay == 2) {
    OvQuad(X, Y, W, H, D3DCOLOR_ARGB(160, 0, 0, 0));
    for (int l = 0; l < 3; l++) OvText(X + pad, Y + pad + l * lineH, s, g_ovLine[l], l == 0 ? D3DCOLOR_XRGB(255, 255, 255) : D3DCOLOR_XRGB(200, 200, 200));
    // graph: 0..25 ms (longer frames reach the top), lines at the target frame time (green) and 60 fps (grey)
    const float kRange = 25.0f;
    float gx = X + pad, gy = Y + 2 * pad + 3 * lineH, msPx = graphH / kRange;
    OvQuad(gx, gy, graphW, graphH, D3DCOLOR_ARGB(90, 40, 40, 40));
    double target = 1000.0 / TargetFps();
    OvQuad(gx, gy + graphH - (float)target * msPx, graphW, s, D3DCOLOR_ARGB(160, 0, 255, 0));
    OvQuad(gx, gy + graphH - 16.67f * msPx, graphW, s, D3DCOLOR_ARGB(160, 160, 160, 160));
    float bw = graphW / kBars;
    for (int k = 0; k < kBars && k + 1 < (int)n; k++) {
        uint32_t i = (g_ovN - 1 - k) % kOvHist, j = (g_ovN - 2 - k) % kOvHist;
        double d = Ms(g_ovT[i] - g_ovT[j]);
        float h = (float)std::min(d, (double)kRange) * msPx;
        D3DCOLOR c = d > std::max(target * 1.8, target + 4.0) ? D3DCOLOR_XRGB(255, 70, 50)
                     : g_ovReal[i] ? D3DCOLOR_XRGB(90, 170, 255) : D3DCOLOR_XRGB(80, 230, 110);
        OvQuad(gx + graphW - (k + 1) * bw, gy + graphH - h, std::max(bw - (bw > 2 ? 1.0f : 0.0f), 1.0f), std::max(h, 1.0f), c);
    }
    }
    // draw with the runtime's own functions, then put every state back
    if (!g_ovSB && FAILED(o_CreateStateBlock(g_dev, D3DSBT_ALL, &g_ovSB))) { bb->Release(); return; }
    o_SBCapture(g_ovSB);
    g_dev->GetRenderTarget(0, &rt);
    g_dev->GetDepthStencilSurface(&ds);
    D3DVIEWPORT9 vp;
    RECT sc;
    g_dev->GetViewport(&vp);
    g_dev->GetScissorRect(&sc);
    o_SetRenderTarget(g_dev, 0, bb);
    o_SetDepthStencilSurface(g_dev, nullptr);
    if (SUCCEEDED(o_BeginScene(g_dev))) {
        static const DWORD rs[][2] = {
            {D3DRS_ZENABLE, FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, TRUE}, {D3DRS_SRCBLEND, D3DBLEND_SRCALPHA},
            {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_BLENDOP, D3DBLENDOP_ADD}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
            {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_LIGHTING, FALSE}, {D3DRS_FOGENABLE, FALSE},
            {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 0xF}, {D3DRS_SRGBWRITEENABLE, FALSE},
            {D3DRS_FILLMODE, D3DFILL_SOLID}, {D3DRS_CLIPPLANEENABLE, 0}, {D3DRS_SPECULARENABLE, FALSE}, {D3DRS_TEXTUREFACTOR, 0xffffffff}};
        for (auto& r : rs) o_SetRenderState(g_dev, (D3DRENDERSTATETYPE)r[0], r[1]);
        o_SetVertexShader(g_dev, nullptr);
        o_SetPixelShader(g_dev, nullptr);
        o_SetFVF(g_dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        o_SetTexture(g_dev, 0, g_ovTex);
        for (DWORD st = 1; st < 8; st++) o_SetTexture(g_dev, st, nullptr);
        o_SetTextureStageState(g_dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        o_SetTextureStageState(g_dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        o_SetTextureStageState(g_dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        o_SetTextureStageState(g_dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        o_SetTextureStageState(g_dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        o_SetTextureStageState(g_dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        o_SetTextureStageState(g_dev, 0, D3DTSS_TEXCOORDINDEX, 0);
        o_SetTextureStageState(g_dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        o_SetTextureStageState(g_dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        o_SetTextureStageState(g_dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        o_SetSamplerState(g_dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        o_SetSamplerState(g_dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        o_SetSamplerState(g_dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        o_SetSamplerState(g_dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        o_SetSamplerState(g_dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        o_SetSamplerState(g_dev, 0, D3DSAMP_SRGBTEXTURE, FALSE);
        D3DVIEWPORT9 full{0, 0, bd.Width, bd.Height, 0.0f, 1.0f};
        o_SetViewport(g_dev, &full);
        if (!g_ovV.empty()) o_DrawPrimitiveUP(g_dev, D3DPT_TRIANGLELIST, (UINT)(g_ovV.size() / 3), g_ovV.data(), sizeof(OvV));
        o_EndScene(g_dev);
    }
    o_SetRenderTarget(g_dev, 0, rt);
    o_SetDepthStencilSurface(g_dev, ds);
    o_SBApply(g_ovSB);
    o_SetViewport(g_dev, &vp);
    o_SetScissorRect(g_dev, &sc);
    if (rt) rt->Release();
    if (ds) ds->Release();
    bb->Release();
}

// ------------------------------------------------------------------------------------------------ self-check
// Ground truth for an interpolated frame does not exist, except for frames the game really rendered: predict
// real frame M by interpolating M-1 and M+1 halfway, and compare with M as presented. Wrong pairings, smears,
// voids and flicker show up as localized errors; the score lets settings be compared objectively.
static IDirect3DSurface9 *g_sysA, *g_sysB, *g_sysH;
static int64_t g_lastCheck;
static int g_checkN, g_checkSaves;
static double g_checkBadSum, g_checkMeanSum;
static int g_checkCount;

static bool EnsureSys(IDirect3DSurface9*& s, UINT w, UINT h, D3DFORMAT f) {
    if (s) {
        D3DSURFACE_DESC d;
        s->GetDesc(&d);
        if (d.Width == w && d.Height == h && d.Format == f) return true;
        s->Release();
        s = nullptr;
    }
    return SUCCEEDED(g_dev->CreateOffscreenPlainSurface(w, h, f, D3DPOOL_SYSTEMMEM, &s, nullptr));
}

static IDirect3DSurface9 *g_smA, *g_smB;  // quarter-resolution copies (cheap readback)
static bool EnsureRT(IDirect3DSurface9*& s, UINT w, UINT h, D3DFORMAT f) {
    if (s) {
        D3DSURFACE_DESC d;
        s->GetDesc(&d);
        if (d.Width == w && d.Height == h && d.Format == f) return true;
        s->Release();
        s = nullptr;
    }
    return SUCCEEDED(g_dev->CreateRenderTarget(w, h, f, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr));
}
static void ReleaseCheckSurfaces() {
    for (IDirect3DSurface9** s : {&g_smA, &g_smB}) if (*s) { (*s)->Release(); *s = nullptr; }
}
static int g_cmpBright, g_cmpDark;  // last comparison: blocks predicted clearly brighter / darker than the real frame
static bool CompareWithReal(IDirect3DSurface9* real, bool quiet = false, double* meanOut = nullptr, double* biasOut = nullptr) {
    D3DSURFACE_DESC d;
    g_bb->GetDesc(&d);
    if (d.Format != D3DFMT_X8R8G8B8 && d.Format != D3DFMT_A8R8G8B8) return false;
    d.Width = std::max<UINT>(d.Width / 4, 16);
    d.Height = std::max<UINT>(d.Height / 4, 16);
    if (!EnsureRT(g_smA, d.Width, d.Height, d.Format) || !EnsureRT(g_smB, d.Width, d.Height, d.Format)) return false;
    if (!EnsureSys(g_sysA, d.Width, d.Height, d.Format) || !EnsureSys(g_sysB, d.Width, d.Height, d.Format) ||
        !EnsureSys(g_sysH, d.Width, d.Height, D3DFMT_X8R8G8B8))
        return false;
    if (FAILED(o_StretchRect(g_dev, g_bb, nullptr, g_smA, nullptr, D3DTEXF_LINEAR)) ||
        FAILED(o_StretchRect(g_dev, real, nullptr, g_smB, nullptr, D3DTEXF_LINEAR)))
        return false;
    if (FAILED(g_dev->GetRenderTargetData(g_smA, g_sysA)) || FAILED(g_dev->GetRenderTargetData(g_smB, g_sysB))) return false;
    D3DLOCKED_RECT la, lb, lh;
    if (FAILED(g_sysA->LockRect(&la, nullptr, D3DLOCK_READONLY))) return false;
    if (FAILED(g_sysB->LockRect(&lb, nullptr, D3DLOCK_READONLY))) { g_sysA->UnlockRect(); return false; }
    if (FAILED(g_sysH->LockRect(&lh, nullptr, 0))) { g_sysA->UnlockRect(); g_sysB->UnlockRect(); return false; }
    const int BS = 4;  // 4x4 at quarter resolution = 16x16 at full
    int W = (int)d.Width, H = (int)d.Height, bw = (W + BS - 1) / BS, bh = (H + BS - 1) / BS;
    std::vector<float> blk((size_t)bw * bh, 0.0f), sblk((size_t)bw * bh, 0.0f);
    double total = 0, signedSum = 0;
    for (int y = 0; y < H; y++) {
        const uint8_t* pa = (const uint8_t*)la.pBits + (size_t)y * la.Pitch;
        const uint8_t* pb = (const uint8_t*)lb.pBits + (size_t)y * lb.Pitch;
        uint8_t* ph = (uint8_t*)lh.pBits + (size_t)y * lh.Pitch;
        float* brow = &blk[(size_t)(y / BS) * bw];
        for (int x = 0; x < W; x++, pa += 4, pb += 4, ph += 4) {
            int e = (std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) + std::abs(pa[2] - pb[2])) / 3;
            double sd = ((int)pa[0] + pa[1] + pa[2] - pb[0] - pb[1] - pb[2]) / 3.0;
            signedSum += sd;
            sblk[(size_t)(y / BS) * bw + x / BS] += (float)sd;
            total += e;
            brow[x / BS] += (float)e;
            int gray = (pb[0] + pb[1] + pb[2]) / 9;  // dimmed real frame
            ph[0] = (uint8_t)gray;
            ph[1] = (uint8_t)gray;
            ph[2] = (uint8_t)std::min(255, gray + e * 4);
            ph[3] = 255;
        }
    }
    g_sysA->UnlockRect();
    g_sysB->UnlockRect();
    g_sysH->UnlockRect();
    int bad = 0;
    for (float& b : blk) {
        b /= (float)(BS * BS);
        if (b > 24.0f) bad++;
    }
    std::vector<float> sorted = blk;
    size_t k99 = sorted.size() * 99 / 100;
    std::nth_element(sorted.begin(), sorted.begin() + k99, sorted.end());
    double mean = total / ((double)W * H);
    double badPct = 100.0 * bad / (double)blk.size();
    if (meanOut) *meanOut = mean;
    if (biasOut) *biasOut = signedSum / ((double)W * H);
    g_cmpBright = g_cmpDark = 0;
    for (float s : sblk) {
        s /= (float)(BS * BS);
        if (s > 8.0f) g_cmpBright++;
        else if (s < -8.0f) g_cmpDark++;
    }
    if (quiet) return false;
    g_checkN++;
    g_checkCount++;
    g_checkBadSum += badPct;
    g_checkMeanSum += mean;
    Log("selfcheck #%d: mean error %.2f, p99 block %.1f, bad blocks %.2f%%", g_checkN, mean, sorted[k99], badPct);
    if (badPct > C.selfcheck_save_pct && g_checkSaves < C.selfcheck_saves) {
        HMODULE dx = LoadLibraryA("d3dx9_43.dll");
        auto save = dx ? (SaveSurf_t)GetProcAddress(dx, "D3DXSaveSurfaceToFileA") : nullptr;
        if (save) {
            char dir[MAX_PATH], path[MAX_PATH];
            snprintf(dir, sizeof dir, "%s\\selfcheck", g_dir);
            CreateDirectoryA(dir, nullptr);
            snprintf(path, sizeof path, "%s\\%04d_predicted.jpg", dir, g_checkN);
            save(path, 1 /*D3DXIFF_JPG*/, g_sysA, nullptr, nullptr);
            snprintf(path, sizeof path, "%s\\%04d_real.jpg", dir, g_checkN);
            save(path, 1, g_sysB, nullptr, nullptr);
            snprintf(path, sizeof path, "%s\\%04d_heat.jpg", dir, g_checkN);
            save(path, 1, g_sysH, nullptr, nullptr);
            g_checkSaves++;
            Log("selfcheck #%d: images saved (%d/%d)", g_checkN, g_checkSaves, C.selfcheck_saves);
            return true;
        }
    }
    return false;
}

// Disassembly of the vertex shaders used by a frame, next to what the analysis concluded.
typedef HRESULT(WINAPI* Disasm_t)(const DWORD*, BOOL, LPCSTR, IUnknown**);
static void DumpShaders(const char* path, const Frame& f) {
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    auto dis = dx ? (Disasm_t)GetProcAddress(dx, "D3DXDisassembleShader") : nullptr;
    FILE* fp = fopen(path, "w");
    if (!fp) return;
    std::unordered_map<void*, int> used;
    for (auto& d : f.draws) used[d.vs]++;
    for (auto& kv : used) {
        const VSInfo* vi = GetVSInfo(kv.first);
        fprintf(fp, "==== vs %p used by %d draws, analysis: all=%d pos:", kv.first, kv.second, vi ? vi->all : -1);
        for (int r = 0; vi && r < 256; r++) if (vi->pos.test(r)) fprintf(fp, " c%d", r);
        fputc('\n', fp);
        UINT sz = 0;
        std::vector<DWORD> code;
        auto* sh = (IDirect3DVertexShader9*)kv.first;
        if (sh && SUCCEEDED(sh->GetFunction(nullptr, &sz)) && sz) {
            code.resize(sz / 4 + 1);
            if (FAILED(sh->GetFunction(code.data(), &sz))) code.clear();
        }
        IUnknown* buf = nullptr;
        if (dis && !code.empty() && SUCCEEDED(dis(code.data(), FALSE, nullptr, &buf)) && buf) {
            auto getPtr = (const char*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            fputs(getPtr(buf), fp);
            buf->Release();
        }
    }
    fclose(fp);
}

// A bad self-check: write the command dump of the two frames that were interpolated, the shaders, and the
// prediction again with non-interpolated draws in magenta.
static void DumpFramesTo(const char* path, const Frame& prevF, const Frame& curF);
static void SelfCheckEvidence(const Frame& N, const Frame& P2) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\selfcheck\\%04d_dump.txt", g_dir, g_checkN);
    DumpFramesTo(path, P2, N);
    snprintf(path, sizeof path, "%s\\selfcheck\\%04d_shaders.txt", g_dir, g_checkN);
    DumpShaders(path, N);
    int dbg = C.debug_unlerped;
    C.debug_unlerped = 1;
    Replay(N, &P2, 0.5f);
    C.debug_unlerped = dbg;
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    auto save = dx ? (SaveSurf_t)GetProcAddress(dx, "D3DXSaveSurfaceToFileA") : nullptr;
    if (save && SUCCEEDED(o_StretchRect(g_dev, g_bb, nullptr, g_smA, nullptr, D3DTEXF_LINEAR)) &&
        SUCCEEDED(g_dev->GetRenderTargetData(g_smA, g_sysA))) {
        snprintf(path, sizeof path, "%s\\selfcheck\\%04d_flags.jpg", g_dir, g_checkN);
        save(path, 1, g_sysA, nullptr, nullptr);
    }
}

// ------------------------------------------------------------------------------------------------ replay tracer
// Replay fidelity (debug). On one tick the render target is snapshotted after every draw of the real frame (a
// downscaled full view and a centre crop, into video-memory atlases). In the limiter that frame is then replayed
// exactly as recorded (no interpolation), and once more interpolated halfway, snapshotting the same way.
// - The first draw whose exact replay differs from the game is where the replay stops reproducing the game's state.
// - The interpolated pass shows which draws interpolation changes most (a jump at one draw = that draw's doing).
// trace\NNN\ gets the per-draw numbers, the atlases, the frame dump and the shaders/textures of the draws involved.
static const int TR_MAX = 4608, TR_FW = 48, TR_FH = 24, TR_CW = 24, TR_CH = 48, TR_AW = 4096, TR_AH = 1344;
static IDirect3DSurface9* g_trAtlas[3][2];  // [real, exact replay, interpolated replay][full view, centre crop]
struct TrDraw { void* rt; void* ds; void* vs; void* ps; IDirect3DBaseTexture9* tex[8]; };
static std::vector<TrDraw> g_trDraws;  // real side, per draw: what it rendered with
static std::vector<char> g_trOk[3];
static int g_trIdx, g_trN, g_trFail, g_trSaves;
static bool g_trWant;
static int64_t g_trLast, g_trWantAt;

static RECT TrRect(int k, bool crop) {
    int w = crop ? TR_CW : TR_FW, h = crop ? TR_CH : TR_FH, per = TR_AW / w;
    LONG x = (LONG)((k % per) * w), y = (LONG)((k / per) * h);
    return RECT{x, y, x + w, y + h};
}
static void TraceReleaseRefs() {
    for (auto& t : g_trDraws)
        for (auto*& x : t.tex) if (x) { x->Release(); x = nullptr; }
}
static void TraceReleaseAll() {
    TraceReleaseRefs();
    g_trDraws.clear();
    for (auto& s : g_trAtlas) for (auto*& a : s) if (a) { a->Release(); a = nullptr; }
    g_trState = 0;
    g_trSide = -1;
}
static bool TraceArm() {
    for (auto& s : g_trAtlas)
        for (auto*& a : s)
            if (!EnsureRT(a, TR_AW, TR_AH, D3DFMT_X8R8G8B8)) { Log("trace: atlas allocation failed"); return false; }
    TraceReleaseRefs();
    g_trDraws.assign(TR_MAX, TrDraw{});
    for (auto& o : g_trOk) o.assign(TR_MAX, 0);
    g_trIdx = 0;
    g_trFail = 0;
    g_trState = 2;
    return true;
}
static void TraceAfterDraw(int side) {
    int k = g_trIdx++;
    if (k >= TR_MAX || side < 0 || side > 2 || !g_trAtlas[side][0] || !g_trAtlas[side][1]) return;
    IDirect3DSurface9* rt = nullptr;
    if (FAILED(g_dev->GetRenderTarget(0, &rt)) || !rt) return;
    D3DSURFACE_DESC d;
    rt->GetDesc(&d);
    RECT src{(LONG)(d.Width * 44 / 100), (LONG)(d.Height / 4), (LONG)(d.Width * 56 / 100), (LONG)(d.Height * 95 / 100)};
    RECT a = TrRect(k, false), b = TrRect(k, true);
    bool ok = SUCCEEDED(o_StretchRect(g_dev, rt, nullptr, g_trAtlas[side][0], &a, D3DTEXF_LINEAR)) &&
              SUCCEEDED(o_StretchRect(g_dev, rt, &src, g_trAtlas[side][1], &b, D3DTEXF_LINEAR));
    if (!ok && side == 0 && g_trFail++ < 6)
        Log("trace: snapshot after draw %d failed (rt %ux%u fmt %d msaa %d)", k, d.Width, d.Height, d.Format, d.MultiSampleType);
    g_trOk[side][k] = ok;
    if (side == 0) {
        TrDraw& t = g_trDraws[k];
        t.rt = rt;
        IDirect3DSurface9* ds = nullptr;
        t.ds = nullptr;
        if (SUCCEEDED(g_dev->GetDepthStencilSurface(&ds)) && ds) { t.ds = ds; ds->Release(); }
        t.vs = S.vs;
        t.ps = S.ps;
        for (int s = 0; s < 8; s++) {  // AddRef'd: described in the report, released after it
            t.tex[s] = nullptr;
            if (FAILED(g_dev->GetTexture(s, &t.tex[s]))) t.tex[s] = nullptr;
        }
    }
    rt->Release();
}
static void TraceDescTex(FILE* fp, IDirect3DBaseTexture9* t) {
    if (!t) { fprintf(fp, " -"); return; }
    D3DRESOURCETYPE ty = t->GetType();
    if (ty == D3DRTYPE_TEXTURE) {
        auto* tx = (IDirect3DTexture9*)t;
        D3DSURFACE_DESC d;
        tx->GetLevelDesc(0, &d);
        IDirect3DSurface9* s0 = nullptr;
        tx->GetSurfaceLevel(0, &s0);
        fprintf(fp, " %p[2D %ux%u fmt %x usage %lx pool %d levels %lu surf0 %p]", (void*)t, d.Width, d.Height, (unsigned)d.Format, d.Usage,
                d.Pool, tx->GetLevelCount(), (void*)s0);
        if (s0) s0->Release();
    } else if (ty == D3DRTYPE_CUBETEXTURE) {
        D3DSURFACE_DESC d;
        ((IDirect3DCubeTexture9*)t)->GetLevelDesc(0, &d);
        fprintf(fp, " %p[cube %u fmt %x usage %lx]", (void*)t, d.Width, (unsigned)d.Format, d.Usage);
    } else {
        fprintf(fp, " %p[type %d]", (void*)t, (int)ty);
    }
}
static void TraceDisasm(FILE* fp, IUnknown* sh, bool ps) {
    if (!sh) return;
    static Disasm_t dis = nullptr;
    if (!dis) {
        HMODULE dx = LoadLibraryA("d3dx9_43.dll");
        dis = dx ? (Disasm_t)GetProcAddress(dx, "D3DXDisassembleShader") : nullptr;
    }
    UINT sz = 0;
    std::vector<DWORD> code;
    HRESULT hr = ps ? ((IDirect3DPixelShader9*)sh)->GetFunction(nullptr, &sz) : ((IDirect3DVertexShader9*)sh)->GetFunction(nullptr, &sz);
    if (FAILED(hr) || !sz) return;
    code.resize(sz / 4 + 1);
    hr = ps ? ((IDirect3DPixelShader9*)sh)->GetFunction(code.data(), &sz) : ((IDirect3DVertexShader9*)sh)->GetFunction(code.data(), &sz);
    IUnknown* buf = nullptr;
    if (SUCCEEDED(hr) && dis && SUCCEEDED(dis(code.data(), FALSE, nullptr, &buf)) && buf) {
        auto getPtr = (const char*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
        fprintf(fp, "==== %s %p\n%s\n", ps ? "ps" : "vs", (void*)sh, getPtr(buf));
        buf->Release();
    }
}
static void TraceReport(const Frame& N, const Frame& P, int nReal, int nExact, int nInterp) {
    int n = std::min({nReal, nExact, nInterp, TR_MAX, (int)N.draws.size()});
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\trace", g_dir);
    CreateDirectoryA(dir, nullptr);
    snprintf(dir, sizeof dir, "%s\\trace\\%03d", g_dir, ++g_trN);
    CreateDirectoryA(dir, nullptr);
    bool saveImages = g_trSaves < C.trace_saves;
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    auto save = dx ? (SaveSurf_t)GetProcAddress(dx, "D3DXSaveSurfaceToFileA") : nullptr;
    static const char* sideName[3] = {"real", "blend2", "blend1"};
    struct Row { double eF, eC, iF, iC; int eFm, eCm; bool ok; };
    std::vector<Row> rows(n);
    for (int k = 0; k < n; k++) rows[k] = {0, 0, 0, 0, 0, 0, g_trOk[0][k] && g_trOk[1][k] && g_trOk[2][k]};
    // read back in bands of 192 rows (3 MB per side): this is a 32-bit process, and big readbacks fail right after
    // the rolling recorder has written its frames
    const int BAND = 192;  // a multiple of both tile heights, so no tile straddles two bands
    IDirect3DSurface9 *bandRT[3] = {}, *bandSys[3] = {};
    bool ok = true;
    for (int s = 0; s < 3 && ok; s++)
        ok = SUCCEEDED(g_dev->CreateRenderTarget(TR_AW, BAND, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &bandRT[s], nullptr)) &&
             SUCCEEDED(g_dev->CreateOffscreenPlainSurface(TR_AW, BAND, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &bandSys[s], nullptr));
    for (int a = 0; a < 2 && ok; a++) {
        for (int y0 = 0; y0 < TR_AH && ok; y0 += BAND) {
            int h = std::min(BAND, TR_AH - y0);
            RECT br{0, y0, TR_AW, y0 + h}, dr{0, 0, TR_AW, h};
            D3DLOCKED_RECT lr[3] = {};
            for (int s = 0; s < 3 && ok; s++)
                ok = SUCCEEDED(o_StretchRect(g_dev, g_trAtlas[s][a], &br, bandRT[s], &dr, D3DTEXF_NONE)) &&
                     SUCCEEDED(g_dev->GetRenderTargetData(bandRT[s], bandSys[s])) &&
                     SUCCEEDED(bandSys[s]->LockRect(&lr[s], nullptr, D3DLOCK_READONLY));
            // mean / max absolute difference of one tile between two sides
            auto diff = [&](int s1, int s2, const RECT& r, double& mean, int& mx) {
                long long sum = 0;
                mx = 0;
                for (LONG y = r.top; y < r.bottom; y++) {
                    const uint8_t* p1 = (const uint8_t*)lr[s1].pBits + (size_t)(y - y0) * lr[s1].Pitch + r.left * 4;
                    const uint8_t* p2 = (const uint8_t*)lr[s2].pBits + (size_t)(y - y0) * lr[s2].Pitch + r.left * 4;
                    for (LONG x = r.left; x < r.right; x++, p1 += 4, p2 += 4)
                        for (int c = 0; c < 3; c++) {
                            int e = std::abs(p1[c] - p2[c]);
                            sum += e;
                            mx = std::max(mx, e);
                        }
                }
                mean = sum / (3.0 * (r.right - r.left) * (r.bottom - r.top));
            };
            for (int k = 0; ok && k < n; k++) {
                RECT r = TrRect(k, a == 1);
                if (r.top < y0 || r.bottom > y0 + h || !rows[k].ok) continue;
                Row& w = rows[k];
                int m;
                if (a == 0) { diff(0, 1, r, w.eF, w.eFm); diff(1, 2, r, w.iF, m); }
                else { diff(0, 1, r, w.eC, w.eCm); diff(1, 2, r, w.iC, m); }
            }
            for (int s = 0; s < 3; s++) if (lr[s].pBits) bandSys[s]->UnlockRect();
        }
    }
    for (int s = 0; s < 3; s++) {
        if (bandRT[s]) bandRT[s]->Release();
        if (bandSys[s]) bandSys[s]->Release();
    }
    if (!ok) { Log("trace #%d: readback failed", g_trN); return; }
    if (saveImages && save) {
        int saved = 0;
        for (int a = 0; a < 2; a++)
            for (int s = 0; s < 3; s++) {
                snprintf(path, sizeof path, "%s\\%s_%s.jpg", dir, sideName[s], a ? "crop" : "full");
                if (SUCCEEDED(save(path, 1 /*D3DXIFF_JPG*/, g_trAtlas[s][a], nullptr, nullptr))) saved++;
            }
        if (saved) g_trSaves++;
        else saveImages = false;
    }
    snprintf(path, sizeof path, "%s\\trace.txt", dir);
    FILE* fp = fopen(path, "w");
    if (!fp) return;
    int firstExact = -1, nExactBad = 0;
    for (int k = 0; k < n; k++) {
        const Row& r = rows[k];
        if (!r.ok) continue;
        bool bad = r.eF > 0.5 || r.eC > 0.5 || r.eFm > 24 || r.eCm > 24;
        if (bad) { nExactBad++; if (firstExact < 0) firstExact = k; }
    }
    // interpolation: a draw's own contribution is the change of the difference since the previous draw on its target
    struct Jump { double v; int k; };
    std::vector<Jump> jumps;
    std::unordered_map<void*, int> lastOn;
    for (int k = 0; k < n; k++) {
        if (!rows[k].ok) continue;
        void* rt = g_trDraws[k].rt;
        auto it = lastOn.find(rt);
        double before = it != lastOn.end() ? rows[it->second].iC + rows[it->second].iF : 0;
        double v = rows[k].iC + rows[k].iF - before;
        if (v > 0.3) jumps.push_back({v, k});
        lastOn[rt] = k;
    }
    std::sort(jumps.begin(), jumps.end(), [](const Jump& a, const Jump& b) { return a.v > b.v; });
    fprintf(fp, "trace #%d tick %u: %d draws traced (real %d, exact %d, interpolated %d, frame %zu)\n", g_trN, g_tickNo, n, nReal,
            nExact, nInterp, N.draws.size());
    fprintf(fp, "2-tick blend vs game (motion only): %d draws differ; first at draw %d\n", nExactBad, firstExact);
    fprintf(fp, "blend with previous tick (t=0.5) vs tick before last (t=0.75): biggest per-draw jumps:");
    for (size_t j = 0; j < std::min<size_t>(jumps.size(), 30); j++) fprintf(fp, " %d(%.1f)", jumps[j].k, jumps[j].v);
    fputc('\n', fp);
    fprintf(fp, "columns: draw off op rt ds vs ps | 2-tick blend vs game: full mean/max crop mean/max | 1-tick vs 2-tick blend: full crop | tex0..7\n");
    std::vector<int> detail;  // draws whose shaders get disassembled
    for (int k = firstExact - 2; firstExact >= 0 && k <= firstExact + 2; k++) if (k >= 0 && k < n) detail.push_back(k);
    for (size_t j = 0; j < std::min<size_t>(jumps.size(), 10); j++) detail.push_back(jumps[j].k);
    for (int k = 0; k < n; k++) {
        const Row& r = rows[k];
        const TrDraw& t = g_trDraws[k];
        const Cmd* c = (const Cmd*)(N.buf.data() + N.draws[k].off);
        fprintf(fp, "%4d %08x %-5s rt %p ds %p vs %p ps %p", k, N.draws[k].off, c->op < OP_MAX ? kOp[c->op] : "?", t.rt, t.ds, t.vs, t.ps);
        if (r.ok) fprintf(fp, " | %.2f/%d %.2f/%d | %.2f %.2f |", r.eF, r.eFm, r.eC, r.eCm, r.iF, r.iC);
        else fprintf(fp, " | no snapshot |");
        bool full = std::find(detail.begin(), detail.end(), k) != detail.end();
        for (int s = 0; s < 8; s++) {
            if (full) TraceDescTex(fp, t.tex[s]);
            else fprintf(fp, " %p", (void*)t.tex[s]);
        }
        if (k == firstExact) fprintf(fp, "   <== first exact divergence");
        fputc('\n', fp);
    }
    std::vector<void*> seen;
    for (int k : detail) {
        for (int w = 0; w < 2; w++) {
            void* sh = w ? g_trDraws[k].ps : g_trDraws[k].vs;
            if (!sh || std::find(seen.begin(), seen.end(), sh) != seen.end()) continue;
            seen.push_back(sh);
            fprintf(fp, "---- draw %d\n", k);
            TraceDisasm(fp, (IUnknown*)sh, w == 1);
        }
    }
    fclose(fp);
    if (saveImages) {
        snprintf(path, sizeof path, "%s\\dump.txt", dir);
        DumpFramesTo(path, P, N);
    }
    Log("trace #%d: %d draws; exact replay differs at %d draws (first: draw %d); top 1-tick-blend jumps: %d %d %d", g_trN, n,
        nExactBad, firstExact, jumps.size() > 0 ? jumps[0].k : -1, jumps.size() > 1 ? jumps[1].k : -1, jumps.size() > 2 ? jumps[2].k : -1);
}
// In the limiter of the traced frame, snapshotting each draw. (Earlier rounds established that an exact replay
// matches the game at every draw, and that five exact replays in a row are identical.)
// Now: the same moment rendered two ways. Blended with the tick before last at t=0.75 (what the self-check does,
// which matches the real frames) and with the previous tick at t=0.5 (what the in-between frames do). For steady
// motion both give the same picture; a draw where they part is where blending neighbouring ticks goes wrong.
static void TraceRun(Frame& N, Frame& P) {
    int nReal = std::min(g_trIdx, TR_MAX);
    g_trState = 0;
    Frame& P2 = g_fr[PrevIdx(2)];
    int nExact = 0;
    if (P2.valid && !P2.draws.empty()) {
        BuildPairs(N, P2);
        g_trSide = 1;
        g_trIdx = 0;
        Replay(N, &P2, 0.75f);
        nExact = g_trIdx;
    }
    BuildPairs(N, P);  // also restores the pairs the in-between frames of this tick use
    g_trSide = 2;
    g_trIdx = 0;
    Replay(N, &P, 0.5f);
    int nInterp = g_trIdx;
    g_trSide = -1;
    TraceReport(N, P, nReal, nExact, nInterp);
    TraceReleaseRefs();
}

// A bad self-check, repeated with one kind of interpolation switched off at a time: whichever variant gets closer
// to the real frame (mean error; bias = predicted minus real brightness) shows which mechanism caused the error.
static void SelfCheckAblation(Frame& N, Frame& P2, IDirect3DSurface9* real) {
    struct Var { const char* name; int* flag; int bit; } vars[] = {
        {"no-ps-const", &C.interp_ps, 0},        {"no-smooth-const", &C.lerp_nonpos, 0}, {"no-up-geometry", &C.interp_up, 0},
        {"no-ffp-xform", &C.interp_xform, 0},    {"no-slerp", &C.slerp_bones, 0},        {"no-camera-estimate", nullptr, 1},
        {"no-screen-fixes", nullptr, 2},         {"no-camera-check", nullptr, 4}};
    double m0 = 0, b0 = 0;
    BuildPairs(N, P2);
    Replay(N, &P2, 0.5f);
    CompareWithReal(real, true, &m0, &b0);
    char line[1400];
    int n = snprintf(line, sizeof line, "selfcheck #%d ablation (mean error, bias, blocks brighter/darker): base %.2f %+.2f %d/%d",
                     g_checkN, m0, b0, g_cmpBright, g_cmpDark);
    for (auto& v : vars) {
        int keep = v.flag ? *v.flag : 0;
        if (v.flag) *v.flag = 0;
        else g_ablate |= v.bit;
        double m = 0, b = 0;
        BuildPairs(N, P2);
        Replay(N, &P2, 0.5f);
        CompareWithReal(real, true, &m, &b);
        if (v.flag) *v.flag = keep;
        else g_ablate &= ~v.bit;
        n += snprintf(line + n, sizeof line - n, " | %s %.2f %+.2f %d/%d", v.name, m, b, g_cmpBright, g_cmpDark);
    }
    Log("%s", line);
}

// Pairing capture (paircap_trigger): everything BuildPairs reads - the settings, the shader analysis tables and the
// last three recorded frames - so tools\pairbench.cpp can run the pairing on real game frames offline (timing,
// profiling, and proving that an optimisation gives the same result). paircap\cap_<tick>.bin in the game folder.
static uint64_t g_pcNextTick;
static void PairCapSave(const Frame& P2, const Frame& P, const Frame& N) {
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\paircap", g_dir);
    CreateDirectoryA(dir, nullptr);
    snprintf(path, sizeof path, "%s\\cap_%06llu.bin", dir, (unsigned long long)g_tickNo);
    // analyse every shader of the three frames now: the game does it lazily during this tick's pairing (a shader
    // seen for the first time, or a reused address), which would be after the tables are written
    for (const Frame* f : {&P2, &P, &N}) {
        GetVSInfo(f->startVS);
        GetPSInfo(f->startPS);
        for (uint32_t off = 0; off < f->buf.size();) {
            const Cmd* c = (const Cmd*)(f->buf.data() + off);
            if (c->op == OP_VS) GetVSInfo(((const PPtr*)(c + 1))->p);
            else if (c->op == OP_PS) GetPSInfo(((const PPtr*)(c + 1))->p);
            if (!c->size) break;
            off += c->size;
        }
        for (const DrawRec& d : f->draws) GetVSInfo(d.vs);
    }
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); };
    fwrite("MHPC", 4, 1, fp);
    w32(1);
    w32(sizeof(Config));
    fwrite(&C, sizeof(Config), 1, fp);
    w32((uint32_t)g_vsInfo.size());
    w32(sizeof(VSInfo));
    for (auto& kv : g_vsInfo) { w32((uint32_t)(uintptr_t)kv.first); fwrite(&kv.second, sizeof(VSInfo), 1, fp); }
    w32((uint32_t)g_psInfo.size());
    w32(sizeof(PSEntry));
    for (auto& kv : g_psInfo) { w32((uint32_t)(uintptr_t)kv.first); fwrite(&kv.second, sizeof(PSEntry), 1, fp); }
    w32(sizeof(DrawRec));
    for (const Frame* f : {&P2, &P, &N}) {
        w32(f->serial);
        w32(f->valid);
        w32(f->nvscf);
        w32((uint32_t)(uintptr_t)f->startVS);
        w32((uint32_t)(uintptr_t)f->startPS);
        w32((uint32_t)f->buf.size());
        fwrite(f->buf.data(), 1, f->buf.size(), fp);
        w32((uint32_t)f->draws.size());
        fwrite(f->draws.data(), sizeof(DrawRec), f->draws.size(), fp);
    }
    w32(g_lastPairsCur);
    w32(g_lastPairsPrev);
    w32((uint32_t)g_lastPairs.size());
    for (size_t h = 0; h < g_lastPairs.k.size(); h++)
        if (g_lastPairs.k[h] != NONE) { w32(g_lastPairs.k[h]); w32(g_lastPairs.v[h]); }
    fclose(fp);
    Log("paircap: %s (%zu + %zu + %zu draws)", path, P2.draws.size(), P.draws.size(), N.draws.size());
}

// The game's memory (a 32-bit process, not large-address-aware: 2 GB, shared with d3d9 / DXVK and this mod): committed,
// address space in use, the largest free block. When the largest free block gets small, the mod's optional memory -
// the rolling recorder's tick recordings - is given back, so the game's own allocations keep succeeding.
static void RetroDropTicks(int keep);
static void MemoryCheck() {  // (the render thread, with the statistics: the watchdog thread measures - MemoryScan)
    if (!g_memAt) return;
    Log("  memory: committed %.0f MB, address space in use %.0f of %.0f MB (free %.0f MB, the largest free block %.0f MB) | measured in %.2f ms",
        g_memCommit, g_memUsed, g_memUsed + g_memFree, g_memFree, g_memLargest, g_memMs);
    // (in steps: the recordings are what a save is analysed with, so a few are kept as long as there is room)
    static int stage;
    if (g_memLargest < 160.0 && stage < 1 && C.retro_ticks > 6) {
        stage = 1;
        Log("memory low (largest free block %.0f MB): the recorder keeps the last 6 tick recordings instead of %d", g_memLargest, C.retro_ticks);
        RetroDropTicks(6);
    }
    if (g_memLargest < 120.0 && stage < 2) {
        stage = 2;
        Log("memory low (largest free block %.0f MB): the recorder's tick recordings are given back and no longer kept", g_memLargest);
        RetroDropTicks(0);
    }
}// Present intervals (real and in-between frames): how even the frames are, and how many come closer than the
// monitor refreshes (with V-Sync off those can tear)
static int64_t g_lastPresentAt;
static void NotePresent() {
    int64_t now = Qpc();
    if (g_lastPresentAt) {
        double dt = Ms(now - g_lastPresentAt);
        if (dt < 100) {
            St.piN++;
            St.piSum += dt;
            St.piSum2 += dt * dt;
            St.piMax = std::max(St.piMax, dt);
            if (dt < 1000.0 / std::max(g_refresh, 30.0) - 0.05) St.piFast++;
        }
    }
    g_lastPresentAt = now;
}
static void Interp(DWORD ms, int64_t sleepAt) {
    PhaseScope ps(4);
    int64_t entry = sleepAt;  // (what ran since the game called Sleep - a flush, a wait - comes out of its sleep)
    DWORD tg = (DWORD)(sleepAt / (g_qpf.QuadPart / 1000));  // (MsClock at the Sleep call: the game frame executes in it first)
    int64_t deadline = entry + (int64_t)ms * g_qpf.QuadPart / 1000;
    Frame& N = g_fr[g_cur];
    Frame& P = g_fr[PrevIdx(1)];
    St.sleeps++;
    double logic = g_frameStart ? Ms(entry - g_frameStart) : 0.0;  // (no previous Present on the first tick)
    if (logic < 1000) {
        St.logicMs += logic;
        g_ovLogicMs = g_ovLogicMs * 0.9 + logic * 0.1;
    }
    if (N.draws.empty() || !P.valid || P.draws.empty() || !g_lastReal || g_lost) { WaitDeadline(deadline, tg + ms); return; }

    int64_t A = g_lastReal;
    double period = (double)(deadline - A);
    double interval = g_qpf.QuadPart / TargetFps();
    if (period < interval * 1.5) { WaitDeadline(deadline, tg + ms); return; }
    g_inInterp = true;
    bool lerp = C.mode == 2;
    if (C.regroup >= 1) {
        int64_t a0 = Qpc();
        // the game frame's analysis, when it was only cut where the recording has a barrier anyway (EndScene)
        // (its windows end wherever the game frame had to be executed early too; still valid, and already analysed)
        N.rgLive = C.regroup >= 2 && N.rg.off == N.buf.size() && !N.rg.open;
        if (N.rgLive) St.rgReuse++;
        else {
            RgFinish(N, N.rgR);
            ShareLists(N);
        }
        St.rgAnaMs += Ms(Qpc() - a0);
    }
    if (!SaveEnd()) { ReleaseEnd(); g_inInterp = false; WaitDeadline(deadline, tg + ms); return; }
    // self-check (debug, occasional): predict the previous real frame from its two neighbours
    Frame& P2 = g_fr[PrevIdx(2)];
    // only meaningful when the three frames are evenly spaced (no late or skipped ticks in between)
    double d1 = Ms(P.presentAt - P2.presentAt), d2 = Ms(deadline - P.presentAt);
    bool even = d1 > 25 && d1 < 45 && d2 > 25 && d2 < 45 && std::fabs(d1 - d2) < 4;
    if (lerp) {  // shadow map reuse
        if (C.shadow_reuse) PairJoin();  // (it reads the shader tables the matching may add to)
        int64_t s0 = Qpc();
        if (g_snapSerial == N.serial) SnapCopyPending();
        int why = PrepareShadowReuse(N);
        St.srMs += Ms(Qpc() - s0);
        if (why == 0) St.srTicks++;
        else St.srOff[why == 8 ? 0 : why & 15]++;
        if (why) { g_reuseOn = false; g_rN = g_subN = 0; }
    }
    bool checked = false;
    if (lerp && C.selfcheck && even && P.hasImg && P.img && P2.valid && !P2.draws.empty() &&
        Ms(entry - g_lastCheck) >= C.selfcheck_interval_ms) {
        g_lastCheck = entry;
        checked = true;
        PairJoin();
        BuildPairs(N, P2);
        if (!g_cut) {
            Replay(N, &P2, 0.5f);
            if (CompareWithReal(P.img)) {
                SelfCheckEvidence(N, P2);
                SelfCheckAblation(N, P2, P.img);
            } else if (C.selfcheck_ablate) {
                SelfCheckAblation(N, P2, P.img);
            }
        }
    }
    if (lerp) {
        if (g_pcLeft > 0 && g_tickNo >= g_pcNextTick && P2.valid && !P2.draws.empty()) {
            PairJoin();
            PairCapSave(P2, P, N);
            g_pcLeft--;
            g_pcNextTick = g_tickNo + 30;
        }
        g_boneTick++;  // (bone slerp cache: one tick)
        // the matching's expected time (started in the background at Sleep: what is left of it)
        double pairLeft = g_ovPairMs;
        if (g_pjBusy) pairLeft = std::max(0.0, g_ovPairMs - Ms(Qpc() - g_pjStartAt));
        else if (!checked && PairJobFor(N, P)) pairLeft = 0;
        if (Ms(deadline - Qpc()) < pairLeft * 1.2 + Ms((int64_t)g_replayCost) + 1.0 && g_ovPairMs > 0) {
            St.noTime++;  // (the game's own frame took too long this tick: show it as it is, on time)
            RestoreEnd();
            g_inInterp = false;
            WaitDeadline(deadline, tg + ms);
            return;
        }
        int64_t p0 = Qpc();
        PairJoin();
        PhaseScope pps(3);
        double pm;
        if (!checked && PairJobFor(N, P)) {
            St.pjUsed++;
            St.pjMs += g_pjLastMs;
            pm = g_pjLastMs;
        } else {
            if (g_pjOk && g_pjReal == &N) St.pjStale++;
            BuildPairs(N, P);
            pm = Ms(Qpc() - p0);
        }
        g_pjOk = false;
        St.pairMs += Ms(Qpc() - p0);
        g_ovPairMs = g_ovPairMs * 0.9 + pm * 0.1;
        g_selfcheckDue = C.selfcheck && Ms(Qpc() - g_lastCheck) + 40 >= C.selfcheck_interval_ms;
        if (g_cut) {  // camera cut: show it as is
            RestoreEnd();
            g_inInterp = false;
            WaitDeadline(deadline, tg + ms);
            return;
        }
        if (g_trState == 2) TraceRun(N, P);
        SaveHistory(N, P);
        RetroKeepTick(N);
        static int64_t lastCarried;
        if (Ms(Qpc() - lastCarried) > 2000.0) {  // (every 2 s: about 1 ms)
            lastCarried = Qpc();
            FindCarriedTargets(N, P);
        }
        if (C.debug_replayhash) {  // fixed points instead of the timed in-between frames (the same work every run)
            uint64_t h[3] = {};
            const float ts[3] = {0.37f, 0.37f, 0.71f};
            for (int k = 0; k < 3; k++) {
                Replay(N, &P, ts[k]);
                if (RcGrab(k, nullptr)) h[k] = RcHash(k);
            }
            // and the game frame's recording replayed as it is (t = 1, recorded order) against the frame as shown
            g_rgOff = true;
            Replay(N, nullptr, 1.0f);
            g_rgOff = false;
            long live = g_saved && RcGrab(0, g_saved) && RcGrab(1, nullptr) ? RcDiff(0, 1) : -1;
            Log("replayhash %llu %016llx %016llx %016llx live %ld", (unsigned long long)g_tickNo, (unsigned long long)h[0],
                (unsigned long long)h[1], (unsigned long long)h[2], live);
            RestoreEnd();
            g_inInterp = false;
            WaitDeadline(deadline, tg + ms);
            return;
        }
    }
    int64_t margin = (int64_t)C.margin_us * g_qpf.QuadPart / 1000000;
    int maxExtra = C.max_extra;
    if (entry < g_backoffUntil) {  // the game ran late recently: leave it more room
        maxExtra = std::max(1, maxExtra / 2);
        margin += g_qpf.QuadPart / 500;
        St.backoffTicks++;
    }
    if (!g_evq) g_dev->CreateQuery(D3DQUERYTYPE_EVENT, &g_evq);
    for (auto& q : g_frameQ) if (!q) g_dev->CreateQuery(D3DQUERYTYPE_EVENT, &q);
    g_frameQIssued[0] = g_frameQIssued[1] = false;
    int extra = 0;
    bool costLimited = false;
    // Schedule backwards from the real frame: the last in-between frame one interval (or one frame budget) before
    // the deadline, the others at even spacing before it, as many as fit after the game's own work for this tick.
    // (A grid from the previous real frame put its first slots inside the game's own frame time, where they were
    // dropped, and left an uneven gap before the real frame.)
    // budget: CPU replay + Present, plus the GPU tail we drain before giving control back to the game
    double budget = (g_replayCost + g_gpuTail) * 1.2 + (double)margin;
    int64_t last = deadline - (int64_t)std::max(interval, budget);
    int64_t start = Qpc();
    // (a slot up to half an interval gone still counts: shown at once, blended for when it is shown; dropping it cost a
    // whole frame whenever the matching finished just after it - in a crowd, most ticks)
    int nSlots = last >= start ? 1 + (int)((double)(last - start) / interval + (C.late_slot ? 0.5 : 0.0)) : 0;
    if (last < start) costLimited = true;
    nSlots = std::min(nSlots, maxExtra);
    // pacing: plan when each frame is shown (a replay is on screen about g_replayCost after it starts) - evenly from the
    // first moment one can be (now + its replay) to the real frame at the deadline, never closer than the interval
    // (or the frame budget), the real frame included; each frame's content is for when it is shown
    int64_t begins[16];
    int nb = 0;
    double gap = interval;
    int64_t rc = (int64_t)g_replayCost;
    if (C.pacing == 1) {
        int64_t firstShow = start + rc;
        double avail = (double)(deadline - firstShow), sMin = std::max(interval, budget);
        int n = avail >= sMin ? (int)(avail / sMin) : 0;
        n = std::min(std::min(n, maxExtra), 16);
        if (n <= 0) costLimited = true;
        gap = n > 0 ? avail / n : interval;
        for (int k = 0; k < n; k++) begins[nb++] = firstShow + (int64_t)(gap * k) - rc;
    } else {
        for (int k = std::min(nSlots, 16) - 1; k >= 0; k--) begins[nb++] = last - (int64_t)(interval * k);
    }
    // latency_trim: how long the first frame waits for its slot this tick - what the game could have started later
    // without losing it (as if it had not been held: plus this tick's hold)
    if (nb > 0) LatencyNote(Ms(begins[0] - start) + g_ltHeld);
    if (!g_carryTex.empty()) CarrySave();
    for (int k = 0; k < nb && extra < maxExtra; k++) {
        int64_t slot = begins[k];
        int64_t now = Qpc();
        if (now > slot + (int64_t)(gap * 0.5)) { St.missed++; continue; }
        if (std::max(now, slot) + (int64_t)budget > deadline) { costLimited = true; break; }
        WaitQ(slot);
        if (C.gpu_queue && g_frameQ[0] && g_frameQ[1]) {
            // at most one of our frames still on the GPU while this one is built
            if (extra >= 2 && g_frameQIssued[extra % 2] && !WaitQueryUntil(g_frameQ[extra % 2], deadline - (int64_t)budget)) {
                St.gpuWaitCut++;  // (the GPU is behind: no more frames this tick rather than a late one)
                costLimited = true;
                break;
            }
        } else if (extra > 0) {
            if (!WaitGpuUntil(deadline - (int64_t)budget)) { St.gpuWaitCut++; costLimited = true; break; }  // keep at most one of our frames queued
        }
        int64_t r0 = Qpc();
        float t = C.pacing ? (float)std::min(0.999, (double)(r0 + rc - A) / period)  // (on screen about rc from now)
                           : (float)((double)((C.late_slot ? std::max(slot, r0) : slot) - A) / period);
        if (extra == 0 && C.regroup_check > 0 && C.regroup >= 1 && Ms(r0 - g_rcLast) >= C.regroup_check * 1000.0) {
            g_rcLast = r0;
            RegroupCheck(N, lerp ? &P : nullptr, t);
        }
        if (extra == 0) GpuTimerBegin(g_gtInterp);
        if (!g_carryTex.empty()) CarryInput();
        Replay(N, lerp ? &P : nullptr, t);
        if (extra == 0) GpuTimerEnd(g_gtInterp);
        Marker(D3DCOLOR_XRGB(0, 255, 0));
        bool capturing = g_capLeft > 0;
        Capture("i", t);
        DrawOverlay();
        int64_t r1 = Qpc();
        OvRecord(false);
        NotePresent();
        g_heartbeat = Qpc();
        HRESULT hr = o_Present(g_dev, g_hasSrc ? &g_src : nullptr, g_hasDst ? &g_dst : nullptr, g_presentWnd, nullptr);
        if (C.debug_lost_at && St.replayN + 1 == (uint64_t)C.debug_lost_at) hr = D3DERR_DEVICELOST;  // (testing)
        InputSample();
        if (C.gpu_queue && g_frameQ[extra % 2]) { g_frameQ[extra % 2]->Issue(D3DISSUE_END); g_frameQIssued[extra % 2] = true; }
        int64_t r2 = Qpc();
        double cost = (double)(r2 - r0);
        if (!capturing) g_replayCost = g_replayCost == 0 ? cost : g_replayCost * 0.8 + cost * 0.2;
        St.replayMs += Ms(r1 - r0);
        St.presentMs += Ms(r2 - r1);
        St.replayN++;
        extra++;
        if (FAILED(hr)) {
            if (hr == D3DERR_DEVICELOST) NoteLost("an in-between frame's");
            else if (St.replayN < 20 || (St.replayN % 1000) == 0) Log("in-between Present failed: %08lx", (unsigned long)hr);
            break;
        }
    }
    // a cost estimate that blocks every replay would never be refreshed; let it recover
    if (extra == 0 && costLimited) { g_replayCost *= 0.85; g_gpuTail *= 0.85; }
    St.interpFrames += extra;
    g_ovExtra = g_ovExtra * 0.9 + extra * 0.1;
    if (extra > 0 && !g_carryTex.empty()) CarryRestore();
    if (extra > 0) {  // drain: the game's own frame must not queue behind ours
        // (unbounded, as before n20: cut at the deadline, DXVK's Present of the game frame that followed sometimes never
        // returned - 3 of 100 test starts hung at the 4th Present, while the first pipelines compile. The loop above
        // already stops starting frames the GPU can't finish in time.)
        int64_t g0 = Qpc();
        WaitGpu();
        double tail = (double)(Qpc() - g0);
        g_gpuTail = g_gpuTail == 0 ? tail : g_gpuTail * 0.8 + tail * 0.2;
        St.gpuTailMs += Ms((int64_t)tail);
        St.gpuTailN++;
    }
    RestoreEnd();
    g_inInterp = false;
    {
        int64_t over = Qpc() - deadline;
        if (over > (int64_t)(g_qpf.QuadPart / 1000)) St.lateExit++, St.lateMs += Ms(over);
    }
    InputSample();
    WaitDeadline(deadline, tg + ms);
}

// Sleep(ms) as called at since: what the hook ran first (the deferred game frame) comes out of it - the limiter's
// next deadline counts from when Sleep returns
static void SleepRest(DWORD ms, int64_t since) {
    double spent = Ms(Qpc() - since);
    if (ms == 0 || ms == INFINITE || spent < 0.5) { o_Sleep(ms); return; }
    DWORD s = (DWORD)(spent + 0.5);
    o_Sleep(ms > s ? ms - s : 0);
}
static void WINAPI hk_Sleep(DWORD ms) {
    int64_t sleepAt = Qpc();
    if (GetCurrentThreadId() == g_tid) g_heartbeat = sleepAt;
    PairJoin();
    // the limiter's: the matching starts on the worker threads, and the game frame executes beside it
    if (ms > 0 && ms <= 100 && g_dev && !g_inInterp && !g_sleptThisFrame && GetCurrentThreadId() == g_tid && C.enabled && C.mode > 0 &&
        !g_fr[g_cur].draws.empty())
        PairJobStart();
    FlushDeferred(kWhySleep);
    if (g_dev && !g_inInterp && GetCurrentThreadId() == g_tid && CheckHooks("Sleep")) {
        // the hooks were gone for part of this frame: the recording is incomplete, don't replay it
        PairJoin();
        for (auto& fr : g_fr) fr.Clear();
        SleepRest(ms, sleepAt);
        return;
    }
    if (ms > 0 && ms <= 100 && g_dev && !g_inInterp && !g_sleptThisFrame && GetCurrentThreadId() == g_tid &&
        C.enabled && C.mode > 0 && !g_fr[g_cur].draws.empty()) {
        g_sleptThisFrame = true;
        g_defer = 0;  // (the rest of this frame, if any, runs as called)
        GpuTimerEnd(g_gtReal);  // the game's own frame is complete here
        // minimized: nobody sees extra frames, so keep vanilla timing and leave the GPU alone
        HWND w = g_presentWnd ? g_presentWnd : g_hwnd;
        if (w && IsIconic(w)) {
            St.minimizedTicks++;
            SleepRest(ms, sleepAt);
        } else {
            Interp(ms, sleepAt);
        }
        g_afterSleep = true;
        return;
    }
    SleepRest(ms, sleepAt);
}

static bool HookSleep() {
    if (o_Sleep) return true;
    HMODULE kb = GetModuleHandleW(L"KernelBase.dll");
    uint8_t* f = kb ? (uint8_t*)GetProcAddress(kb, "Sleep") : nullptr;
    if (!f) return false;
    bool pad = true;
    for (int i = 1; i <= 5; i++) pad &= (f[-i] == 0xCC || f[-i] == 0x90);
    if (f[0] == 0xEB && f[1] == 0xF9 && f[-5] == 0xE9) { Log("Sleep already hot-patched by someone else"); return false; }
    if (!(f[0] == 0x8B && f[1] == 0xFF && pad)) {
        Log("KernelBase!Sleep prologue not hot-patchable: %02x %02x %02x %02x", f[0], f[1], f[2], f[3]);
        return false;
    }
    DWORD old;
    VirtualProtect(f - 5, 7, PAGE_EXECUTE_READWRITE, &old);
    f[-5] = 0xE9;
    *(int32_t*)(f - 4) = (int32_t)((uint8_t*)&hk_Sleep - f);
    o_Sleep = (Sleep_t)(f + 2);
    __atomic_store_n((uint16_t*)f, (uint16_t)0xF9EB, __ATOMIC_SEQ_CST);
    VirtualProtect(f - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f - 5, 7);
    return true;
}

// ------------------------------------------------------------------------------------------------ per-frame bookkeeping
static void BeginFrame() {
    PairJoin();
    Frame& f = g_fr[g_cur];
    f.Clear();
    g_defer = 0;
    g_flushFrom = 0;
    g_flushWin = 0;
    g_flushCuts = 0;
    g_liveRT0 = f.rt[0];
    g_liveDS = f.ds;
    g_pendDraws = 0;
    g_pendTex.clear();
    g_pendTexAll = false;
    if (!C.enabled || C.mode == 0 || g_lost || !o_SBCapture || !o_SBApply) return;
    if (!f.sb && FAILED(o_CreateStateBlock(g_dev, D3DSBT_ALL, &f.sb))) return;
    o_SBCapture(f.sb);
    for (int i = 0; i < 4; i++) if (FAILED(g_dev->GetRenderTarget(i, &f.rt[i]))) f.rt[i] = nullptr;
    if (FAILED(g_dev->GetDepthStencilSurface(&f.ds))) f.ds = nullptr;
    f.startVS = S.vs;
    f.startPS = S.ps;
    f.valid = true;
    static uint32_t serial = 0;
    f.serial = ++serial;
    if (f.buf.capacity() < (8u << 20)) f.buf.reserve(8u << 20);
    // regrouping: what is bound when the frame starts
    Regroup& r = f.rg;
    for (int i = 0; i < 4; i++) r.cur.s[i] = f.rt[i];
    r.cur.s[4] = f.ds;
    r.lastBind = r.cur;
    r.ps = S.ps;
    for (int s = 0; s < 21; s++) {
        IDirect3DBaseTexture9* tx = nullptr;
        if (SUCCEEDED(g_dev->GetTexture(s < 16 ? (DWORD)s : 256u + (DWORD)(s - 16), &tx)) && tx) { r.tex[s] = tx; tx->Release(); }
    }
    for (void* tx : r.tex)
        if (tx) g_pendTex.push_back(tx);
    memcpy(f.startTex, r.tex, sizeof f.startTex);
    g_liveRT0 = f.rt[0];
    g_liveDS = f.ds;
    bool live = f.rgR.live;
    f.rgR = r;
    f.rgR.live = live;
    St.rgFrames++;
    g_pendGen++;
    g_defer = C.regroup >= 2;
}

static void DumpFramesTo(const char* path, const Frame& prevF, const Frame& curF);
static void DumpFrames() {
    char path[MAX_PATH];
    if (g_dumpTo[0]) snprintf(path, sizeof path, "%s", g_dumpTo);
    else snprintf(path, sizeof path, "%s\\mhfz-fps-dump.txt", g_dir);
    g_dumpTo[0] = 0;
    DumpFramesTo(path, g_fr[PrevIdx(1)], g_fr[g_cur]);
}
static void DumpFramesTo(const char* path, const Frame& prevF, const Frame& curF) {
    FILE* fp = fopen(path, "w");
    if (!fp) return;
    for (int w = 0; w < 2; w++) {
        const Frame& f = w == 0 ? prevF : curF;
        fprintf(fp, "==== frame %s: %zu bytes, %zu draws\n", w == 0 ? "PREV" : "CUR", f.buf.size(), f.draws.size());
        size_t di = 0, pi = 0;
        for (size_t off = 0; off < f.buf.size();) {
            const Cmd* c = (const Cmd*)(f.buf.data() + off);
            const uint8_t* p = (const uint8_t*)(c + 1);
            bool paired = false;
            if (w == 1) {
                while (pi < g_pairs.size() && g_pairs[pi].cur < off) pi++;
                paired = pi < g_pairs.size() && g_pairs[pi].cur == off;
            }
            fprintf(fp, "%08zx %c %-9s", off, paired ? '*' : ' ', c->op < OP_MAX ? kOp[c->op] : "?");
            if (w == 1 && c->op == OP_VSCF && ((const PConst*)p)->ord < g_pmask.size()) {
                auto* q = (const PConst*)p;
                int np = 0;
                for (UINT r = 0; r < q->count; r++) np += g_pmask[q->ord].test(q->start + r);
                fprintf(fp, " pos%u/%u", np, q->count);
            }
            switch (c->op) {
            case OP_RS: case OP_FVF: case OP_LENABLE: case OP_SSFREQ: fprintf(fp, " %u %u", ((const P2*)p)->a, ((const P2*)p)->b); break;
            case OP_SS: case OP_TSS: fprintf(fp, " %u %u %u", ((const P3*)p)->a, ((const P3*)p)->b, ((const P3*)p)->c); break;
            case OP_TEX: case OP_RT: case OP_VS: case OP_PS: case OP_DECL: case OP_IDX: case OP_DS: case OP_SBAPPLY: case OP_SBCAPTURE:
                fprintf(fp, " %u %p", ((const PPtr*)p)->a, (void*)((const PPtr*)p)->p); break;
            case OP_VSCF: case OP_PSCF: {
                auto* q = (const PConst*)p;
                const float* v = (const float*)(q + 1);
                fprintf(fp, " c%u x%u :", q->start, q->count);
                for (UINT i = 0; i < std::min(q->count * 4, 8u); i++) fprintf(fp, " %.4g", v[i]);
                break;
            }
            case OP_SSRC: { auto* x = (const PSsrc*)p; fprintf(fp, " s%u %p off %u stride %u", x->stream, (void*)x->vb, x->off, x->stride); break; }
            case OP_DIP: { auto* x = (const PDIP*)p; fprintf(fp, " t%d base %d min %u nv %u si %u pc %u", x->type, x->base, x->minIdx, x->numV, x->startIdx, x->primCount); break; }
            case OP_DP: { auto* x = (const PDP*)p; fprintf(fp, " t%d s %u n %u", x->type, x->start, x->count); break; }
            case OP_DPUP: { auto* x = (const PDPUP*)p; fprintf(fp, " t%d pc %u stride %u pos %d", x->type, x->primCount, x->stride, x->posOff); break; }
            case OP_DIPUP: { auto* x = (const PDIPUP*)p; fprintf(fp, " t%d pc %u nv %u stride %u pos %d", x->type, x->primCount, x->numV, x->stride, x->posOff); break; }
            case OP_CLEAR: { auto* x = (const PClear*)p; fprintf(fp, " n%u fl %x col %08x", x->count, x->flags, x->color); break; }
            case OP_VP: { auto* x = (const D3DVIEWPORT9*)p; fprintf(fp, " %u,%u %ux%u", x->X, x->Y, x->Width, x->Height); break; }
            case OP_XFORM: fprintf(fp, " state %u", ((const PXform*)p)->state); break;
            }
            if (di < f.draws.size() && f.draws[di].off == off) {
                bool flag = w == 1 && di < g_drawFlag.size() && g_drawFlag[di];
                fprintf(fp, "   [draw %zu key %016llx vs %p%s", di, (unsigned long long)f.draws[di].key, f.draws[di].vs,
                        flag ? " NOT-LERPED" : "");
                if (w == 1 && di < g_match.size()) {
                    if (g_match[di] >= 0) fprintf(fp, " <- prev %d", g_match[di]);
                    else fprintf(fp, " UNMATCHED");
                }
                fputc(']', fp);
                di++;
            }
            fputc('\n', fp);
            off += c->size;
        }
    }
    fprintf(fp, "==== vertex shaders (%zu)\n", g_vsInfo.size());
    for (auto& kv : g_vsInfo) {
        fprintf(fp, "%p vs_%d all=%d pos:", kv.first, kv.second.major, kv.second.all);
        for (int r = 0; r < 256; r++) {
            if (!kv.second.pos.test(r)) continue;
            int e = r;
            while (e + 1 < 256 && kv.second.pos.test(e + 1)) e++;
            fprintf(fp, e > r ? " c%d-%d" : " c%d", r, e);
            r = e;
        }
        fputc('\n', fp);
    }
    fprintf(fp, "==== pairs (%zu)\n", g_pairs.size());
    for (auto& p : g_pairs) fprintf(fp, "%08x <- %08x\n", p.cur, p.prev);
    fclose(fp);
    Log("dumped frames to %s", path);
}

static bool KeyEdge(int code) {  // (code: vk | 0x100 ctrl | 0x200 shift | 0x400 alt)
    static bool down[256];
    int vk = code & 0xFF;
    if (!vk) return false;
    // a combination (Ctrl+Shift+5) asks for its key only while its modifiers are held, and only "is it down": the
    // "pressed since the last call" bit is shared with every caller, the game included - asking for the digits on every
    // frame could take a quick tap of 1-5 away from it
    if (code & 0x700) {
        auto held = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
        if (((code & 0x100) && !held(VK_CONTROL)) || ((code & 0x200) && !held(VK_SHIFT)) || ((code & 0x400) && !held(VK_MENU))) {
            down[vk] = false;
            return false;
        }
        bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool r = d && !down[vk];
        down[vk] = d;
        return r;
    }
    SHORT s = GetAsyncKeyState(vk);
    bool d = (s & 0x8000) != 0;
    bool r = (d || (s & 1)) && !down[vk];  // bit 0: pressed since the last poll (a tap shorter than one tick)
    down[vk] = d;
    if (!r) return false;
    auto held = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
    if ((code & 0x100) && !held(VK_CONTROL)) return false;
    if ((code & 0x200) && !held(VK_SHIFT)) return false;
    if ((code & 0x400) && !held(VK_MENU)) return false;
    return true;
}
// Single keys (usable with one hand while holding a controller) plus the Ctrl+Shift+F-key originals.
// Capture is delayed so the player can get back on the stick and be moving when it records.
static int64_t g_capAt;
static void LiveConfig() {
    int64_t now = Qpc();
    if (now - g_iniChecked < g_qpf.QuadPart / 2) return;  // twice a second
    g_iniChecked = now;
    if (!IniChanged()) return;
    int capT = C.capture_trigger, dumpT = C.dump_trigger, retroT = C.retro_trigger, traceT = C.trace_trigger, pcT = C.paircap_trigger;
    int cpT = C.camprobe_trigger;
    LoadConfig();
    ApplyVariant();
    if (C.input_latch) InputHooks();  // (switched on while playing: DirectInput's device class is hooked now)
    if (C.trace_trigger != traceT) { g_trWant = true; Log("remote: trace"); }
    if (C.paircap_trigger != pcT) { g_pcLeft = std::max(1, C.paircap_count); g_pcNextTick = 0; Log("remote: %d pairing captures", g_pcLeft); }
    Log("config reloaded: enabled %d mode %d target %d vs %d xform %d up %d ps %d nonpos %d debug %d selfcheck %d (%d ms)",
        C.enabled, C.mode, C.target_fps, C.interp_vs, C.interp_xform, C.interp_up, C.interp_ps, C.lerp_nonpos,
        C.debug_unlerped, C.selfcheck, C.selfcheck_interval_ms);
    if (C.capture_trigger != capT) { g_capLeft = 24; g_dumpNext = true; Log("remote: capture + dump"); }
    else if (C.dump_trigger != dumpT) { g_dumpNext = true; Log("remote: dump"); }
    if (C.retro_trigger != retroT) { Log("remote: saving the last frames"); RetroSave(); }
    if (C.camprobe_trigger != cpT) { Log("remote: camera probe"); CamProbeStart(); }
}

static double QueryRefresh(HWND w);
static int64_t g_refreshChecked;
static void TrackMonitor() {  // the target rate follows the monitor the game window is on
    int64_t now = Qpc();
    if (now - g_refreshChecked < g_qpf.QuadPart * 2) return;
    g_refreshChecked = now;
    double r = QueryRefresh(g_presentWnd ? g_presentWnd : g_hwnd);
    if (r != g_refresh) {
        Log("monitor refresh changed: %.0f -> %.0f Hz", g_refresh, r);
        g_refresh = r;
    }
}

static void Hotkeys() {
    LiveConfig();
    TrackMonitor();
    if (!C.hotkeys) return;
    // (Ctrl+Shift+F7..F12, developer keys only: the F keys alone are the game's)
    const int cs = 0x300;
    bool f7 = C.debug_keys && KeyEdge(VK_F7 | cs), f8 = C.debug_keys && KeyEdge(VK_F8 | cs), f9 = C.debug_keys && KeyEdge(VK_F9 | cs),
         f10 = C.debug_keys && KeyEdge(VK_F10 | cs), f11 = C.debug_keys && KeyEdge(VK_F11 | cs), f12 = C.debug_keys && KeyEdge(VK_F12 | cs);
    bool kToggle = KeyEdge(C.key_toggle), kDebug = KeyEdge(C.key_debug), kMode = KeyEdge(C.key_mode), kCap = KeyEdge(C.key_capture);
    bool kOverlay = C.key_overlay && KeyEdge(C.key_overlay);
    bool kShadow = C.key_shadow && KeyEdge(C.key_shadow);
    bool kNonpos = C.key_nonpos && KeyEdge(C.key_nonpos);
    bool kCamPaths = C.key_campaths && KeyEdge(C.key_campaths);
    bool kField = C.key_field && KeyEdge(C.key_field);
    bool kLatency = C.key_latency && KeyEdge(C.key_latency);
    bool kCarry = C.key_carry && KeyEdge(C.key_carry);
    bool kCamProbe = C.debug_keys && C.key_camprobe && KeyEdge(C.key_camprobe);
    if (!C.debug_keys) f7 = f8 = f9 = f10 = f11 = f12 = kDebug = kMode = false;
    if (!C.debug_keys && !C.retro) kCap = false;  // (with the clip recorder on, Delete saves a clip for everyone)
    if (!C.debug_keys) kCamPaths = kCarry = kNonpos = kField = kShadow = false;  // (the A/B switches: developer keys)
    {  // keys only with the game in front: typing elsewhere (Insert, Delete, Pause in another window) must not reach it
        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid != GetCurrentProcessId()) kToggle = kOverlay = kLatency = kShadow = kNonpos = kCamPaths = kField = kCarry = kDebug = kMode = kCap = false;
    }
    if (g_capAt && Qpc() >= g_capAt) {
        g_capAt = 0;
        g_dumpNext = true;
        g_capLeft = 24;
        Log("capture: starting (24 frames + dump)");
    }
    // the clip key only with the game in front (typing elsewhere - Delete in a file manager - must not save clips)
    static bool autosaved;
    if (kCap) {
        DWORD fg = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fg);
        if (fg != GetCurrentProcessId()) kCap = false;
    }
    if (C.retro && C.retro_autosave > 0 && !autosaved && Ms(Qpc() - g_t0) >= C.retro_autosave * 1000.0) autosaved = true, kCap = true;  // (tests)
    if (kCap) {
        if (C.retro) {
            Log("hotkey: saving the last frames");
            int clip = RetroSave();
            if (clip) Toast("Clip %d saved - thanks!", clip);
            else Toast("Clip could not be saved");
        }
        else { g_capAt = Qpc() + (int64_t)C.capture_delay_ms * g_qpf.QuadPart / 1000; Log("hotkey: capture in %d ms", C.capture_delay_ms); }
        if (C.trace_interval_s > 0) g_trWantAt = Qpc() + g_qpf.QuadPart * 3;  // and trace a frame once that's written
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return;
    bool mod = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    if (!mod) f7 = f8 = f9 = f10 = f11 = f12 = false;
    if (f7) {
        static const char* names[7] = {"normal", "smooth constants switch at the tick (red)", "pixel-shader constants not blended (green)",
                                       "CPU-built quads not interpolated (blue)", "bones blended linearly (yellow)",
                                       "passes other than the main scene not interpolated (purple)", "main scene pass not interpolated (cyan)"};
        g_variant = (g_variant + 1) % 7;
        LoadConfig();
        ApplyVariant();
        Log("hotkey: diagnostic variant %d: %s", g_variant, names[g_variant]);
    }
    if (f9 || kToggle) { C.enabled = !C.enabled; Log("hotkey: enabled=%d", C.enabled); }
    if (kOverlay) {  // off -> fps counter -> detailed -> off
        C.overlay = (C.overlay + 1) % 3;
        Toast("FPS counter: %s", C.overlay == 1 ? "on" : C.overlay == 2 ? "detailed" : "off");
    }
    if (kShadow) { C.shadow_reuse = !C.shadow_reuse; Log("hotkey: shadow_reuse=%d", C.shadow_reuse); }
    if (kNonpos) { C.lerp_nonpos = C.lerp_nonpos == 2 ? 1 : 2; Toast("fog/light blending rule %d (%s)", C.lerp_nonpos, C.lerp_nonpos == 2 ? "new" : "old"); }
    if (kCamPaths) { C.cam_paths = !C.cam_paths; Toast("camera paths %s", C.cam_paths ? "ON" : "OFF"); }
    if (kField) { C.field_match = !C.field_match; Toast("repeated objects by the motion around them %s", C.field_match ? "ON" : "OFF"); }
    if (kCamProbe) CamProbeStart();
    if (g_cpMsgReady && InterlockedExchange(&g_cpMsgReady, 0)) Toast("%s", g_cpMsg);
    if (kLatency) { C.latency_trim = !C.latency_trim; Toast("lower input latency %s", C.latency_trim ? "ON" : "OFF"); }
    if (kCarry) {
        C.carry_restore = (C.carry_restore + 1) % 3;
        Toast("light fix %d: %s", C.carry_restore,
              C.carry_restore == 0 ? "OFF" : C.carry_restore == 1 ? "restore after (old)" : "same input for every frame (new)");
    }
    if (f8 || kDebug) { C.debug_unlerped = !C.debug_unlerped; Log("hotkey: debug view (magenta = not interpolated) %d", C.debug_unlerped); }
    if (f10) g_dumpNext = true;
    if (f12) { g_capLeft = 24; Log("hotkey: capturing next 24 frames"); }
    if (f11 || kMode) { C.mode = (C.mode + 1) % 3; Log("hotkey: mode=%d (%s)", C.mode, C.mode == 0 ? "passthrough" : C.mode == 1 ? "duplicate" : "interpolate"); }
}

static void Stats(int64_t now) {
    if (!C.stats_interval) return;
    if (!St.tLast) { St.tLast = now; return; }
    double el = Ms(now - St.tLast) / 1000.0;
    if (el < C.stats_interval) return;
    double ticks = (double)std::max<uint64_t>(St.ticks, 1);
    double sl = (double)std::max<uint64_t>(St.sleeps, 1);
    if (St.minimizedTicks) Log("  minimized: %llu ticks without extra frames", (unsigned long long)St.minimizedTicks);
    if (St.gpuRealN || St.gpuInterpN)
        Log("  GPU time: game frame %.2f ms (%llu measured), in-between frame %.2f ms (%llu measured)",
            St.gpuRealMs / (double)std::max<uint64_t>(St.gpuRealN, 1), (unsigned long long)St.gpuRealN,
            St.gpuInterpMs / (double)std::max<uint64_t>(St.gpuInterpN, 1), (unsigned long long)St.gpuInterpN);
    if (St.rgWins || St.rgLWins || St.rgFlushes) {
        double fr = (double)std::max<uint64_t>(St.rgFrames, 1);
        int top[4] = {-1, -1, -1, -1};
        for (int i = 0; i < kWhyCount; i++) {
            if (!St.rgWhy[i]) continue;
            for (int k = 0; k < 4; k++)
                if (top[k] < 0 || St.rgWhy[i] > St.rgWhy[top[k]]) {
                    for (int m = 3; m > k; m--) top[m] = top[m - 1];
                    top[k] = i;
                    break;
                }
        }
        char why[256];
        int n = 0;
        why[0] = 0;
        for (int k = 0; k < 4 && top[k] >= 0; k++)
            n += snprintf(why + n, sizeof why - n, "%s%s %.1f (%.2f ms)", k ? ", " : "", WhyName(top[k]), St.rgWhy[top[k]] / fr,
                          St.rgWhyMs[top[k]] / fr);
        Log("  regroup: in-between frames %.1f windows, render target switches %.0f -> %.0f | game frame %.1f windows, %.0f -> %.0f, "
            "run in %.1f parts (%s), %.0f state reads answered | windows ended by: 5th target %.1f, shared surface %.1f, "
            "sampled before %.1f, samples target %.1f, feedback %.1f (per frame)",
            St.rgWins / fr, St.rgIn / fr, St.rgOut / fr, St.rgLWins / fr, St.rgLIn / fr, St.rgLOut / fr, St.rgFlushes / fr, why,
            St.rgLocal / fr, St.rgEnd[1] / fr, St.rgEnd[2] / fr, St.rgEnd[3] / fr, St.rgEnd[4] / fr, St.rgEnd[5] / fr);
        if (St.tlFlushes || St.lockCopies)
            Log("  texture locks under deferred draws: copied %.1f/frame (%llu copies could not be made), executed the game frame %.1f/frame | "
                "the last %ux%u format %d pool %d usage %lx flags %lx after draw %u of %zu",
                St.lockCopies / fr, (unsigned long long)St.lockCopyFails, St.tlFlushes / fr, g_tlLast.Width, g_tlLast.Height, g_tlLast.Format,
                g_tlLast.Pool, g_tlLast.Usage, g_tlLastFlags, g_tlLastAt, g_fr[PrevIdx(1)].draws.size());
        if (C.shadow_reuse || St.srTicks)
            Log("  shadow reuse: %.0f%% of ticks (%.2f ms/tick to prepare), %.0f frozen constant registers/tick, copies %.1f/frame | not reused: off %llu, "
                "no copies %llu, -%llu, drawn into later %llu, vertex texture %llu, sampled after clear %llu, "
                "shader not analysed %llu, sampled while drawn %llu (clear in a skipped window %llu)",
                100.0 * St.srTicks / std::max<uint64_t>(St.sleeps, 1), St.srMs / std::max<uint64_t>(St.sleeps, 1),
                St.srFrozen / (double)std::max<uint64_t>(St.srTicks, 1),
                St.srSnaps / fr, (unsigned long long)St.srOff[1], (unsigned long long)St.srOff[2], (unsigned long long)St.srOff[3],
                (unsigned long long)St.srOff[4], (unsigned long long)St.srOff[5], (unsigned long long)St.srOff[6],
                (unsigned long long)St.srOff[7], (unsigned long long)St.srOff[0], (unsigned long long)St.srOff[9]);
        Log("  regroup lists: %.1f built/frame, %.2f ms/frame, in-between analysis %.2f ms/frame (game frame's reused %.0f%%) | "
            "in-between frame runs %.0f commands (recorded %.0f) | game frame at EndScene %.2f ms | matching in the background "
            "%.0f%% of ticks, %.2f ms there, waited %.2f ms/tick, stale %llu",
            St.rgBuilds / fr, St.rgBuildMs / fr, St.rgAnaMs / fr, 100.0 * St.rgReuse / fr,
            St.replayCmds / (double)std::max<uint64_t>(St.replayN, 1), St.cmdCount / fr, St.endFlushMs / fr,
            100.0 * St.pjUsed / fr, St.pjMs / (double)std::max<uint64_t>(St.pjUsed, 1), St.pjWaitMs / fr, (unsigned long long)St.pjStale);
    }
    if (C.profile) {
        static const char* ph[] = {"game", "game frame executed", "in-between frames", "matching (on this thread / waiting)", "rest of ours"};
        LONG tot = 0;
        for (auto& r : g_prCount)
            for (LONG v : r) tot += v;
        char line[700];
        int n = 0;
        for (int p = 0; p < 5 && tot; p++) {
            LONG s = g_prCount[p][0] + g_prCount[p][1] + g_prCount[p][2] + g_prCount[p][3];
            if (!s) continue;
            n += snprintf(line + n, sizeof line - n, " | %s %.1f%%: mod %.0f%% d3d9 %.0f%% system %.0f%% other %.0f%%", ph[p], 100.0 * s / tot,
                          100.0 * g_prCount[p][0] / s, 100.0 * g_prCount[p][1] / s, 100.0 * g_prCount[p][2] / s, 100.0 * g_prCount[p][3] / s);
        }
        if (n) Log("  profile (render thread, %ld samples)%s", tot, line);
        // the code histograms (tools\prof.py with build\d3d9.map): game frame executed, in-between frames
        for (int h = 0; h < 2; h++) {
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%s\\mhfz-fps-prof-%s.bin", g_dir, h ? "inbetween" : "gameframe");
            if (FILE* fp = fopen(path, "wb")) {
                uint32_t base = (uint32_t)g_prRange[0][0];
                fwrite(&base, 4, 1, fp);
                fwrite(g_prHist[h], 4, 1 << 16, fp);
                fwrite(g_prHist[h], 4, 1 << 16, fp);
                fclose(fp);
            }
        }
        for (auto& r : g_prCount)
            for (auto& v : r) v = 0;
    }
    if (St.constSent + St.constSkipped)
        Log("  in-between frames: constant uploads sent %.0f / skipped (no change) %.0f, other states sent %.0f / skipped %.0f per frame, "
            "%.2f ms precomputing the blended constants",
            St.constSent / (double)std::max<uint64_t>(St.replayN, 1), St.constSkipped / (double)std::max<uint64_t>(St.replayN, 1),
            St.stateSent / (double)std::max<uint64_t>(St.replayN, 1), St.stateSkipped / (double)std::max<uint64_t>(St.replayN, 1),
            St.pvMs / (double)std::max<uint64_t>(St.replayN, 1));
    MemoryCheck();
    if (St.piN)
        Log("  present intervals: mean %.2f ms, deviation %.2f, longest %.2f | closer than one refresh (%.0f Hz): %.1f%%", St.piSum / St.piN,
            std::sqrt(std::max(0.0, St.piSum2 / St.piN - (St.piSum / St.piN) * (St.piSum / St.piN))), St.piMax, g_refresh,
            100.0 * St.piFast / St.piN);
    if (C.latency_trim)
        Log("  lower input latency: the game held %.2f ms after its real frame on %.0f%% of ticks (next hold %.2f ms)",
            St.ltTicks ? St.ltHeldMs / St.ltTicks : 0.0, 100.0 * St.ltTicks / std::max<uint64_t>(St.ticks, 1), g_ltHold);
    Log("  render targets carried over from the previous tick (sampled before drawn into):%s | restored after the in-between frames %.1f/tick, "
        "given to the in-between frames as the game frame had them %.1f/tick (carry_restore %d)", g_carried[0] ? g_carried : " (not checked)",
        St.carryRestores / sl, St.carryInputs / sl, C.carry_restore);
    Log("  camera paths: %.1f contexts/tick, %.1f with an estimate (%.1f by objects explained), %.1f with a path, %.1f packets following one (%.1f shared camera matrices, "
        "%.1f still under another context's path) | repeated objects paired by the motion around them: %.1f groups/tick (field_match %d), poor pairings re-paired by another prediction %.1f, again once the camera was known %.1f (origins' maps %.1f)",
        St.camCtx / sl, St.camSolved / sl, St.camFit / sl, St.camPath / sl, St.camPathPairs / sl, St.camShared / sl, St.camJoined / sl, St.fieldGroups / sl, C.field_match,
        St.groupsRescued / sl, St.groupsAgain / sl, St.camAnchor / sl);
    Log("  text: %.1f strings (%.0f glyph quads) matched whole per tick, %.1f glyphs moved with their string | copies of the same text "
        "(which one a string continues is a guess): %.1f strings/tick, of them %.1f moved with the 3D objects around them, %.1f left as they are "
        "| glyphs re-slotted in the atlas (no counterpart by glyphs): %.1f strings/tick matched by layout, %.1f left as they are | glyphs known by "
        "their pixels %.0f/tick (atlas copies %.1f/tick) | lerp_nonpos %d",
        St.textRuns / sl, St.textRunQuads / sl, St.textRunMoved / sl, St.textGuesses / sl, St.textGeo / sl, St.textLeft / sl, St.textLayout / sl,
        St.textNoLayout / sl, St.ckeyGlyphs / sl, St.atlasSnaps / sl, C.lerp_nonpos);
    Log("  timing: game late (skipped limiter) %llu ticks, backoff %llu ticks, gpu tail %.2f ms | camera-estimated packets/tick %.1f, rejected pairs/tick %.1f (camera-check %llu), screen-crossing pairs %llu, screen groups re-matched %llu, screen quads dropped %llu, camera-facing estimates %llu, steady-motion lerps %llu, position slots that switched (not blended) %llu (rows switched with them %llu), screen quads moved with neighbours %llu, constants paired by reader %llu",
        (unsigned long long)St.gameLate, (unsigned long long)St.backoffTicks, St.gpuTailMs / std::max<uint64_t>(St.gpuTailN, 1),
        St.synth / (double)std::max<uint64_t>(St.sleeps, 1), St.rejected / (double)std::max<uint64_t>(St.sleeps, 1),
        (unsigned long long)St.verifyRejected, (unsigned long long)St.crossing,
        (unsigned long long)St.screenRefined, (unsigned long long)St.screenDropped, (unsigned long long)St.synthBillboard,
        (unsigned long long)St.steadyLerps, (unsigned long long)St.slotJumps, (unsigned long long)St.slotRows, (unsigned long long)St.screenSynth, (unsigned long long)St.livePairs);
    Log("stats %.0fs: real %.2f fps, shown %.1f fps | ticks w/ sleep %llu, extra/tick %.2f, missed %llu, late %llu (%.1f ms avg, GPU waits cut %llu) | "
        "logic %.2f ms, pair %.2f ms, replay %.2f ms + present %.2f ms | draws/frame %.0f (post-sleep %.1f), rec %.0f KB | "
        "matched %.1f%%, pairs/tick %.0f, snaps/tick %.1f, up-snaps/tick %.1f, unpaired-pos/tick %.1f, cuts %llu | "
        "foreign %llu, SB create/begin/capture/apply %llu/%llu/%llu/%llu, updates %llu, rtdata %llu",
        el, St.ticks / el, (St.ticks + St.interpFrames) / el, (unsigned long long)St.sleeps,
        St.interpFrames / sl, (unsigned long long)St.missed, (unsigned long long)St.lateExit,
        St.lateExit ? St.lateMs / St.lateExit : 0.0, (unsigned long long)St.gpuWaitCut,
        St.logicMs / sl, St.pairMs / sl, St.replayMs / std::max<uint64_t>(St.replayN, 1), St.presentMs / std::max<uint64_t>(St.replayN, 1),
        St.draws / ticks, St.postSleepDraws / ticks, St.cmdBytes / ticks / 1024.0,
        100.0 * St.matched / std::max<uint64_t>(St.matched + St.unmatched, 1), St.pairs / sl, St.snaps / sl, St.upSnaps / sl,
        St.unpairedPos / sl, (unsigned long long)St.cuts, (unsigned long long)St.foreign, (unsigned long long)St.sbCreate,
        (unsigned long long)St.sbBegin, (unsigned long long)St.sbCapture, (unsigned long long)St.sbApply,
        (unsigned long long)St.updates, (unsigned long long)St.rtData);
    if (St.lostReports)
        Log("device lost: %llu Present(s) said so, the in-between frames resumed %llu time(s) without a Reset", (unsigned long long)St.lostReports,
            (unsigned long long)St.lostResumes);
    if (C.input_latch || St.inputLatched)
        Log("input_latch: %llu quick presses handed to the game, %llu device reads while it slept (%d devices)",
            (unsigned long long)St.inputLatched, (unsigned long long)St.inputSamples, g_ldN);
    // recorded commands per frame, most frequent first
    int order[OP_MAX];
    for (int i = 0; i < OP_MAX; i++) order[i] = i;
    std::sort(order, order + OP_MAX, [](int a, int b) { return St.ops[a] > St.ops[b]; });
    char line[512];
    int n = 0;
    for (int i = 0; i < 10 && St.ops[order[i]]; i++)
        n += snprintf(line + n, sizeof line - n, " %s %.0f", kOp[order[i]], St.ops[order[i]] / ticks);
    if (n) Log("  cmds/frame:%s", line);
    n = 0;
    for (int k = 0; k < (int)(sizeof kPhase / sizeof *kPhase); k++)
        n += snprintf(line + n, sizeof line - n, " %s %.2f", kPhase[k], St.phaseMs[k] / sl);
    Log("  pair phases (ms/tick):%s", line);
    if (g_checkCount) {
        Log("  selfcheck: %d checks, avg mean error %.2f, avg bad blocks %.2f%%", g_checkCount, g_checkMeanSum / g_checkCount,
            g_checkBadSum / g_checkCount);
        g_checkCount = 0;
        g_checkMeanSum = g_checkBadSum = 0;
    }
    int64_t keep = now;
    memset(&St, 0, sizeof St);
    St.tLast = keep;
}

static HRESULT PresentBody(Dev* d, const RECT* s, const RECT* dr, HWND w, const RGNDATA* rg);
HOOKDEF(HRESULT, Present, const RECT* s, const RECT* dr, HWND w, const RGNDATA* rg) {
    if (g_inPresent && r_Present) return r_Present(d, s, dr, w, rg);
    g_inPresent++;
    HRESULT hr = PresentBody(d, s, dr, w, rg);
    g_inPresent--;
    return hr;
}
static HRESULT PresentBody(Dev* d, const RECT* s, const RECT* dr, HWND w, const RGNDATA* rg) {
    if (d != g_dev || g_inInterp || GetCurrentThreadId() != g_tid) return o_Present(d, s, dr, w, rg);
    g_lastApi = "Present";
    PairJoin();
    FlushDeferred(kWhyPresent);
    g_defer = 0;
    if (++g_presentCalls <= 3) Log("Present #%llu via hook", (unsigned long long)g_presentCalls);
    Hotkeys();
    g_hasSrc = s != nullptr;
    if (s) g_src = *s;
    g_hasDst = dr != nullptr;
    if (dr) g_dst = *dr;
    g_presentWnd = w;
    Frame& f = g_fr[g_cur];
    St.draws += f.draws.size();
    St.cmdBytes += f.buf.size();
    St.ticks++;
    if (g_dumpNext && f.valid) { g_dumpNext = false; if (C.mode == 2 && g_fr[PrevIdx(1)].valid) BuildPairs(f, g_fr[PrevIdx(1)]); DumpFrames(); }
    if (C.selfcheck && C.mode == 2 && f.valid && !f.draws.empty()) {  // keep the real frame for the self-check
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            D3DSURFACE_DESC dd, id;
            bb->GetDesc(&dd);
            if (f.img) {
                f.img->GetDesc(&id);
                if (id.Width != dd.Width || id.Height != dd.Height || id.Format != dd.Format) { f.img->Release(); f.img = nullptr; }
            }
            if (!f.img) g_dev->CreateRenderTarget(dd.Width, dd.Height, dd.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &f.img, nullptr);
            if (f.img && SUCCEEDED(o_StretchRect(g_dev, bb, nullptr, f.img, nullptr, D3DTEXF_NONE))) f.hasImg = true;
            bb->Release();
        }
    }
    Capture("r", 1.0f);
    g_tickNo++;
    int64_t t = Qpc();
    f.presentAt = t;
    // the game skipped our limiter hook this tick (it was late): back off for a while
    if (C.enabled && C.mode > 0 && f.valid && !f.draws.empty() && !g_sleptThisFrame && g_lastReal) {
        St.gameLate++;
        g_backoffUntil = t + g_qpf.QuadPart * 2;
    }
    GpuTimerEnd(g_gtReal);
    {
        double ms;
        if (GpuTimerRead(g_gtReal, ms)) { St.gpuRealMs += ms; St.gpuRealN++; }
        if (GpuTimerRead(g_gtInterp, ms)) { St.gpuInterpMs += ms; St.gpuInterpN++; }
    }
    DrawOverlay();
    OvRecord(true);
    NotePresent();
    g_heartbeat = Qpc();
    HRESULT hr = o_Present(d, s, dr, w, rg);
    if (hr == D3DERR_DEVICELOST) NoteLost("the game's");
    else if (g_lost && SUCCEEDED(hr)) {
        // the game's own frame went out: the device isn't lost. (DXVK can say "lost" for a moment - at start-up, while it
        // sets up the display - and a windowed game never resets, so waiting for a Reset kept the mod off all session.)
        g_lost = false;
        St.lostResumes++;
        Log("device answers again - in-between frames resume");
    }
    g_lastReal = t;
    if (g_capFlush) { FlushCaptures(true); g_lastReal = Qpc(); }
    if (f.valid && !f.draws.empty()) g_cur = (g_cur + 1) % 3;
    // replay tracer: a traced frame that was not interpolated (cut, late...) is retried on the next one
    if (g_trState == 2) { g_trState = 0; g_trWant = true; }
    if (!g_trLast) g_trLast = t;
    if (C.trace_interval_s > 0 && Ms(t - g_trLast) >= C.trace_interval_s * 1000.0) g_trWant = true;
    if (g_trWantAt && t >= g_trWantAt) { g_trWantAt = 0; g_trWant = true; }
    BeginFrame();
    if (g_trWant && C.mode == 2 && g_fr[g_cur].valid) {
        g_trWant = false;
        g_trLast = t;
        TraceArm();
    }
    if (g_trState == 2) g_defer = 0;  // (the tracer snapshots the game's draws as they are made)
    g_sleptThisFrame = false;
    g_afterSleep = false;
    // latency_trim: the game's next tick starts later by what its first in-between frame would wait for anyway (it reads its
    // input that much later; its limiter sleeps that much less, so the tick and every frame keep their times)
    g_ltHeld = 0;
    if (C.latency_trim && C.enabled && C.mode == 2 && t >= g_backoffUntil && g_ltHold > 0 && !g_lost) {
        int64_t until = Qpc() + (int64_t)(g_ltHold * g_qpf.QuadPart / 1000.0);
        WaitQ(until);
        g_ltHeld = g_ltHold;
        St.ltHeldMs += g_ltHold;
        St.ltTicks++;
    }
    g_frameStart = Qpc();
    GpuTimerBegin(g_gtReal);  // the game's next frame
    Stats(g_frameStart);
    return hr;
}

static HRESULT ResetBody(Dev* d, D3DPRESENT_PARAMETERS* pp);
HOOKDEF(HRESULT, Reset, D3DPRESENT_PARAMETERS* pp) {
    if (g_inReset && r_Reset) return r_Reset(d, pp);
    g_inReset++;
    HRESULT hr = ResetBody(d, pp);
    g_inReset--;
    return hr;
}
static HRESULT ResetBody(Dev* d, D3DPRESENT_PARAMETERS* pp) {
    if (d != g_dev) return o_Reset(d, pp);
    g_lastApi = "Reset";
    FlushDeferred(kWhyReset);
    ReleaseAll();
    if (pp && C.present_interval >= 0) pp->PresentationInterval = C.present_interval ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = o_Reset(d, pp);
    g_lost = FAILED(hr);
    if (pp) g_pp = *pp;
    Log("Reset -> %08lx  %ux%u fmt %d windowed %d interval %08x", hr, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
        pp ? pp->BackBufferFormat : 0, pp ? pp->Windowed : -1, pp ? pp->PresentationInterval : 0);
    return hr;
}

// ------------------------------------------------------------------------------------------------ device creation
static double QueryRefresh(HWND w) {
    HMONITOR mon = MonitorFromWindow(w ? w : GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof mi;
    DEVMODEA dm{};
    dm.dmSize = sizeof dm;
    if (GetMonitorInfoA(mon, &mi) && EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return dm.dmDisplayFrequency;
    return 60.0;
}

static void** g_vt;  // the vtable we patched last
static void HookDeviceVT(Dev* dev) {
    void** vt = *(void***)dev;
    g_vt = vt;
    if (!r_Present && vt[17] != (void*)&hk_Present) r_Present = (Present_t)vt[17];
    if (!r_Reset && vt[16] != (void*)&hk_Reset) r_Reset = (Reset_t)vt[16];
    if (!r_EndScene && vt[42] != (void*)&hk_EndScene) r_EndScene = (EndScene_t)vt[42];
#define H(idx, name) do { void* o = HookVT(vt, idx, (void*)&hk_##name); if (o) o_##name = (name##_t)o; } while (0)
    H(16, Reset); H(17, Present); H(30, UpdateSurface); H(31, UpdateTexture); H(32, GetRenderTargetData);
    H(34, StretchRect); H(35, ColorFill); H(37, SetRenderTarget); H(39, SetDepthStencilSurface); H(41, BeginScene);
    H(42, EndScene); H(43, Clear); H(44, SetTransform); H(46, MultiplyTransform); H(47, SetViewport); H(49, SetMaterial);
    H(51, SetLight); H(53, LightEnable); H(55, SetClipPlane); H(57, SetRenderState); H(59, CreateStateBlock);
    H(45, GetTransform); H(48, GetViewport); H(58, GetRenderState);
    H(60, BeginStateBlock); H(61, EndStateBlock); H(65, SetTexture); H(67, SetTextureStageState); H(69, SetSamplerState);
    H(75, SetScissorRect); H(77, SetSoftwareVertexProcessing); H(79, SetNPatchMode); H(81, DrawPrimitive);
    H(82, DrawIndexedPrimitive); H(83, DrawPrimitiveUP); H(84, DrawIndexedPrimitiveUP); H(87, SetVertexDeclaration);
    H(89, SetFVF); H(91, CreateVertexShader); H(92, SetVertexShader); H(94, SetVertexShaderConstantF); H(96, SetVertexShaderConstantI);
    H(98, SetVertexShaderConstantB); H(100, SetStreamSource); H(102, SetStreamSourceFreq); H(104, SetIndices); H(106, CreatePixelShader);
    H(107, SetPixelShader); H(109, SetPixelShaderConstantF); H(111, SetPixelShaderConstantI); H(113, SetPixelShaderConstantB);
    H(38, GetRenderTarget); H(40, GetDepthStencilSurface); H(64, GetTexture); H(68, GetSamplerState);
#undef H
    // every other method first executes the deferred game frame (FlushDeferred) - except what neither depends on
    // nor affects the order of drawing: device queries and resource creation
    for (int i = 3; i < 119; i++) {
        bool skip = false;
        for (int h : {16, 17, 30, 31, 32, 34, 35, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 51, 53, 55, 57, 58, 59, 60, 61, 64, 65,
                      67, 68, 69, 75, 77, 79, 81, 82, 83, 84, 87, 89, 91, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 113,
                      3, 4, 6, 7, 8, 9, 15, 19, 23, 24, 25, 26, 27, 28, 29, 36, 86, 118})
            skip |= h == i;
        if (!skip) ThunkVT(vt, i, i);
    }
}

// Something (in MHF's process) resets the device vtable to the runtime's originals. Detect and re-apply.
static uint64_t g_rehooks;
static bool CheckHooks(const char* where) {
    if (g_sbRecording) return false;  // the recording vtable is the runtime's business until EndStateBlock
    void** vt = *(void***)g_dev;
    if (vt == g_vt && vt[17] == (void*)&hk_Present && vt[83] == (void*)&hk_DrawPrimitiveUP && vt[94] == (void*)&hk_SetVertexShaderConstantF)
        return false;
    g_rehooks++;
    if (g_rehooks <= 40 || g_rehooks % 1000 == 0)
        Log("hooks lost (%s, #%llu): vt %p (hooked %p) Present=%p DPUP=%p, last api %s, real presents %llu", where,
            (unsigned long long)g_rehooks, (void*)vt, (void*)g_vt, vt[17], vt[83], g_lastApi, (unsigned long long)g_presentCalls);
    HookDeviceVT(g_dev);
    return true;
}

static void HookDevice(Dev* dev) {
    HookDeviceVT(dev);
    Log("device vtable %p hooked", (void*)g_vt);
    HookResourceVTs(dev);
    // state block vtable (may be per instance too, so every new state block gets checked in HookSB)
    IDirect3DStateBlock9* sb = nullptr;
    if (SUCCEEDED(o_CreateStateBlock(dev, D3DSBT_ALL, &sb)) && sb) {
        HookSB(sb);
        g_sbEnd = sb;
    }
}

static void HookSB(IDirect3DStateBlock9* sb) {
    void** svt = *(void***)sb;
    void* o;
    if ((o = HookVT(svt, 4, (void*)&hk_SBCapture)) && !o_SBCapture) o_SBCapture = (SB_t)o;
    if ((o = HookVT(svt, 5, (void*)&hk_SBApply)) && !o_SBApply) o_SBApply = (SB_t)o;
}

typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static CreateDevice_t o_CreateDevice;
static HRESULT WINAPI hk_CreateDevice(IDirect3D9* self, UINT ad, D3DDEVTYPE type, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    if (pp) {
        Log("CreateDevice: adapter %u type %d flags %08lx  %ux%u fmt %d count %u ms %d swap %d windowed %d autoDS %d dsfmt %d "
            "flags %08lx refresh %u interval %08x hwnd %p",
            ad, type, flags, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount,
            pp->MultiSampleType, pp->SwapEffect, pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat,
            pp->Flags, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval, (void*)pp->hDeviceWindow);
        if (C.present_interval >= 0) pp->PresentationInterval = C.present_interval ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    }
    HRESULT hr = o_CreateDevice(self, ad, type, focus, flags, pp, out);
    Log("CreateDevice -> %08lx dev %p thread %lu", hr, out ? (void*)*out : nullptr, GetCurrentThreadId());
    if (SUCCEEDED(hr) && out && *out) {
        if (g_dev) { Log("new device replaces %p (launcher device?)", (void*)g_dev); ReleaseAll(); g_cur = 0; }
        g_dev = *out;
        g_tid = GetCurrentThreadId();
        g_heartbeat = 0;  // (the watchdog starts watching at this device's first Present or Sleep)
        g_hwnd = pp && pp->hDeviceWindow ? pp->hDeviceWindow : focus;
        if (pp) g_pp = *pp;
        g_refresh = QueryRefresh(g_hwnd);
        HookDevice(g_dev);
        ProfStart(g_realD3D9);
        WatchdogStart();
        bool sl = HookSleep();
        if (C.input_latch) InputHooks();
        TimerResolution1ms();
        g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        if (!g_timer) g_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        Log("hooks installed: sleep=%d, refresh %.0f Hz, target %.0f fps, mode %d", sl, g_refresh, TargetFps(), C.mode);
        char ini[MAX_PATH];
        snprintf(ini, sizeof ini, "%s\\mhfz-fps.ini", g_dir);
        g_capLeft = (int)GetPrivateProfileIntA("fps", "capture_at_start", 0, ini);
        Log("keys (vk | 0x100 ctrl | 0x200 shift | 0x400 alt): save %#x, overlay %#x, camera paths %#x, light fix %#x, fog rule %#x, repeated objects %#x",
            C.key_capture, C.key_overlay, C.key_campaths, C.key_carry, C.key_nonpos, C.key_field);
        if (C.hotkeys) Toast("frame-rate mod on: Ctrl+Shift+1 camera paths, +2 light fix, +3 fog rule, +4 repeated objects, Delete saves");
        if (g_backendFellBack) Toast("DXVK could not start on this PC (it needs Vulkan 1.3) - using Windows' Direct3D");
        BeginFrame();
    }
    return hr;
}

extern "C" IDirect3D9* WINAPI MyDirect3DCreate9(UINT sdk) __asm__("_Proxy_Direct3DCreate9");
extern "C" IDirect3D9* WINAPI MyDirect3DCreate9(UINT sdk) {
    LoadRealD3D9();
    auto real = (IDirect3D9 * (WINAPI*)(UINT)) g_real[10];
    if (!real) { Log("real Direct3DCreate9 not found"); return nullptr; }
    IDirect3D9* d3d = real(sdk);
    char backend[MAX_PATH * 3] = "?";
    auto backendName = [&] {  // (UTF-8 for the log: the path may hold a user name with ø, é...)
        wchar_t w[MAX_PATH];
        if (GetModuleFileNameW(g_realD3D9, w, MAX_PATH)) WideCharToMultiByte(CP_UTF8, 0, w, -1, backend, sizeof backend, nullptr, nullptr);
    };
    backendName();
    Log("Direct3DCreate9(%u) -> %p (d3d9: %s)", sdk, (void*)d3d, backend);
    // a backend that loaded but can't start (DXVK without a Vulkan 1.3 driver returns nothing): the game still starts,
    // on Windows' own Direct3D 9
    if (!d3d && g_d3d9Backend[0] && UseSystemD3D9()) {
        real = (IDirect3D9 * (WINAPI*)(UINT)) g_real[10];
        d3d = real ? real(sdk) : nullptr;
        backendName();
        Log("the backend could not start - using Windows' own Direct3D 9 instead: -> %p (d3d9: %s)", (void*)d3d, backend);
        g_backendFellBack = d3d != nullptr;
    }
    if (d3d && C.enabled) {
        void* o = HookVT(*(void***)d3d, 16, (void*)&hk_CreateDevice);
        if (o) o_CreateDevice = (CreateDevice_t)o;
    }
    return d3d;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        // Never unloaded: our exception handlers, the Sleep hook and the worker threads point into this DLL. Rain's
        // client loads d3d9.dll, frees it and loads it again; the handlers of the freed copy stayed registered,
        // another DLL was later mapped at the same address, and the next exception (an ordinary one inside the
        // graphics driver) jumped into it and killed the game. A pinned module stays until the process ends.
        HMODULE self = nullptr;
        BOOL pinned = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)(void*)&DllMain, &self);
        QueryPerformanceFrequency(&g_qpf);
        g_t0 = Qpc();
        GetModuleFileNameA(h, g_dir, MAX_PATH);
        if (char* s = strrchr(g_dir, '\\')) *s = 0;
        g_modBase = (uintptr_t)h;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(g_modBase + ((IMAGE_DOS_HEADER*)h)->e_lfanew);
        g_modEnd = g_modBase + nt->OptionalHeader.SizeOfImage;
        g_codeBeg = g_modBase + nt->OptionalHeader.BaseOfCode;
        g_codeEnd = g_codeBeg + nt->OptionalHeader.SizeOfCode;
        LoadConfig();
        AddVectoredExceptionHandler(0, CrashNote);
        IniChanged();  // remember the current timestamp; later edits trigger a live reload
        char path[MAX_PATH], prev[MAX_PATH];
        snprintf(path, sizeof path, "%s\\mhfz-fps.log", g_dir);
        snprintf(prev, sizeof prev, "%s\\mhfz-fps.prev.log", g_dir);
        MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);  // the last session's log survives one restart (crash reports)
        g_log = fopen(path, "w");
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        Log("mhfz-fps (built " __DATE__ " " __TIME__ ") loaded into %s (pid %lu)%s", exe, GetCurrentProcessId(), pinned ? "" : " - could not pin the DLL");
        if (C.exc_log) { AddVectoredExceptionHandler(1, ExcLog); Log("exception log on"); }
        SetupBackend();
        Log("config: enabled %d mode %d target_fps %d interval %d vs %d xform %d up %d ps %d snap %.2f margin %dus",
            C.enabled, C.mode, C.target_fps, C.present_interval, C.interp_vs, C.interp_xform, C.interp_up, C.interp_ps,
            C.snap_ratio, C.margin_us);
    }
    if (reason == DLL_PROCESS_DETACH && !reserved) Log("unloaded by FreeLibrary (should not happen: the DLL is pinned)");
    if (reason == DLL_PROCESS_DETACH && reserved) Log("process exit");
    return TRUE;
}
