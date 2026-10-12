// Orbit camera test (src\orbit.h, src\camera.h), windowless: no Direct3D device and no game. A stand-in game sits at
// 0x10000000 - the camera object, the hunter, the collision grid, the game's geometry and ground queries as small
// stand-ins, the D3DXMatrixLookAtRH import slot - with a stand-in controller; the mod DLL is loaded with camera_test=1
// and the game's camera call is made the way the game makes it, once a tick. Given MezeLounge's mhfo-hd.dll, its
// sections are mapped there as plain data (never run) to check the camera's code checks against the real game, and
// that one changed byte turns the camera off.
//   cameratest.exe [mod folder (default: this exe's folder)] [mhfo-hd.dll]
// Each case runs in a process of its own (the DLL stays once loaded). Exit code: the number of failed checks.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "../src/orbit.h"

// ------------------------------------------------------------------------------------------------ checks
static int g_fail;
static void Check(bool ok, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    printf("  %s ", ok ? "ok  " : "FAIL");
    vprintf(fmt, a);
    printf("\n");
    va_end(a);
    fflush(stdout);
    if (!ok) g_fail++;
}

// ------------------------------------------------------------------------------------------------ orbit.h alone
static void MathTests() {
    printf("orbit.h\n");
    double x, y;
    OrbitStick(3000, 0, 0.18, x, y);
    Check(x == 0 && y == 0, "inside the deadzone the stick is still");
    OrbitStick(32767, 0, 0.18, x, y);
    Check(fabs(x - 1) < 1e-9 && y == 0, "full right is 1 (%.4f)", x);
    OrbitStick(32767, 32767, 0.18, x, y);
    Check(fabs(sqrt(x * x + y * y) - 1) < 1e-9, "a square gate's corner is held to 1 (%.4f)", sqrt(x * x + y * y));
    OrbitStick(-32768, 0, 0.5, x, y);
    Check(fabs(x + 1) < 1e-9, "full left is -1 (%.4f)", x);

    OrbitSettings s;
    Orbit o;
    for (int i = 0; i < 300; i++) OrbitTurn(o, 0, -1, 1 / 30.0, s, false, 0);
    Check(fabs(o.pitch + 65 * kOrbitRad) < 1e-6, "stick down: tilt stops at the look-down limit (%.2f)", o.pitch / kOrbitRad);
    for (int i = 0; i < 300; i++) OrbitTurn(o, 0, 1, 1 / 30.0, s, false, 0);
    Check(fabs(o.pitch - 35 * kOrbitRad) < 1e-6, "stick up: and at the look-up limit (%.2f)", o.pitch / kOrbitRad);
    OrbitSettings inv = s;
    inv.invertY = true;
    Orbit q;
    for (int i = 0; i < 300; i++) OrbitTurn(q, 0, 1, 1 / 30.0, inv, false, 0);
    Check(fabs(q.pitch + 65 * kOrbitRad) < 1e-6, "inverted: stick up looks down (%.2f)", q.pitch / kOrbitRad);
    Orbit t;
    OrbitTurn(t, 1, 0, 0.5, s, false, 0);  // (a hitch: dt is held to 50 ms)
    Check(fabs(t.wantYaw + s.yawSpeed * kOrbitRad * 0.05) < 1e-9, "a long step turns at most 50 ms worth (%.2f)", t.wantYaw / kOrbitRad);
    Orbit r;
    r.yaw = r.wantYaw = 3;
    for (int i = 0; i < 90; i++) OrbitTurn(r, 0, 0, 1 / 30.0, s, i == 0, 1.0);
    Check(fabs(r.yaw - 1.0) < 1e-3 && fabs(r.pitch + 15 * kOrbitRad) < 1e-3, "recentering goes behind the hunter at the reset tilt (%.3f, %.2f)",
          r.yaw, r.pitch / kOrbitRad);
    Orbit w;
    w.yaw = w.wantYaw = 3.1;
    OrbitTurn(w, -1, 0, 1 / 30.0, s, false, 0);
    Check(w.yaw < -3 || w.yaw > 3, "turning across the half turn takes the short way (%.3f)", w.yaw);

    float a[3] = {0, 0, 0}, b[3] = {10, 0, 0}, h[3] = {4, 0, 0};
    Check(fabs(OrbitHitFraction(a, b, h) - 0.4) < 1e-9, "hit fraction");
    float in[3] = {50, 0, 50}, out[3] = {150, 0, 50};
    Check(fabs(OrbitBoxFraction(in, out, 0, 100, 0, 100) - 0.5) < 1e-9, "the map's box cuts the segment at its edge");
    Check(OrbitBoxFraction(out, in, 0, 100, 0, 100) < 0, "a start outside the map's box has no fraction");
    Check(OrbitRecover(100, 40, 1 / 30.0, 5) == 40, "collision closes in at once");
    double grow = OrbitRecover(40, 100, 1 / 30.0, 5);
    Check(grow > 40 && grow < 60, "and grows back smoothly (%.1f)", grow);
    struct Hill { static bool H(const float p[3], double* hh, void*) { *hh = p[0] > 5 ? 10 : 0; return true; } };
    float s0[3] = {0, 5, 0}, s1[3] = {10, 5, 0};
    double gf = OrbitGroundFraction(s0, s1, 2, 24, Hill::H, nullptr);
    Check(gf > 0.45 && gf <= 0.5, "ground: the segment stops before rising ground (%.3f)", gf);
}

