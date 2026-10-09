// An "overlay" that hooks IDirect3DDevice9::Present in the device vtable after the mod did (saving the mod's hook as
// its original, like vtable-hooking overlays do). The mod re-hooks on top of it at the next limiter Sleep; without
// the re-entry guard the two hooks then call each other until the stack overflows.
// Exit code 0 and "overlay calls: N" > 0 = fine.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
static Present_t g_saved;
static int g_calls;
static HRESULT WINAPI OverlayPresent(IDirect3DDevice9* d, const RECT* a, const RECT* b, HWND w, const RGNDATA* r) {
    g_calls++;
    return g_saved(d, a, b, w, r);
}

int main() {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "overlaytest";
    RegisterClassA(&wc);
    HWND w = CreateWindowA("overlaytest", "overlaytest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = w;
    IDirect3DDevice9* dev = nullptr;
    if (!d3d || FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) return 3;
    for (int f = 0; f < 90; f++) {
        if (f == 10) {  // the overlay arrives
            void** vt = *(void***)dev;
            DWORD old;
            VirtualProtect(&vt[17], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
            g_saved = (Present_t)vt[17];
            vt[17] = (void*)&OverlayPresent;
            VirtualProtect(&vt[17], sizeof(void*), old, &old);
        }
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(f, 40, 80), 1.0f, 0);
        dev->BeginScene();
        dev->EndScene();
        Sleep(20);  // the limiter: the mod checks its hooks here
        dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    printf("overlay calls: %d\n", g_calls);
    dev->Release();
    d3d->Release();
    return g_calls > 0 ? 0 : 1;
}
