// The orbit camera's game side (camera=1 in mhfz-fps.ini, off by default; the math is in orbit.h, the background in
// docs/CAMERA.md). Included by fps.cpp.
//
// This is the one place where MHFZ Plus writes game memory - the owner's exception to the rule, for this opt-in only.
// Once per tick, when the game builds its view (its own D3DXMatrixLookAtRH call for the gameplay camera), it writes the
// camera's position and target in the game's camera object, and the camera direction the game keeps for the hunter's
// movement (two 16-bit angles), so that the hunter walks the way the camera looks. Never code, never anything else.
// The game's own code is not touched: the hook sits in d3dx9_43.dll (Windows' or Wine's), like the Sleep hook in
// KernelBase. The collision asks the game's own geometry and ground-height queries.
//
// The addresses hold for MezeLounge's 2019 client (mhfo-hd.dll) and Rain's client (client.dll, mapped at 0x10000000 by
// client.exe), whose code is the same here byte for byte. Before anything is hooked, 13 blocks of the game's code that
// use these addresses are compared with what they were when this was written (FNV-1a hashes, from Frontier Orbit's
// exact-byte checks): any difference - another game version - and the camera stays off.
#include "orbit.h"

// ------------------------------------------------------------------------------------------------ the game's layout
static const uintptr_t kCamEngineBase = 0x10000000;  // (the code holds absolute addresses: it only works at this base)
enum : uintptr_t {
    kCamObjSlot = 0xedaad60,    // -> the camera object (0xe7fff40): eye at +0x80, target at +0x8c, up at +0x98 (floats)
    kCamObj = 0xe7fff40,
    kCamPlayerSlot = 0x1c4a0d4,  // -> the hunter: position at +0xac (3 floats), facing at +0xa4 (16-bit angle)
    kCamYaw = 0x1c4a168,        // the camera direction the hunter moves by, and its copy (16-bit angles)
    kCamYawCopy = 0x1c4a16a,
    kCamChannel = 0x1c4a409,    // which camera the game uses: 0 = the gameplay camera
    kCamSpecial = 0x1c4a41d,    // a special camera (scripted, cutscene): -1 = none
    kCamMapSlot = 0xe77dc2c,    // -> the map's collision grid (13 words: cell sizes and counts, walls, floor cells)
    kCamMapLow = 0x18e469c,     // the map grid's lower bound (float)
    kCamTraceFn = 0x8d0f10,     // geometry sweep: ECX = start, (end, radius, hit out, surface mask), caller cleans, EAX = hit
    kCamGroundFn = 0x8202a0,    // ground height under a point: ESI = point, XMM0 = height
    kCamLookAtIat = 0x15d242c,  // the game's import slot for D3DXMatrixLookAtRH (only read: another mod may have taken it)
};
static const struct { uint32_t rva, size; uint64_t fnv; } kCamGuards[] = {
    {0x0baee10, 64, 0x8b02ae00581da56bull},  // builds the view from the camera object (D3DXMatrixLookAtRH)
    {0x0baf300, 61, 0x88246d0b93d80ea7ull},
    {0x0baed60, 63, 0x8e51db4df6e1918cull},
    {0x0821250, 61, 0x41b4251e6209fbd9ull},  // the game's camera controller
    {0x081fe90, 64, 0x04cfd32aa9527676ull},
    {0x08d0f10, 59, 0x8adad379fcf4999full},  // the geometry sweep and its helpers
    {0x08d0b40, 63, 0x6687755441733899ull},
    {0x08ca760, 61, 0x7e0c205979376936ull},
    {0x13e84e0, 62, 0x88480096d687c5d0ull},
    {0x08cbe30, 64, 0x729d33f2b77c9c95ull},
    {0x08cb000, 64, 0xdc3d7a16a810ef22ull},
    {0x08c74c0, 64, 0x20bfde561d5152d0ull},
    {0x08202a0, 64, 0x3bd6e71339485667ull},  // the ground height
};
static const struct { uint32_t rva, size; } kCamData[] = {
    {kCamLookAtIat, 4}, {kCamMapLow, 4}, {kCamPlayerSlot, 4}, {kCamYaw, 4}, {kCamChannel, 1},
    {kCamSpecial, 1}, {kCamMapSlot, 4}, {kCamObj, 0x98}, {kCamObjSlot, 4},
};

