// Rain's pattern: d3d9.dll loaded, used and freed, other DLLs loaded, then an ordinary exception (thrown and caught).
// A d3d9 proxy whose exception handlers outlive its unloading crashes here; the pinned one survives.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <initializer_list>

static LONG CALLBACK Handled(EXCEPTION_POINTERS* e) {
    return e->ExceptionRecord->ExceptionCode == 0xE0000001 ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

int main() {
    HMODULE d = LoadLibraryA("d3d9.dll");
    if (!d) { printf("no d3d9\n"); return 2; }
    auto create = (IDirect3D9 * (WINAPI*)(UINT))GetProcAddress(d, "Direct3DCreate9");
    if (IDirect3D9* x = create ? create(D3D_SDK_VERSION) : nullptr) x->Release();
    char path[MAX_PATH];
    GetModuleFileNameA(d, path, MAX_PATH);
    FreeLibrary(d);
    printf("after FreeLibrary: %s %s\n", path, GetModuleHandleA(path) ? "still loaded" : "unloaded");
    // fill the address range the proxy had
    for (const char* n : {"winmm.dll", "dinput8.dll", "dsound.dll", "xinput1_3.dll", "msacm32.dll", "avifil32.dll"}) LoadLibraryA(n);
    // a Windows exception, handled by a handler that runs after any the proxy left behind
    AddVectoredExceptionHandler(0, Handled);
    RaiseException(0xE0000001, 0, 0, nullptr);
    printf("exception handled: survived\n");
    return 0;
}