// ------------------------------------------------------------------------------------------------ the stand-in game
static uint8_t* const B = (uint8_t*)0x10000000;
enum : uintptr_t {
    kObjSlot = 0xedaad60, kObj = 0xe7fff40, kPlayerSlot = 0x1c4a0d4, kYaw = 0x1c4a168, kYawCopy = 0x1c4a16a, kChannel = 0x1c4a409,
    kSpecial = 0x1c4a41d, kMapSlot = 0xe77dc2c, kMapLow = 0x18e469c, kTraceFn = 0x8d0f10, kGroundFn = 0x8202a0, kIat = 0x15d242c,
};
static uint8_t g_actor[0x200];
static uint32_t g_grid[13], g_cells[100 * 100], g_walls[4] = {1};
extern "C" {
float g_groundOut, g_groundY = -5000;
int g_wallOn, g_wallAxis, g_traceCalls, g_groundCalls;
float g_wallPos;
int FakeTraceImpl(const float* s, const float* e, float radius, float* hit, unsigned mask) {
    g_traceCalls++;
    if (!(mask & 0x8009) || !g_wallOn) return 0;
    double a = s[g_wallAxis] - g_wallPos, b = e[g_wallAxis] - g_wallPos;
    if ((a < 0) == (b < 0)) return 0;
    double t = a / (a - b);
    for (int i = 0; i < 3; i++) hit[i] = (float)(s[i] + (e[i] - s[i]) * t);
    return 1;
}
void FakeGroundImpl(const float* p) {
    g_groundCalls++;
    g_groundOut = g_groundY;
}
}
// the game's calling conventions: the sweep takes its start in ECX and four stack arguments, the ground query its point
// in ESI and returns the height in XMM0
__attribute__((naked)) static void FakeTraceEntry() {
    __asm__ volatile("push 16(%esp)\n push 16(%esp)\n push 16(%esp)\n push 16(%esp)\n push %ecx\n call _FakeTraceImpl\n add $20, %esp\n ret");
}
__attribute__((naked)) static void FakeGroundEntry() {
    __asm__ volatile("push %esi\n call _FakeGroundImpl\n add $4, %esp\n movss _g_groundOut, %xmm0\n ret");
}
struct Pad { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
static Pad g_pad;
static bool g_padOn = true;
extern "C" __declspec(dllexport) DWORD WINAPI CameraTestPad(DWORD slot, Pad* p) {
    if (slot != 0 || !g_padOn) return ERROR_DEVICE_NOT_CONNECTED;
    g_pad.packet++;
    *p = g_pad;
    return ERROR_SUCCESS;
}

static bool Reserve(SIZE_T size) { return VirtualAlloc(B, size, MEM_RESERVE, PAGE_EXECUTE_READWRITE) == B; }
static bool Commit(uintptr_t rva, SIZE_T n) { return VirtualAlloc(B + rva, n, MEM_COMMIT, PAGE_EXECUTE_READWRITE) != nullptr; }
static void Jump(uintptr_t rva, void* to) {
    B[rva] = 0xE9;
    *(int32_t*)(B + rva + 1) = (int32_t)((uint8_t*)to - (B + rva + 5));
}
// MezeLounge's mhfo-hd.dll's sections, copied to where the game would have them (plain data, never run).
static bool MapGameDll(const char* path, int flipRva) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* d = (uint8_t*)malloc(n);
    bool ok = fread(d, 1, n, f) == (size_t)n;
    fclose(f);
    if (!ok) return false;
    auto nt = (IMAGE_NT_HEADERS32*)(d + ((IMAGE_DOS_HEADER*)d)->e_lfanew);
    if (nt->OptionalHeader.ImageBase != 0x10000000 || !Reserve(nt->OptionalHeader.SizeOfImage)) return false;
    Commit(0, nt->OptionalHeader.SizeOfHeaders);
    memcpy(B, d, nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD size = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (!Commit(sec[i].VirtualAddress, size)) return false;
        memcpy(B + sec[i].VirtualAddress, d + sec[i].PointerToRawData, std::min(size, sec[i].SizeOfRawData));
    }
    free(d);
    if (flipRva) B[flipRva] ^= 0x01;
    return true;
}
static bool StandIn() {
    if (!Reserve(0x0F000000)) return false;
    static const uintptr_t pages[] = {0, kObjSlot, kObj, kPlayerSlot, kYaw, kChannel, kSpecial, kMapSlot, kMapLow, kTraceFn, kGroundFn, kIat};
    for (uintptr_t p : pages)
        if (!Commit(p & ~0xFFFu, 0x2000)) return false;
    Jump(kTraceFn, (void*)FakeTraceEntry);
    Jump(kGroundFn, (void*)FakeGroundEntry);
    return true;
}
// what both layouts get: the camera object, the hunter, the collision grid, the controls
static void GameState(void* lookAt) {
    *(uint32_t*)(B + kObjSlot) = (uint32_t)(uintptr_t)(B + kObj);
    *(uint32_t*)(B + kPlayerSlot) = (uint32_t)(uintptr_t)g_actor;
    float pos[3] = {5000, 100, 5000};
    memcpy(g_actor + 0xac, pos, sizeof pos);
    *(int16_t*)(g_actor + 0xa4) = 0;
    *(uint16_t*)(B + kYaw) = *(uint16_t*)(B + kYawCopy) = 0x1234;
    B[kChannel] = 0;
    B[kSpecial] = 0xFF;
    g_grid[2] = g_grid[3] = g_grid[4] = g_grid[5] = 100;
    g_grid[6] = (uint32_t)(uintptr_t)g_walls;
    g_grid[8] = g_grid[9] = g_grid[10] = g_grid[11] = 100;
    g_grid[12] = (uint32_t)(uintptr_t)g_cells;
    for (auto& c : g_cells) c = 1;
    *(uint32_t*)(B + kMapSlot) = (uint32_t)(uintptr_t)g_grid;
    *(float*)(B + kMapLow) = 0;
    *(void**)(B + kIat) = lookAt;
}