static uint8_t* g_camBase;   // the game DLL (null: not found or not this version)
static bool g_camStarted, g_camLive;  // CameraStart ran / the hook is in and the checks passed
static long g_camEpoch = 1;  // (switching the camera on resyncs it with the game's camera)
static bool g_camUserOff;    // switched off with key_camera this session
static void* g_camLookAtFn;  // D3DXMatrixLookAtRH in d3dx9_43.dll
static OrbitCam g_orbit;
static struct {
    uint64_t calls, steps, active, writes, collisionHits, noPad, iatTaken;
    double ms, msMax;
    int lastState = -1;
} g_camSt;

// Readable memory, all of it.
static bool CamReadable(uintptr_t a, SIZE_T n) {
    while (n) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        SIZE_T in = (uintptr_t)mi.BaseAddress + mi.RegionSize - a;
        if (in >= n) return true;
        a += in;
        n -= in;
    }
    return true;
}
static bool CamWine() {
    static int wine = -1;
    if (wine < 0) wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}
// Pointers the game may have freed. Under Wine each ReadProcessMemory is a trip to its server (some 0.1 ms) while the
// memory query is not: there, the query and a plain copy.
static bool CamRead(uintptr_t a, void* out, SIZE_T n) {
    if (CamWine()) {
        if (!CamReadable(a, n)) return false;
        memcpy(out, (const void*)a, n);
        return true;
    }
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (void*)a, out, n, &got) && got == n;
}
// The address range lies in the game DLL's own committed memory (executable: code).
static bool CamRange(uintptr_t base, uintptr_t rva, SIZE_T size, bool code) {
    MEMORY_BASIC_INFORMATION mi;
    uintptr_t a = base + rva;
    if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        (uintptr_t)mi.AllocationBase != base)
        return false;
    if (size > mi.RegionSize - (a - (uintptr_t)mi.BaseAddress)) return false;
    return !code || (mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
static uint64_t CamFnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
// The game DLL: MezeLounge loads mhfo-hd.dll normally; Rain's client.exe maps its (encrypted) game DLL at 0x10000000
// itself, so it is in no module list. 0: none there, or this game version's code differs.
static int CamFindEngine(uint8_t** out) {
    HMODULE m = GetModuleHandleA("mhfo-hd.dll");
    uintptr_t base = m ? (uintptr_t)m : kCamEngineBase;
    if (base != kCamEngineBase || !CamRange(base, 0, 4096, false)) return -1;
    if (!C.camera_test || C.camera_test == 2) {
        for (size_t i = 0; i < sizeof kCamGuards / sizeof kCamGuards[0]; i++) {
            uint8_t b[64];
            if (!CamRange(base, kCamGuards[i].rva, kCamGuards[i].size, true) || !CamRead(base + kCamGuards[i].rva, b, kCamGuards[i].size) ||
                CamFnv(b, kCamGuards[i].size) != kCamGuards[i].fnv)
                return 1 + (int)i;
        }
    }
    for (size_t i = 0; i < sizeof kCamData / sizeof kCamData[0]; i++)
        if (!CamRange(base, kCamData[i].rva, kCamData[i].size, false)) return 100 + (int)i;
    *out = (uint8_t*)base;
    return 0;
}

// ------------------------------------------------------------------------------------------------ the game's queries
// The game's own functions take their arguments in registers; these call them the way the game does.
__attribute__((naked, noinline)) static int CamTraceCall(void* fn, const float* start, const float* end, float radius, float* hit, unsigned mask) {
    __asm__ volatile(
        "push %ebp\n mov %esp, %ebp\n push 28(%ebp)\n push 24(%ebp)\n push 20(%ebp)\n push 16(%ebp)\n"
        "mov 12(%ebp), %ecx\n call *8(%ebp)\n add $16, %esp\n leave\n ret");
}
__attribute__((naked, noinline)) static void CamGroundCall(void* fn, const float* point, float* height) {
    __asm__ volatile(
        "push %ebp\n mov %esp, %ebp\n push %esi\n mov 12(%ebp), %esi\n call *8(%ebp)\n pop %esi\n"
        "mov 16(%ebp), %edx\n movss %xmm0, (%edx)\n leave\n ret");
}

// The map's collision grid as read once a tick (13 words: wall cell sizes and counts at 2..5, the walls at 6, ground
// cell sizes and counts at 8..11, the ground cells at 12). cells: the ground cells, checked readable as a whole once,
// then read plainly (the ground is sampled some 30 times a tick); null without them.
struct CamMap { uint32_t map, g[13]; float low; const uint32_t* cells; };
struct CamGround { const CamMap* m; double reference, clearance, low; };
static bool CamGroundHeight(const float p[3], double* height, void* ctx) {
    auto c = (CamGround*)ctx;
    const uint32_t* g = c->m->g;
    if (p[0] < c->low || p[2] < c->low || p[0] < 0 || p[2] < 0 || p[0] >= (double)g[11] * g[8] - c->low || p[2] >= (double)g[10] * g[9] - c->low)
        return false;
    uint32_t x = (uint32_t)(p[0] / g[8]), z = (uint32_t)(p[2] / g[9]);
    if (x >= g[11] || z >= g[10] || !c->m->cells[(size_t)x * g[10] + z]) return false;  // (no ground known here)
    float probe[3] = {p[0], (float)std::max(c->reference, p[1] + c->clearance), p[2]}, h = 0;
    CamGroundCall(g_camBase + kCamGroundFn, probe, &h);
    // no ground under the probe returns its own height: that is no floor, not one at the probe
    if (!std::isfinite(h) || std::fabs(h - probe[1]) < 0.001 || std::fabs(h - probe[1]) > 10000) return false;
    *height = h;
    return true;
}
// The collision for OrbitStep: walls (the game camera's own surface mask), floors (a second pass), the map's box and
// the ground height along the way. The camera keeps 28 units (plus 4) from what it would touch.
static bool CamSweep(const float start[3], const float end[3], double* fraction, void* ctx) {
    const double radius = 28, margin = 4;
    auto m = (const CamMap*)ctx;
    const uint32_t* g = m->g;
    if (!m->map || !std::isfinite(m->low) || !g[2] || !g[3] || !g[4] || !g[5] || g[2] > 100000 || g[3] > 100000 || g[4] > 100000 || g[5] > 100000)
        return false;
    double low = m->low + radius, xmax = (double)g[5] * g[2] - low, zmax = (double)g[4] * g[3] - low;
    double box = OrbitBoxFraction(start, end, low, xmax, low, zmax);
    if (box < 0 || xmax <= low || zmax <= low) return false;
    float bounded[3];
    double total = 0, length = 0;
    for (int i = 0; i < 3; i++) {
        double d = end[i] - start[i];
        total += d * d;
        bounded[i] = (float)(start[i] + d * box);
        double b = bounded[i] - start[i];
        length += b * b;
    }
    total = std::sqrt(total);
    length = std::sqrt(length);
    if (total < 1e-5) { *fraction = 1; return true; }
    double allowed = length;
    static const unsigned masks[] = {0x8009, 4};  // walls (the game camera's own mask), then floors
    for (unsigned mask : masks) {
        float hit[3] = {bounded[0], bounded[1], bounded[2]};
        if (CamTraceCall(g_camBase + kCamTraceFn, start, bounded, (float)radius, hit, mask)) {
            for (float v : hit)
                if (!std::isfinite(v)) return false;
            allowed = std::min(allowed, std::max(0.0, OrbitHitFraction(start, bounded, hit) * length - margin));
            g_camSt.collisionHits++;
        }
    }
    if (!m->cells) return false;  // (no ground data: the game's camera is kept)
    CamGround ground{m, start[1], radius + margin, m->low};
    float endpoint[3];
    for (int i = 0; i < 3; i++) endpoint[i] = (float)(start[i] + (bounded[i] - start[i]) * allowed / std::max(length, 0.01));
    allowed *= OrbitGroundFraction(start, endpoint, ground.clearance, 24, CamGroundHeight, &ground);
    *fraction = std::clamp(allowed / total, 0.0, 1.0);
    return true;
}

// ------------------------------------------------------------------------------------------------ the controller
// (XInput through fps.cpp's XPadState: shared with pad_triggers)
static WORD g_camPrevButtons;

// ------------------------------------------------------------------------------------------------ one tick
static OrbitView g_camView;
static double g_camLastStep = -1e9;
static bool g_camHaveView;
static bool g_camYawChecked;  // (the first resync compares the game's direction field with its camera: logged)

static void CamWrite(float* eye, float* target) {
    memcpy(eye, g_camView.eye, sizeof g_camView.eye);
    memcpy(target, g_camView.target, sizeof g_camView.target);
    uint16_t yaw = OrbitGameYaw(g_orbit.orbit.yaw);
    *(volatile uint16_t*)(g_camBase + kCamYaw) = yaw;
    *(volatile uint16_t*)(g_camBase + kCamYawCopy) = yaw;
    g_camSt.writes++;
}

static void CamStep(float* eye, float* target, double now);
static void CamApply(float* eye, float* target) {
    double now = Ms(Qpc() - g_t0);
    // the game calls this twice in a tick while it blends two cameras, right after the first: the second call gets
    // the same view again (timed from the end of the last step - a slow step must not make it look like a new tick)
    if (now - g_camLastStep < 4) {
        if (g_camHaveView) CamWrite(eye, target);
        return;
    }
    CamStep(eye, target, now);
    g_camLastStep = Ms(Qpc() - g_t0);
}
static void CamStep(float* eye, float* target, double now) {
    g_camHaveView = false;
    g_camSt.steps++;
    // another camera mod (Frontier Helper) took the game's call: it would fight this one every tick
    if (*(void* volatile*)(g_camBase + kCamLookAtIat) != g_camLookAtFn) {
        if (!g_camSt.iatTaken++) {
            Log("camera: another program took the game's camera call (Frontier Helper?) - MHFZ Plus's camera stays off");
            Toast("Camera: another camera mod is running - MHFZ Plus's camera is off");
        }
        OrbitSuspend(g_orbit.ref);
        return;
    }
    OrbitSettings& s = g_orbit.set;
    s.yawSpeed = C.camera_yaw_speed;
    s.pitchSpeed = C.camera_yaw_speed * C.camera_tilt_ratio / 100.0;
    s.lookDown = C.camera_look_down;
    s.lookUp = C.camera_look_up;
    s.resetTilt = std::clamp((double)C.camera_reset_tilt, -s.lookUp, s.lookDown);
    s.response = C.camera_smooth;
    s.deadzone = C.camera_deadzone / 100.0;
    s.distance = C.camera_distance / 100.0;
    s.height = C.camera_height;
    s.shoulder = C.camera_shoulder;
    s.invertX = C.camera_invert_x != 0;
    s.invertY = C.camera_invert_y != 0;

    OrbitFrame f;
    f.now = now;
    f.epoch = g_camEpoch;
    f.enabled = C.camera && !g_camUserOff;
    uint8_t channel = *(volatile uint8_t*)(g_camBase + kCamChannel);
    int8_t special = *(volatile int8_t*)(g_camBase + kCamSpecial);
    uint32_t actor = *(volatile uint32_t*)(g_camBase + kCamPlayerSlot);
    uint8_t hunter[0x14];  // (the hunter from +0xa4: facing at +0, position at +8)
    static CamMap m;
    m = CamMap{};
    m.map = *(volatile uint32_t*)(g_camBase + kCamMapSlot);
    m.low = *(const float*)(g_camBase + kCamMapLow);
    f.sceneValid = channel == 0 && special == -1 && actor && CamRead(actor + 0xa4, hunter, sizeof hunter) && m.map &&
                   CamRead(m.map, m.g, sizeof m.g) && m.g[2] && m.g[3] && m.g[4] && m.g[5];
    const uint32_t* g = m.g;
    if (f.sceneValid && g[12] && g[8] && g[8] <= 100000 && g[9] && g[9] <= 100000 && g[10] && g[10] <= 100000 && g[11] && g[11] <= 100000 &&
        (uint64_t)g[10] * g[11] <= (1u << 24) && CamReadable(g[12], (SIZE_T)g[10] * g[11] * 4))
        m.cells = (const uint32_t*)(uintptr_t)g[12];
    f.actor = actor;
    f.map = m.map;
    f.walls = g[6];
    f.floor = g[12];
    if (f.sceneValid) {
        f.heading = *(int16_t*)hunter * kOrbitTau / 65536.0;
        memcpy(f.pos, hunter + 8, sizeof f.pos);
    }
    memcpy(f.eye, eye, sizeof f.eye);
    memcpy(f.target, target, sizeof f.target);
    XPad p{};
    bool pad = f.enabled && f.sceneValid && XPadState(C.camera_pad, &p);
    static double fgAt = -1e9;  // (the window in front, asked 4 times a second: each ask is a trip to Wine's server)
    static bool front;
    if (now - fgAt > 250) {
        DWORD fg = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fg);
        front = fg == GetCurrentProcessId();
        fgAt = now;
    }
    f.input = pad && (front || C.camera_test);
    f.stickX = p.rx;
    f.stickY = p.ry;
    f.recenter = C.camera_l3 && (p.buttons & 0x40) && !(g_camPrevButtons & 0x40);  // (XINPUT_GAMEPAD_LEFT_THUMB)
    g_camPrevButtons = p.buttons;
    if (f.enabled && f.sceneValid && !pad) {  // keyboard and mouse: the game's own camera, which they turn
        g_camSt.noPad++;
        f.sceneValid = false;
    }
    bool resync = g_orbit.ref.pending || !g_orbit.ref.active;
    OrbitState st = OrbitStep(g_orbit, f, CamSweep, &m, g_camView);
    if (st != g_camSt.lastState) {
        Log("camera: %s", kOrbitStateNames[st]);
        g_camSt.lastState = st;
    }
    if (st != kOrbitActive) return;
    if (resync && !g_camYawChecked) {  // (diagnostic: the game's direction field against its camera, before the first write)
        g_camYawChecked = true;
        double x = f.target[0] - f.eye[0], z = f.target[2] - f.eye[2];
        int16_t diff = (int16_t)(*(volatile uint16_t*)(g_camBase + kCamYaw) - OrbitGameYaw(std::atan2(x, z)));
        Log("camera: resync - distance %.0f, height %.0f, the game's direction field is %.1f degrees from its camera",
            g_orbit.ref.radius, g_orbit.ref.height, diff * 360.0 / 65536.0);
    }
    g_camSt.active++;
    g_camHaveView = true;
    CamWrite(eye, target);
}

