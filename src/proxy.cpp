// d3d9.dll proxy: forwards every export of the real d3d9.dll; Direct3DCreate9 is intercepted by fps.cpp.
// The real one is Windows' own, or another implementation set by d3d9_backend in mhfz-fps.ini (DXVK: Direct3D 9
// on Vulkan, much less driver work per draw); exports that one lacks come from Windows' own.
#include <windows.h>

extern "C" {
void* g_real[32];
HMODULE g_realD3D9 = nullptr;
char g_d3d9Backend[MAX_PATH];  // full path, set by fps.cpp before the first Direct3DCreate9 (empty: Windows' own)
}
static HMODULE g_sysD3D9;

static const char* kNames[] = {
    "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame",
    "D3DPERF_SetMarker", "D3DPERF_SetOptions", "D3DPERF_SetRegion", "DebugSetLevel", "DebugSetMute",
    "Direct3D9EnableMaximizedWindowedModeShim", "Direct3DCreate9", "Direct3DCreate9Ex",
    "Direct3DCreate9On12", "Direct3DCreate9On12Ex", "Direct3DShaderValidatorCreate9", "PSGPError",
    "PSGPSampleTexture",
    // ordinal-only exports follow (index 17..22 -> ordinals 16,17,18,19,22,23)
};
static const WORD kOrdinals[] = {16, 17, 18, 19, 22, 23};

static HMODULE SystemD3D9() {
    if (!g_sysD3D9) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);  // WoW64 redirects System32 -> SysWOW64 for this 32-bit process
        lstrcatA(path, "\\d3d9.dll");
        g_sysD3D9 = LoadLibraryA(path);
    }
    return g_sysD3D9;
}
static void* Export(HMODULE m, int i) {
    return m ? (void*)GetProcAddress(m, i < 17 ? kNames[i] : (LPCSTR)(ULONG_PTR)kOrdinals[i - 17]) : nullptr;
}

extern "C" void LoadRealD3D9() {
    if (g_realD3D9) return;
    if (g_d3d9Backend[0]) g_realD3D9 = LoadLibraryA(g_d3d9Backend);
    if (!g_realD3D9) g_realD3D9 = SystemD3D9();
    if (!g_realD3D9) return;
    for (int i = 0; i < 23; i++) g_real[i] = Export(g_realD3D9, i);
}

// Each stub loads the real d3d9 on first use and tail-jumps to the real export (one the backend lacks: Windows'
// own, loaded only then).
extern "C" void* __stdcall ResolveReal(int i) {
    LoadRealD3D9();
    if (!g_real[i]) g_real[i] = Export(SystemD3D9(), i);
    return g_real[i];
}

#define STUB(name, idx)                                  \
    __asm__(".globl _Proxy_" #name "\n"                 \
            "_Proxy_" #name ":\n"                       \
            "  pushl $" #idx "\n"                       \
            "  call _ResolveReal@4\n"                   \
            "  jmp *%eax\n");

STUB(D3DPERF_BeginEvent, 0)
STUB(D3DPERF_EndEvent, 1)
STUB(D3DPERF_GetStatus, 2)
STUB(D3DPERF_QueryRepeatFrame, 3)
STUB(D3DPERF_SetMarker, 4)
STUB(D3DPERF_SetOptions, 5)
STUB(D3DPERF_SetRegion, 6)
STUB(DebugSetLevel, 7)
STUB(DebugSetMute, 8)
STUB(Direct3D9EnableMaximizedWindowedModeShim, 9)
// 10 = Direct3DCreate9 -> implemented in fps.cpp
STUB(Direct3DCreate9Ex, 11)
STUB(Direct3DCreate9On12, 12)
STUB(Direct3DCreate9On12Ex, 13)
STUB(Direct3DShaderValidatorCreate9, 14)
STUB(PSGPError, 15)
STUB(PSGPSampleTexture, 16)
STUB(Ord16, 17)
STUB(Ord17, 18)
STUB(Ord18, 19)
STUB(Ord19, 20)
STUB(Ord22, 21)
STUB(Ord23, 22)