// ------------------------------------------------------------------------------------------------ the game's camera
typedef float*(WINAPI* LookAt_t)(float*, const float*, const float*, const float*);
static LookAt_t g_lookAt;
static float g_out[16];
static const float kTarget[3] = {5000, 220, 5000}, kEye[3] = {5300, 270, 5265}, kUp[3] = {0, 1, 0};
static float* Obj(int off) { return (float*)(B + kObj + off); }
static void GameCamera() {  // (the game puts its own camera into the object every tick)
    memcpy(Obj(0x80), kEye, 12);
    memcpy(Obj(0x8c), kTarget, 12);
    memcpy(Obj(0x98), kUp, 12);
}
static double g_callMs, g_callMax;  // the camera call's time (what the game's tick pays)
static int g_callN;
static void Tick() {
    GameCamera();
    LARGE_INTEGER a, b, q;
    QueryPerformanceCounter(&a);
    g_lookAt(g_out, Obj(0x80), Obj(0x8c), Obj(0x98));
    QueryPerformanceCounter(&b);
    QueryPerformanceFrequency(&q);
    double ms = (b.QuadPart - a.QuadPart) * 1000.0 / q.QuadPart;
    g_callMs += ms;
    g_callMax = std::max(g_callMax, ms);
    g_callN++;
}
static void Run(int ms) {
    for (int t = 0; t < ms; t += 33) {
        Tick();
        Sleep(33);
    }
}
static double Dist(const float* a, const float* b) {
    double s = 0;
    for (int i = 0; i < 3; i++) s += (a[i] - b[i]) * (double)(a[i] - b[i]);
    return sqrt(s);
}
static double Yaw() { return atan2(Obj(0x8c)[0] - Obj(0x80)[0], Obj(0x8c)[2] - Obj(0x80)[2]); }  // where the view looks
static double Elevation() { return asin((Obj(0x80)[1] - Obj(0x8c)[1]) / Dist(Obj(0x80), Obj(0x8c))) / kOrbitRad; }  // the camera above
static bool IsGameCamera() { return !memcmp(Obj(0x80), kEye, 12) && !memcmp(Obj(0x8c), kTarget, 12); }
// D3DXMatrixLookAtRH's own math, to check the matrices
static void LookAtRH(float* m, const float* eye, const float* at, const float* up) {
    double z[3], x[3], y[3], l;
    for (int i = 0; i < 3; i++) z[i] = eye[i] - at[i];
    l = sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (double& v : z) v /= l;
    x[0] = up[1] * z[2] - up[2] * z[1], x[1] = up[2] * z[0] - up[0] * z[2], x[2] = up[0] * z[1] - up[1] * z[0];
    l = sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (double& v : x) v /= l;
    y[0] = z[1] * x[2] - z[2] * x[1], y[1] = z[2] * x[0] - z[0] * x[2], y[2] = z[0] * x[1] - z[1] * x[0];
    double r[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                    -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
                    -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
    for (int i = 0; i < 16; i++) m[i] = (float)r[i];
}
static double MatrixError(const float* a, const float* b) {
    double e = 0;
    for (int i = 0; i < 16; i++) e = std::max(e, (double)fabs(a[i] - b[i]) / std::max(1.0, (double)fabs(b[i])));
    return e;
}
static char g_logPath[MAX_PATH];
static bool LogHas(const char* what) {
    FILE* f = fopen(g_logPath, "rb");
    if (!f) return false;
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, what) != nullptr;
}