typedef float*(WINAPI* CamLookAt_t)(float* out, const float* eye, const float* at, const float* up);
static CamLookAt_t o_CamLookAt;
static volatile LONG g_camBusy;
static float* WINAPI hk_CamLookAt(float* out, const float* eye, const float* at, const float* up) {
    // only the gameplay camera's own call: the camera object's eye and target (shadows and other views pass others)
    if (g_camLive) {
        uintptr_t obj = *(volatile uint32_t*)(g_camBase + kCamObjSlot);
        if (obj == (uintptr_t)(g_camBase + kCamObj) && (uintptr_t)eye == obj + 0x80 && (uintptr_t)at == obj + 0x8c &&
            InterlockedCompareExchange(&g_camBusy, 1, 0) == 0) {
            int64_t t0 = Qpc();
            g_camSt.calls++;
            CamApply((float*)eye, (float*)at);
            double ms = Ms(Qpc() - t0);
            g_camSt.ms += ms;
            g_camSt.msMax = std::max(g_camSt.msMax, ms);
            InterlockedExchange(&g_camBusy, 0);
        }
    }
    return o_CamLookAt(out, eye, at, up);
}

// The first whole instructions of a plain prologue (push, mov ebp/edi, sub esp): their length, 0 for anything else.
static int CamPrologueLen(const uint8_t* p) {
    if (p[0] >= 0x50 && p[0] <= 0x57) return 1;
    if ((p[0] == 0x89 && p[1] == 0xE5) || (p[0] == 0x8B && p[1] == 0xEC) || (p[0] == 0x8B && p[1] == 0xFF)) return 2;
    if (p[0] == 0x83 && p[1] == 0xEC) return 3;
    if (p[0] == 0x81 && p[1] == 0xEC) return 6;
    return 0;
}
static bool CamHookLookAt(uint8_t* f) {
    DWORD old;
    bool pad = true;
    for (int i = 1; i <= 5; i++) pad &= f[-i] == 0xCC || f[-i] == 0x90;
    if (f[0] == 0x8B && f[1] == 0xFF && pad) {  // hot-patchable (Windows' d3dx9_43.dll): the way Sleep is hooked
        if (!VirtualProtect(f - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return false;
        f[-5] = 0xE9;
        *(int32_t*)(f - 4) = (int32_t)((uint8_t*)&hk_CamLookAt - f);
        o_CamLookAt = (CamLookAt_t)(void*)(f + 2);
        __atomic_store_n((uint16_t*)f, (uint16_t)0xF9EB, __ATOMIC_SEQ_CST);
        VirtualProtect(f - 5, 7, old, &old);
        FlushInstructionCache(GetCurrentProcess(), f - 5, 7);
        return true;
    }
    // a plain prologue (Wine's own d3dx9_43.dll): its first instructions (5 bytes or more) move to a trampoline that
    // goes on after them, and a jump to the hook takes their place
    int n = 0;
    while (n < 5) {
        int len = CamPrologueLen(f + n);
        if (!len) {
            Log("camera: D3DXMatrixLookAtRH starts with %02x %02x %02x %02x %02x %02x %02x - not hooked (another mod's hook?)",
                f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
            return false;
        }
        n += len;
    }
    auto t = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return false;
    memcpy(t, f, n);
    t[n] = 0xE9;
    *(int32_t*)(t + n + 1) = (int32_t)((f + n) - (t + n + 5));
    FlushInstructionCache(GetCurrentProcess(), t, n + 5);
    if (!VirtualProtect(f, n, PAGE_EXECUTE_READWRITE, &old)) { VirtualFree(t, 0, MEM_RELEASE); return false; }
    o_CamLookAt = (CamLookAt_t)(void*)t;
    uint8_t patch[16];
    patch[0] = 0xE9;
    *(int32_t*)(patch + 1) = (int32_t)((uint8_t*)&hk_CamLookAt - (f + 5));
    for (int i = 5; i < n; i++) patch[i] = 0x90;
    memcpy(f, patch, n);
    VirtualProtect(f, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f, n);
    return true;
}

// Sets the camera up once camera=1: at the device's creation, when switched on while playing, and every 2 seconds
// while no game DLL is there yet (a device made before the game DLL is mapped: CameraPoll). Each check that fails is
// logged once and the camera stays off for the session; the game is untouched then.
static void CameraStart() {
    if (!C.camera || g_camStarted) return;
    uint8_t* base = nullptr;
    int why = CamFindEngine(&base);
    if (why < 0) {
        static bool said;
        if (!said) Log("camera: no game DLL at 0x10000000 yet - looking again while the game runs");
        said = true;
        return;
    }
    g_camStarted = true;
    if (why) {
        Log("camera: not for this game version (%s %d) - the camera stays off", why < 100 ? "code check" : "memory check", why % 100);
        Toast("Camera: not available for this game version");
        return;
    }
    HMODULE dx = GetModuleHandleA("d3dx9_43.dll");
    void* fn = dx ? (void*)GetProcAddress(dx, "D3DXMatrixLookAtRH") : nullptr;
    if (!fn) { Log("camera: d3dx9_43.dll isn't loaded - the camera stays off"); return; }
    if (*(void**)(base + kCamLookAtIat) != fn) {
        Log("camera: the game's D3DXMatrixLookAtRH import points elsewhere (another camera mod?) - the camera stays off");
        Toast("Camera: another camera mod is running - MHFZ Plus's camera is off");
        return;
    }
    g_camBase = base;
    g_camLookAtFn = fn;
    if (!CamHookLookAt((uint8_t*)fn)) { Log("camera: could not hook D3DXMatrixLookAtRH - the camera stays off"); return; }
    g_camLive = true;
    Log("camera: ready (%s, %s hook)%s", GetModuleHandleA("mhfo-hd.dll") ? "mhfo-hd.dll" : "game DLL at 0x10000000",
        o_CamLookAt == (CamLookAt_t)(void*)((uint8_t*)fn + 2) ? "hot-patch" : "trampoline", C.camera_test ? " - TEST MODE" : "");
}

static void CameraPoll() {  // (every frame)
    static int64_t at;
    if (!C.camera || g_camStarted || Qpc() < at) return;
    at = Qpc() + g_qpf.QuadPart * 2;
    CameraStart();
}

static void CameraToggle() {
    if (!g_camLive) return;
    g_camUserOff = !g_camUserOff;
    g_camEpoch++;
    Toast("Camera: orbit %s", g_camUserOff ? "OFF (the game's camera)" : "ON");
}

static void CameraStats() {
    if (!g_camStarted) return;
    if (!g_camLive) { Log("  camera: off (see the camera line at the start)"); return; }
    Log("  camera: %s, %llu ticks active / %llu, %llu without a controller, %llu collision hits, %.3f ms a tick (longest %.3f)",
        g_camSt.lastState >= 0 ? kOrbitStateNames[g_camSt.lastState] : "not called yet", (unsigned long long)g_camSt.active,
        (unsigned long long)g_camSt.steps, (unsigned long long)g_camSt.noPad, (unsigned long long)g_camSt.collisionHits,
        g_camSt.calls ? g_camSt.ms / g_camSt.calls : 0.0, g_camSt.msMax);
    g_camSt.msMax = 0;
}
