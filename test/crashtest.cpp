// Checks the mod's crash note: creates a device through build\d3d9.dll, then passes a bad pointer to a hooked
// call so the mod itself faults. Expected: "EXCEPTION ... in the mod" in mhfz-fps.log, then the process dies.
#include <windows.h>
#include <d3d9.h>

int main() {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "crashtest";
    RegisterClassA(&wc);
    HWND w = CreateWindowA("crashtest", "crashtest", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return 2;
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = w;
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) return 3;
    dev->BeginScene();
    dev->SetVertexShaderConstantF(0, (const float*)(uintptr_t)0x10, 4);  // the mod records the constants: reads 0x10
    dev->EndScene();
    dev->Present(nullptr, nullptr, nullptr, nullptr);
    return 0;
}
