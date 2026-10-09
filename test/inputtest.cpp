// input_latch test: a game-like loop (draw, Sleep-based ~30 Hz limiter, Present) that reads a DirectInput keyboard once
// per tick - as MHF does - while another thread types short F13 taps with SendInput. Prints how many taps the loop saw.
//   inputtest.exe [seconds=8] [hold ms=12] [gap ms=90]
// F13 goes to whatever window is in front; nothing uses it.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <d3d9.h>
#include <dinput.h>
#include <cstdio>
#include <cstdlib>

static volatile LONG g_taps, g_stop;
static int g_hold = 12, g_gap = 90;

static void Key(bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = 0x64;  // F13 (DIK_F13)
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof in);
}
static DWORD WINAPI Typist(void*) {
    Sleep(1500);  // (let the loop and the mod settle)
    srand(7);
    while (!g_stop) {
        Key(true);
        Sleep(g_hold);
        Key(false);
        InterlockedIncrement(&g_taps);
        Sleep(g_gap + rand() % 40);
    }
    return 0;
}
static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 8;
    if (argc > 2) g_hold = atoi(argv[2]);
    if (argc > 3) g_gap = atoi(argv[3]);
    WNDCLASSA wc{};
    wc.lpfnWndProc = Wnd;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "mhfzinputtest";
    RegisterClassA(&wc);
    HWND hw = CreateWindowA("mhfzinputtest", "mhfz-fps input test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 480, 0, 0, wc.hInstance, 0);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.hDeviceWindow = hw;
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, hw, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) { printf("CreateDevice failed\n"); return 1; }

    IDirectInput8A* di = nullptr;
    IDirectInputDevice8A* kb = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&di, nullptr)) ||
        FAILED(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr)) || FAILED(kb->SetDataFormat(&c_dfDIKeyboard)) ||
        FAILED(kb->SetCooperativeLevel(hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) { printf("DirectInput failed\n"); return 1; }
    kb->Acquire();

    struct V { float x, y, z, rhw; DWORD c; };
    HANDLE th = CreateThread(nullptr, 0, Typist, nullptr, 0, nullptr);
    DWORD start = timeGetTime(), last = start;
    int seen = 0, ticks = 0;
    bool was = false;
    MSG msg;
    while (timeGetTime() - start < (DWORD)seconds * 1000) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        BYTE keys[256] = {};
        if (FAILED(kb->GetDeviceState(sizeof keys, keys))) kb->Acquire();
        bool down = (keys[DIK_F13] & 0x80) != 0;
        if (down && !was) seen++;
        was = down;
        ticks++;
        // a frame with one moving quad, so the mod has something to interpolate
        float x = 100.0f + (float)((ticks * 7) % 400);
        V q[4] = {{x, 100, 0.5f, 1, 0xff40a0ff}, {x + 80, 100, 0.5f, 1, 0xff40a0ff}, {x, 180, 0.5f, 1, 0xff40a0ff}, {x + 80, 180, 0.5f, 1, 0xff40a0ff}};
        dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff202020, 1.0f, 0);
        dev->BeginScene();
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        dev->EndScene();
        // MHF's limiter: Sleep until 33 ms after the last tick
        DWORD now = timeGetTime();
        int wait = (int)(last + 33 - now);
        if (wait > 0) Sleep(wait);
        last = timeGetTime();
        dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    g_stop = 1;
    WaitForSingleObject(th, 2000);
    RECT wr{};
    GetWindowRect(hw, &wr);  // (where the window ended up: off-screen windows get their presents throttled)
    printf("ticks %d, taps %ld, seen %d (%.0f%%), hold %d ms, window %ld,%ld %ldx%ld%s\n", ticks, g_taps, seen, g_taps ? 100.0 * seen / g_taps : 0.0,
           g_hold, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, IsIconic(hw) ? " minimized" : "");
    kb->Unacquire();
    kb->Release();
    di->Release();
    dev->Release();
    d3d->Release();
    return 0;
}