// ------------------------------------------------------------------------------------------------ the cases
static int Case(const char* name, const char* dir, const char* gameDll) {
    printf("%s\n", name);
    timeBeginPeriod(1);  // (ticks 33 ms apart, as the game's)
    snprintf(g_logPath, sizeof g_logPath, "%s\\mhfz-fps.log", dir);
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    g_lookAt = dx ? (LookAt_t)(void*)GetProcAddress(dx, "D3DXMatrixLookAtRH") : nullptr;
    if (!g_lookAt) { printf("  (no d3dx9_43.dll here - skipped)\n"); return 0; }
    char dll[MAX_PATH];
    snprintf(dll, sizeof dll, "%s\\d3d9.dll", dir);
    uint8_t before[8];
    memcpy(before, (void*)g_lookAt, 8);
    if (!strcmp(name, "late")) {  // (a device before the game DLL is there: not final, looked for again while playing)
        HMODULE mod = LoadLibraryA(dll);
        Check(mod != nullptr, "mod DLL loaded");
        Check(!memcmp(before, (void*)g_lookAt, 8), "no game DLL yet: nothing hooked");
        Check(LogHas("looking again"), "and the camera looks again later");
        return g_fail;
    }
    bool real = !strcmp(name, "real") || !strcmp(name, "mismatch");
    bool ok = real ? MapGameDll(gameDll, !strcmp(name, "mismatch") ? 0xbaee10 + 10 : 0) : StandIn();
    Check(ok, "stand-in game at 0x10000000");
    if (!ok) return g_fail;
    GameState((void*)g_lookAt);
    HMODULE mod = LoadLibraryA(dll);
    Check(mod != nullptr, "mod DLL loaded");
    if (!mod) return g_fail;
    bool hooked = memcmp(before, (void*)g_lookAt, 8) != 0;

    if (!strcmp(name, "off")) {
        Check(!hooked, "camera=0: D3DXMatrixLookAtRH is left alone");
        Run(800);
        Check(IsGameCamera(), "camera=0: the game's camera is not touched");
        Check(!LogHas("camera: ready"), "camera=0: nothing set up");
        return g_fail;
    }
    if (!strcmp(name, "mismatch")) {
        Check(!hooked, "one changed byte in the game's code: nothing hooked");
        Check(LogHas("code check 1"), "the log names the code check");
        return g_fail;
    }
    if (!strcmp(name, "real")) {
        Check(hooked, "MezeLounge's mhfo-hd.dll passes the code checks: hooked");
        Check(LogHas("camera: ready"), "the log says ready");
        B[kChannel] = 1;  // (a non-gameplay camera: the collision - the game's real code - is never asked)
        Run(200);
        Check(IsGameCamera(), "another camera channel: the game's camera is not touched");
        return g_fail;
    }

    // ---- "orbit": the stand-in game
    Check(hooked, "D3DXMatrixLookAtRH hooked (%s)", LogHas("hot-patch hook") ? "hot-patch" : "trampoline");
    Check(LogHas("camera: ready"), "the log says ready");
    Tick();
    Check(IsGameCamera(), "while the scene settles the game's camera stays");
    Run(700);
    double r0 = Dist(kEye, kTarget);
    Check(Dist(Obj(0x80), kEye) < 0.5 && Dist(Obj(0x8c), kTarget) < 0.5, "after the resync the orbit starts where the game's camera is (%.2f)",
          Dist(Obj(0x80), kEye));
    uint16_t want = OrbitGameYaw(atan2(kTarget[0] - kEye[0], kTarget[2] - kEye[2]));
    Check(abs((int16_t)(*(uint16_t*)(B + kYaw) - want)) <= 2 && *(uint16_t*)(B + kYawCopy) == *(uint16_t*)(B + kYaw),
          "the direction the hunter moves by is the camera's (%04x, want %04x)", *(uint16_t*)(B + kYaw), want);
    Check(LogHas("degrees from its camera"), "the first resync logs the game's direction field against its camera");

    double y0 = Yaw();
    g_pad.rx = 32767;
    Run(500);
    g_pad.rx = 0;
    double turned = fabs(OrbitWrap(Yaw() - y0)) / kOrbitRad;
    Check(turned > 85 && turned < 140, "half a second of full right stick turns the camera about 125 degrees (%.0f)", turned);
    Check(fabs(Dist(Obj(0x80), Obj(0x8c)) - r0) < 1, "the distance stays the game's (%.1f / %.1f)", Dist(Obj(0x80), Obj(0x8c)), r0);
    want = OrbitGameYaw(Yaw());
    Check(abs((int16_t)(*(uint16_t*)(B + kYaw) - want)) <= 4, "the hunter's direction follows the turn (%04x, want %04x)", *(uint16_t*)(B + kYaw), want);
    float m[16];
    LookAtRH(m, Obj(0x80), Obj(0x8c), Obj(0x98));
    Check(MatrixError(g_out, m) < 1e-4, "the game's view matrix is built from the orbit camera (error %.1e)", MatrixError(g_out, m));

    g_pad.ry = -32768;
    Run(2000);
    Check(fabs(Elevation() - 65) < 1.5, "stick down: the camera rises to the look-down limit (%.1f degrees)", Elevation());
    g_pad.ry = 32767;
    Run(3000);
    g_pad.ry = 0;
    Check(fabs(Elevation() + 35) < 1.5, "stick up: it sinks to the look-up limit (%.1f degrees)", Elevation());
    g_groundY = 100;  // (the ground at the hunter's feet: the camera below the pivot would be in it)
    Run(300);
    Check(Obj(0x80)[1] >= 100 + 32 - 1 && Dist(Obj(0x80), Obj(0x8c)) < r0 - 50, "the ground keeps the camera above it (height %.1f, distance %.0f)",
          Obj(0x80)[1], Dist(Obj(0x80), Obj(0x8c)));
    Check(g_groundCalls > 0, "the game's ground query was asked (%d times)", g_groundCalls);
    g_groundY = -5000;

    *(int16_t*)(g_actor + 0xa4) = 0x4000;  // (the hunter faces a quarter turn round)
    g_pad.buttons = 0x40;
    Run(66);
    g_pad.buttons = 0;
    Run(1000);
    Check(fabs(OrbitWrap(Yaw() - kOrbitTau / 4)) / kOrbitRad < 3 && fabs(Elevation() - 15) < 1.5,
          "L3 recenters behind the hunter at the reset tilt (yaw %.1f, tilt %.1f)", Yaw() / kOrbitRad, Elevation());

    g_wallOn = 1;  // a wall between the hunter and the camera, 200 units behind the pivot
    g_wallAxis = 0;
    g_wallPos = kTarget[0] - 200;
    Tick();
    double d = Dist(Obj(0x80), Obj(0x8c));
    Check(d > 190 && d < 210, "a wall: the camera comes in front of it at once (distance %.1f)", d);
    Check(g_traceCalls > 0, "the game's geometry sweep was asked (%d times)", g_traceCalls);
    g_wallOn = 0;
    Sleep(33);
    Tick();
    double d1 = Dist(Obj(0x80), Obj(0x8c));
    Sleep(33);
    Run(1500);
    double d2 = Dist(Obj(0x80), Obj(0x8c));
    Check(d1 > d && d1 < 300 && d2 > r0 - 5, "wall gone: the distance grows back smoothly (%.0f, then %.0f)", d1, d2);

    Sleep(33);
    Tick();
    float e1[3];
    memcpy(e1, Obj(0x80), 12);
    Tick();  // (the same tick again: the game blending two cameras)
    Check(!memcmp(e1, Obj(0x80), 12), "a second call in the same tick gets the same view");
    float oe[3] = {1, 2, 3}, oa[3] = {4, 5, 9}, ou[3] = {0, 1, 0}, om[16];
    g_lookAt(g_out, oe, oa, ou);
    LookAtRH(om, oe, oa, ou);
    Check(oe[0] == 1 && oa[2] == 9 && MatrixError(g_out, om) < 1e-5, "other views (shadows, menus) are left alone");

    B[kChannel] = 1;
    *(uint16_t*)(B + kYaw) = 0x4321;
    Sleep(33);
    Tick();
    Check(IsGameCamera() && *(uint16_t*)(B + kYaw) == 0x4321, "another camera channel: nothing written");
    B[kChannel] = 0;
    B[kSpecial] = 4;
    Sleep(33);
    Tick();
    Check(IsGameCamera(), "a special camera (cutscene): nothing written");
    B[kSpecial] = 0xFF;
    Run(800);
    Check(!IsGameCamera(), "back in the gameplay camera: the orbit camera again");

    g_padOn = false;
    Run(200);
    Check(IsGameCamera(), "no controller: the game's own camera (keyboard and mouse turn it)");
    g_padOn = true;
    Run(2000);  // (a missing controller is asked for again once a second, then the scene settles)
    Check(!IsGameCamera(), "controller back: the orbit camera again");

    *(uint32_t*)(B + kIat) = 0x12345678;
    Sleep(33);
    Tick();
    Check(IsGameCamera() && LogHas("another program took"), "another mod takes the game's camera call: this one stands down");
    *(void**)(B + kIat) = (void*)g_lookAt;
    Run(800);
    Check(!IsGameCamera(), "and comes back when it's gone");
    // (the stand-in queries cost nothing: this is the camera's own cost, the game's real queries come on top)
    Check(g_callMs / g_callN < 0.5, "the camera call costs the game's tick %.3f ms (longest %.3f, %d calls)", g_callMs / g_callN, g_callMax, g_callN);
    return g_fail;
}

