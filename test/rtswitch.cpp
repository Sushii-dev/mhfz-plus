// Render-target switching cost: MHF draws every shadow caster into its 3 cascades one after another (RT A, B, C,
// A, B, C... one draw per switch), ~1200-4800 switches a frame. This times the same draws interleaved like the game
// and grouped by target, with Windows' d3d9 or another d3d9.dll (DXVK).
//   rtswitch.exe [d3d9.dll path|-] [objects] [size]
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

static LARGE_INTEGER qpf;
static double Now() { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x.QuadPart * 1000.0 / qpf.QuadPart; }

struct V { float x, y, z, rhw; DWORD c; };

int main(int argc, char** argv) {
    QueryPerformanceFrequency(&qpf);
    const char* dll = argc > 1 && strcmp(argv[1], "-") ? argv[1] : nullptr;
    int objects = argc > 2 ? atoi(argv[2]) : 1200;
    int size = argc > 3 ? atoi(argv[3]) : 3440;
    SetEnvironmentVariableA("DXVK_LOG_LEVEL", "none");
    SetEnvironmentVariableA("DXVK_LOG_PATH", "none");
    HMODULE m = dll ? LoadLibraryA(dll) : LoadLibraryA("d3d9.dll");
    if (!m) { printf("cannot load %s\n", dll ? dll : "d3d9.dll"); return 1; }
    auto create = (IDirect3D9*(WINAPI*)(UINT))GetProcAddress(m, "Direct3DCreate9");
    IDirect3D9* d3d = create(D3D_SDK_VERSION);
    HWND w = CreateWindowA("STATIC", "rtswitch", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, 0, 0, 0, 0);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.hDeviceWindow = w;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) { printf("no device\n"); return 1; }
    const int T = 3;
    IDirect3DTexture9* tex[T];
    IDirect3DSurface9 *rt[T], *ds[T], *bb;
    for (int i = 0; i < T; i++) {
        if (FAILED(dev->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G32R32F, D3DPOOL_DEFAULT, &tex[i], nullptr))) { printf("no rt\n"); return 1; }
        tex[i]->GetSurfaceLevel(0, &rt[i]);
        if (FAILED(dev->CreateDepthStencilSurface(size, size, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds[i], nullptr))) { printf("no ds\n"); return 1; }
    }
    dev->GetRenderTarget(0, &bb);
    // one small mesh per object: 64 triangles in a ~60 px patch
    const int triPerObj = 64;
    IDirect3DVertexBuffer9* vb;
    dev->CreateVertexBuffer(objects * triPerObj * 3 * sizeof(V), D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &vb, nullptr);
    V* v;
    vb->Lock(0, 0, (void**)&v, 0);
    srand(1);
    for (int o = 0; o < objects; o++) {
        float cx = (float)(rand() % (size - 80) + 40), cy = (float)(rand() % (size - 80) + 40);
        for (int t = 0; t < triPerObj; t++)
            for (int k = 0; k < 3; k++)
                *v++ = {cx + (rand() % 60 - 30), cy + (rand() % 60 - 30), (rand() % 1000) / 1000.0f, 1.0f, 0xFF808080};
    }
    vb->Unlock();
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetStreamSource(0, vb, 0, sizeof(V));
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    IDirect3DQuery9 *dis, *t0, *t1, *fq, *ev;
    dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &dis);
    dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &t0);
    dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &t1);
    dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &fq);
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev);
    D3DVIEWPORT9 vp = {0, 0, (DWORD)size, (DWORD)size, 0, 1};
    auto Bind = [&](int i) {
        dev->SetRenderTarget(0, rt[i]);
        dev->SetDepthStencilSurface(ds[i]);
        dev->SetViewport(&vp);
    };
    const char* names[] = {"interleaved (like MHF)", "grouped by target", "interleaved, same target"};
    for (int mode = 0; mode < 3; mode++) {
        double cpu = 0, gpu = 0, wall = 0;
        int n = 0;
        for (int f = 0; f < 60; f++) {
            double a = Now();
            dev->BeginScene();
            for (int i = 0; i < T; i++) { Bind(i); dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFFFFFFFF, 1, 0); }
            dis->Issue(D3DISSUE_BEGIN);
            t0->Issue(D3DISSUE_END);
            if (mode == 1) {
                for (int i = 0; i < T; i++) {
                    Bind(i);
                    for (int o = 0; o < objects; o++) dev->DrawPrimitive(D3DPT_TRIANGLELIST, o * triPerObj * 3, triPerObj);
                }
            } else {
                for (int o = 0; o < objects; o++)
                    for (int i = 0; i < T; i++) {
                        Bind(mode == 2 ? 0 : i);
                        dev->DrawPrimitive(D3DPT_TRIANGLELIST, o * triPerObj * 3, triPerObj);
                    }
            }
            t1->Issue(D3DISSUE_END);
            fq->Issue(D3DISSUE_END);
            dis->Issue(D3DISSUE_END);
            dev->SetRenderTarget(0, bb);
            dev->SetDepthStencilSurface(nullptr);
            dev->EndScene();
            double b = Now();
            dev->Present(nullptr, nullptr, nullptr, nullptr);
            ev->Issue(D3DISSUE_END);
            while (ev->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {}
            double c = Now();
            UINT64 x = 0, y = 0, fr = 0;
            BOOL dj = TRUE;
            while (dis->GetData(&dj, sizeof dj, D3DGETDATA_FLUSH) == S_FALSE) {}
            t0->GetData(&x, 8, D3DGETDATA_FLUSH);
            t1->GetData(&y, 8, D3DGETDATA_FLUSH);
            fq->GetData(&fr, 8, D3DGETDATA_FLUSH);
            if (f < 10 || dj || !fr) continue;  // warm-up
            cpu += b - a;
            gpu += (double)(y - x) * 1000.0 / (double)fr;
            wall += c - a;
            n++;
        }
        printf("%-26s %5d objects x %d targets: CPU submit %6.2f ms, GPU %6.2f ms, frame wall %6.2f ms\n", names[mode], objects, T,
               cpu / n, gpu / n, wall / n);
    }
    return 0;
}