// ------------------------------------------------------------------------------------------------ main
static int Child(const char* exe, const char* name, const char* modDir, const char* gameDll, int test) {
    char dir[MAX_PATH], tmp[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    snprintf(dir, sizeof dir, "%smhfz-cameratest-%lu-%s", tmp, GetCurrentProcessId(), name);
    CreateDirectoryA(dir, nullptr);
    snprintf(src, sizeof src, "%s\\d3d9.dll", modDir);
    snprintf(dst, sizeof dst, "%s\\d3d9.dll", dir);
    if (!CopyFileA(src, dst, FALSE)) { printf("%s: cannot copy %s\n", name, src); return 1; }
    snprintf(dst, sizeof dst, "%s\\mhfz-fps.ini", dir);
    FILE* f = fopen(dst, "w");
    fprintf(f, "[fps]\ncamera=%d\ncamera_test=%d\n", strcmp(name, "off") ? 1 : 0, test);
    fclose(f);
    char cmd[MAX_PATH * 4];
    snprintf(cmd, sizeof cmd, "\"%s\" --case %s \"%s\" \"%s\"", exe, name, dir, gameDll ? gameDll : "");
    STARTUPINFOA si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, 0, nullptr, dir, &si, &pi)) { printf("%s: cannot start\n", name); return 1; }
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 99);
        printf("  FAIL %s: timed out\n", name);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code > 100) printf("  FAIL %s: ended with %#lx (see %s\\mhfz-fps.log)\n", name, code, dir);
    return code > 100 ? 1 : (int)code;
}

int main(int argc, char** argv) {
    if (argc >= 4 && !strcmp(argv[1], "--case")) return Case(argv[2], argv[3], argc > 4 ? argv[4] : "");
    setvbuf(stdout, nullptr, _IONBF, 0);
    char exe[MAX_PATH], modDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    strcpy(modDir, exe);
    if (char* s = strrchr(modDir, '\\')) *s = 0;
    if (argc > 1 && argv[1][0]) strcpy(modDir, argv[1]);
    const char* gameDll = argc > 2 ? argv[2] : nullptr;
    MathTests();
    int fails = g_fail;
    fails += Child(exe, "orbit", modDir, nullptr, 1);
    fails += Child(exe, "off", modDir, nullptr, 1);
    fails += Child(exe, "late", modDir, nullptr, 1);
    if (gameDll && GetFileAttributesA(gameDll) != INVALID_FILE_ATTRIBUTES) {
        fails += Child(exe, "real", modDir, gameDll, 2);
        fails += Child(exe, "mismatch", modDir, gameDll, 2);
    } else printf("(no mhfo-hd.dll given: the code checks against the real game are not run)\n");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails;
}
