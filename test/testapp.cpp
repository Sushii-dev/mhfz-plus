// Mimics MHF's frame structure: logic+draw -> Sleep-based 30 Hz limiter -> Present.
// Draws: a VS-constant-driven rotating triangle, a fixed-function SetTransform quad, and a moving DrawPrimitiveUP quad.
#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>

typedef HRESULT(WINAPI* AsmFn)(LPCSTR, UINT, const void*, void*, DWORD, void**, void**);
struct V { float x, y, z; DWORD c; };
struct VT { float x, y, z, rhw; DWORD c; };

static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 6;
    bool billboards = argc > 2 && atoi(argv[2]) != 0;
    bool swapTest = argc > 3 && atoi(argv[3]) != 0;
    bool tagTest = argc > 4 && atoi(argv[4]) != 0;
    int labelTest = argc > 5 ? atoi(argv[5]) : 0;  // 2: two letters also trade atlas slots every 7 ticks
    bool menuTest = argc > 6 && atoi(argv[6]) != 0;
    bool skipTest = argc > 7 && atoi(argv[7]) != 0;
    bool rampTest = argc > 8 && atoi(argv[8]) != 0;
    bool liveTest = argc > 9 && atoi(argv[9]) != 0;
    bool heavyTest = argc > 10 && atoi(argv[10]) != 0;
    WNDCLASSA wc{};
    wc.lpfnWndProc = Wnd;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "mhfzfpstest";
    RegisterClassA(&wc);
    HWND hw = CreateWindowA("mhfzfpstest", "mhfz-fps test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800, 600, 0, 0, wc.hInstance, 0);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    pp.hDeviceWindow = hw;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, hw, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) { printf("CreateDevice failed %08lx\n", hr); return 1; }

    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    AsmFn asmShader = dx ? (AsmFn)GetProcAddress(dx, "D3DXAssembleShader") : nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    if (asmShader) {
        const char* src = "vs_1_1\ndcl_position v0\ndcl_color v1\nm4x4 oPos, v0, c0\nmov oD0, v1\n";
        void* code = nullptr;
        if (SUCCEEDED(asmShader(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
            // ID3DXBuffer: vtbl[3] GetBufferPointer
            auto buf = (IUnknown*)code;
            auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            dev->CreateVertexShader((const DWORD*)getPtr(buf), &vs);
            buf->Release();
        }
    }
    printf("vertex shader: %p\n", (void*)vs);
    // skinned shader like MHF's: one bone from a palette read through the address register
    //   c0-c3 = view-projection, c4.x = bone index * 3, c8.. = bone palette (3 rows of [R|t] per bone)
    IDirect3DVertexShader9* vsSkin = nullptr;
    if (asmShader) {
        const char* src =
            "vs_2_0\ndef c5, 0, 0, 0, 1\ndcl_position v0\ndcl_color v1\nmova a0.x, c4.x\n"
            "dp4 r0.x, v0, c[a0.x + 8]\ndp4 r0.y, v0, c[a0.x + 9]\ndp4 r0.z, v0, c[a0.x + 10]\nmov r0.w, c5.w\n"
            "m4x4 oPos, r0, c0\nmov oD0, v1\n";
        void* code = nullptr;
        if (SUCCEEDED(asmShader(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
            auto buf = (IUnknown*)code;
            auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            dev->CreateVertexShader((const DWORD*)getPtr(buf), &vsSkin);
            buf->Release();
        }
    }
    printf("skinned vertex shader: %p\n", (void*)vsSkin);
    // colour from a constant (like a per-character light colour): oD0 = vertex colour * c4
    IDirect3DVertexShader9* vsTint = nullptr;
    if (asmShader) {
        const char* src = "vs_1_1\ndcl_position v0\ndcl_color v1\nm4x4 oPos, v0, c0\nmul oD0, v1, c4\n";
        void* code = nullptr;
        if (SUCCEEDED(asmShader(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
            auto buf = (IUnknown*)code;
            auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            dev->CreateVertexShader((const DWORD*)getPtr(buf), &vsTint);
            buf->Release();
        }
    }
    // world-view-projection in c10 (a camera context of its own for the culling-swap scene)
    IDirect3DVertexShader9* vs10 = nullptr;
    if (asmShader) {
        const char* src = "vs_1_1\ndcl_position v0\ndcl_color v1\nm4x4 oPos, v0, c10\nmov oD0, v1\n";
        void* code = nullptr;
        if (SUCCEEDED(asmShader(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
            auto buf = (IUnknown*)code;
            auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            dev->CreateVertexShader((const DWORD*)getPtr(buf), &vs10);
            buf->Release();
        }
    }
    // game-like material shaders for scene 15: WVP in c10, fog c3, light/material c20-c25; pixel shader reads c0-c7
    IDirect3DVertexShader9* vsHeavy = nullptr;
    IDirect3DPixelShader9* psHeavy = nullptr;
    if (asmShader && heavyTest) {
        const char* src = "vs_2_0\ndcl_position v0\ndcl_color v1\nm4x4 oPos, v0, c10\nmov r0, v1\nmad r0, r0, c20, c21\n"
                          "dp3 r1.x, c22, c23\nmad r0, r0, r1.x, c24\nmul r0, r0, c25\nmad oD0, r0, c3.x, c3.y\n";
        void* code = nullptr;
        if (SUCCEEDED(asmShader(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
            auto buf = (IUnknown*)code;
            auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            dev->CreateVertexShader((const DWORD*)getPtr(buf), &vsHeavy);
            buf->Release();
        }
        const char* psrc = "ps_2_0\ndcl v0\nmul r0, v0, c0\nmad r0, r0, c1, c2\nmad r0, r0, c3, c4\nmad r0, r0, c5, c6\n"
                           "add r0, r0, c7\nmov oC0, r0\n";
        code = nullptr;
        if (SUCCEEDED(asmShader(psrc, (UINT)strlen(psrc), nullptr, nullptr, 0, &code, nullptr)) && code) {
            auto buf = (IUnknown*)code;
            auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)buf)[3]);
            dev->CreatePixelShader((const DWORD*)getPtr(buf), &psHeavy);
            buf->Release();
        }
    }
    D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0}, {0, 12, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
    IDirect3DVertexDeclaration9* decl = nullptr;
    dev->CreateVertexDeclaration(el, &decl);

    // town-sized load (scene 15): 40 meshes in one vertex/index buffer, two shadow render targets
    const int kMeshes = 40;
    IDirect3DVertexBuffer9* hvb = nullptr;
    IDirect3DIndexBuffer9* hib = nullptr;
    IDirect3DSurface9 *shadowRT[2] = {}, *shadowDS = nullptr, *mainRT = nullptr, *mainDS = nullptr;
    int meshV[kMeshes], meshI[kMeshes], meshT[kMeshes];
    if (heavyTest) {
        int nv = 0, ni = 0;
        for (int m = 0; m < kMeshes; m++) { meshV[m] = nv; meshI[m] = ni; meshT[m] = 4 + m; nv += meshT[m] + 1; ni += meshT[m] * 3; }
        dev->CreateVertexBuffer(nv * sizeof(V), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &hvb, nullptr);
        dev->CreateIndexBuffer(ni * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &hib, nullptr);
        V* pv = nullptr;
        WORD* pi = nullptr;
        hvb->Lock(0, 0, (void**)&pv, 0);
        hib->Lock(0, 0, (void**)&pi, 0);
        for (int m = 0; m < kMeshes; m++) {
            DWORD col = 0xff000000 | ((m * 53 % 200 + 55) << 16) | ((m * 97 % 200 + 55) << 8) | (m * 31 % 200 + 55);
            V* v = pv + meshV[m];
            v[0] = {0, 0, 0, col};
            for (int t = 0; t < meshT[m]; t++) {
                float a0 = t * 6.2832f / meshT[m];
                v[1 + t] = {cosf(a0), sinf(a0) * (0.6f + 0.01f * m), 0.1f * (t & 1), col};
                WORD* ix = pi + meshI[m] + t * 3;
                ix[0] = 0; ix[1] = (WORD)(1 + t); ix[2] = (WORD)(1 + (t + 1) % meshT[m]);
            }
        }
        hvb->Unlock();
        hib->Unlock();
        for (int k = 0; k < 2; k++) {
            IDirect3DTexture9* tex = nullptr;
            dev->CreateTexture(512, 512, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
            if (tex) { tex->GetSurfaceLevel(0, &shadowRT[k]); tex->Release(); }
        }
        dev->CreateDepthStencilSurface(512, 512, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &shadowDS, nullptr);
        dev->GetRenderTarget(0, &mainRT);
        dev->GetDepthStencilSurface(&mainDS);
    }

    // MHF records a state block once at start-up; the runtime rewrites the device vtable while recording
    IDirect3DStateBlock9* sb = nullptr;
    dev->BeginStateBlock();
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->EndStateBlock(&sb);

    timeBeginPeriod(1);
    DWORD last = timeGetTime();
    DWORD start = last;
    int tick = 0;
    MSG msg;
    while (timeGetTime() - start < (DWORD)seconds * 1000) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        tick++;
        float a = tick * 0.15f;  // fixed step per tick, like the game
        dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(20, 20, 40), 1.0f, 0);
        dev->BeginScene();
        if (sb) sb->Apply();
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        // 15) town-sized load for timing: 720 static/moving props from 40 meshes (main pass + two shadow passes into
        //     their own render targets), 30 skinned characters with 20-bone palettes, 120 glyph quads of name tags.
        //     About 1300 draws per tick, like a busy MHF town.
        if (heavyTest && vs10 && vsSkin && hvb && hib) {
            auto mul = [](const float A[4][4], const float B[4][4], float R[4][4]) {
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { R[i][j] = 0; for (int k = 0; k < 4; k++) R[i][j] += A[i][k] * B[k][j]; }
            };
            float yaw = 0.35f * sinf(tick * 0.012f), cy = cosf(yaw), sy = sinf(yaw);  // sways, stays on the field
            float camX = 20.0f * sinf(tick * 0.01f), camZ = -60.0f;
            float Vw[4][4] = {{cy, 0, -sy, -(cy * camX - sy * camZ)}, {0, 1, 0, -6.0f}, {sy, 0, cy, -(sy * camX + cy * camZ)}, {0, 0, 0, 1}};
            float Pm[4][4] = {{1.0f, 0, 0, 0}, {0, 1.333f, 0, 0}, {0, 0, 1.0f, -1.0f}, {0, 0, 1, 0}};
            float PV[4][4];
            mul(Pm, Vw, PV);
            struct Obj { int mesh; float x, y, z, depth; };
            static Obj vis[720];
            int nvis = 0;
            static Obj all[720];
            for (int o = 0; o < 720; o++) {
                float x = (o % 30 - 15) * 9.0f, z = (o / 30) * 9.0f - 40.0f;
                if (o % 12 == 0) x += 4.0f * sinf(tick * 0.1f + o);  // some props move by themselves
                all[o] = {o % kMeshes, x, 0, z, 0};
                float vx = Vw[0][0] * x + Vw[0][2] * z + Vw[0][3], vz = Vw[2][0] * x + Vw[2][2] * z + Vw[2][3];
                if (vz > 1.0f && std::fabs(vx) < vz * 1.1f) { all[o].depth = vz; vis[nvis++] = all[o]; }
            }
            std::sort(vis, vis + nvis, [](const Obj& a, const Obj& b) { return a.depth < b.depth; });
            dev->SetVertexDeclaration(decl);
            dev->SetStreamSource(0, hvb, 0, sizeof(V));
            dev->SetIndices(hib);
            auto drawMesh = [&](int m) { dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, meshV[m], 0, meshT[m] + 1, meshI[m], meshT[m]); };
            // shadow cascades: fixed light, orthographic boxes of two sizes around the camera target
            for (int k = 0; k < 2 && shadowRT[k]; k++) {
                dev->SetRenderTarget(0, shadowRT[k]);
                dev->SetDepthStencilSurface(shadowDS);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffffffff, 1.0f, 0);
                float s = 1.0f / (60.0f * (k + 1));
                float L[4][4] = {{s, 0, 0, -camX * s}, {0, 0, s, 0}, {0, s * 0.5f, 0, 0.5f}, {0, 0, 0, 1}};
                dev->SetVertexShader(vs10);
                for (int o = 0; o < 720; o++) {
                    const Obj& b = all[o];
                    if (std::fabs(b.x - camX) * s > 1.0f || std::fabs(b.z) * s > 1.0f) continue;
                    float W[4][4] = {{2, 0, 0, b.x}, {0, 2, 0, 1.0f}, {0, 0, 2, b.z}, {0, 0, 0, 1}}, M[4][4];
                    mul(L, W, M);
                    dev->SetVertexShaderConstantF(10, &M[0][0], 4);
                    drawMesh(b.mesh);
                }
                dev->SetVertexShader(vsSkin);
                dev->SetVertexShaderConstantF(0, &L[0][0], 4);
                for (int c = 0; c < 30; c++) {
                    float idx[4] = {(float)((c % 20) * 3), 0, 0, 0};
                    dev->SetVertexShaderConstantF(4, idx, 1);
                    drawMesh(c % kMeshes);
                }
            }
            if (mainRT) { dev->SetRenderTarget(0, mainRT); dev->SetDepthStencilSurface(mainDS); }
            // main pass: per object like MHF, shaders and states set again, 5 vertex- and 8 pixel-constant uploads
            float fog[4] = {0.9f + 0.05f * sinf(tick * 0.02f), 0.1f, 0, 0};
            float light[4] = {cosf(tick * 0.01f), 0.7f, sinf(tick * 0.01f), 0};
            for (int i = 0; i < nvis; i++) {
                const Obj& b = vis[i];
                float W[4][4] = {{2, 0, 0, b.x}, {0, 2, 0, 1.0f}, {0, 0, 2, b.z}, {0, 0, 0, 1}}, M[4][4];
                mul(PV, W, M);
                dev->SetVertexShader(vsHeavy ? vsHeavy : vs10);
                dev->SetPixelShader(psHeavy);
                dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                dev->SetRenderState(D3DRS_ALPHAREF, 0x40 + b.mesh);
                dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                dev->SetVertexShaderConstantF(10, &M[0][0], 4);
                float mat[8] = {0.8f + 0.005f * b.mesh, 0.8f, 0.8f, 1, 0.05f, 0.05f, 0.05f, 0};
                dev->SetVertexShaderConstantF(20, mat, 2);
                float lt[8] = {light[0], light[1], light[2], 0, 0.3f, 0.6f, 0.2f, 0};
                dev->SetVertexShaderConstantF(22, lt, 2);
                float amb[8] = {0.2f, 0.2f, 0.25f, 0, 1, 1, 1, 1};
                dev->SetVertexShaderConstantF(24, amb, 2);
                dev->SetVertexShaderConstantF(3, fog, 1);
                for (int pc = 0; pc < 8; pc++) {
                    float p[4] = {1.0f - 0.02f * pc, 1.0f, 1.0f - 0.01f * b.mesh, pc == 7 ? 0.0f : 1.0f};
                    if (pc == 6) p[0] = 0.5f + 0.4f * sinf(tick * 0.05f + b.mesh);  // an animated material parameter
                    dev->SetPixelShaderConstantF(pc, p, 1);
                }
                drawMesh(b.mesh);
            }
            dev->SetPixelShader(nullptr);
            // characters: a 20-bone palette each (60 registers), walking in circles
            dev->SetVertexShader(vsSkin);
            dev->SetVertexShaderConstantF(0, &PV[0][0], 4);
            float tagX[30], tagY[30];
            int ntag = 0;
            for (int c = 0; c < 30; c++) {
                float ang = tick * 0.04f + c, px = (c % 6 - 3) * 14.0f + 5.0f * cosf(ang), pz = (c / 6) * 12.0f - 20.0f + 5.0f * sinf(ang);
                float pal[60 * 4];
                for (int bn = 0; bn < 20; bn++) {
                    float ba = tick * 0.05f * (bn % 5 + 1) + c, cb = cosf(ba), sb = sinf(ba);
                    float rows[12] = {cb * 1.5f, 0, sb * 1.5f, px, 0, 1.5f, 0, 1.5f + bn * 0.1f, -sb * 1.5f, 0, cb * 1.5f, pz};
                    memcpy(pal + bn * 12, rows, sizeof rows);
                }
                dev->SetVertexShaderConstantF(8, pal, 60);
                float idx[4] = {(float)((c % 20) * 3), 0, 0, 0};
                dev->SetVertexShaderConstantF(4, idx, 1);
                drawMesh((c * 7) % kMeshes);
                float hx = PV[0][0] * px + PV[0][1] * 4.0f + PV[0][2] * pz + PV[0][3];
                float hy = PV[1][0] * px + PV[1][1] * 4.0f + PV[1][2] * pz + PV[1][3];
                float hw = PV[3][0] * px + PV[3][1] * 4.0f + PV[3][2] * pz + PV[3][3];
                if (hw > 1.0f && ntag < 30) { tagX[ntag] = 400 + 400 * hx / hw; tagY[ntag] = 300 - 300 * hy / hw; ntag++; }
            }
            dev->SetVertexShader(nullptr);
            // name tags (12 glyph quads each, following the characters) and a static chat/menu panel (300 glyphs):
            // screen space, like MHF's ~750 pre-transformed quads per frame
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            auto glyph = [&](float x0, float y0, float u) {
                VG q[4] = {{x0, y0, 0, 1, 0xffffffff, u, 0}, {x0 + 6, y0, 0, 1, 0xffffffff, u + 0.04f, 0},
                           {x0, y0 + 10, 0, 1, 0xffffffff, u, 1}, {x0 + 6, y0 + 10, 0, 1, 0xffffffff, u + 0.04f, 1}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
            };
            for (int t = 0; t < ntag; t++)
                for (int g = 0; g < 12; g++) glyph(tagX[t] - 40 + g * 7.0f, tagY[t] - 30, ((t * 5 + g * 3) % 25) * 0.04f);
            for (int l = 0; l < 10; l++)
                for (int g = 0; g < 30; g++) glyph(20 + g * 7.0f, 420 + l * 12.0f, ((l * 7 + g * 11) % 25) * 0.04f);
        }
        // 1) shader-constant triangle (left)
        if (vs) {
            float c = cosf(a), s = sinf(a);
            float m[16] = {c * 0.4f, -s * 0.4f, 0, -0.5f, s * 0.4f, c * 0.4f, 0, 0, 0, 0, 1, 0.5f, 0, 0, 0, 1};  // rows = transposed WVP
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vs);
            dev->SetVertexShaderConstantF(0, m, 4);  // shared "camera" upload, read by every draw below
            // a draw that only exists every other tick (like a mesh culled in and out): the upload above lands
            // in its segment on even ticks and in the main triangle's segment on odd ticks
            if (tick % 2 == 0) {
                V t0[3] = {{-1.2f, 1.2f, 0, 0xffffffff}, {-1.0f, 1.2f, 0, 0xffffffff}, {-1.1f, 1.0f, 0, 0xffffffff}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, t0, sizeof(V));
            }
            V tri[3] = {{0, 1, 0, 0xffff4040}, {-0.87f, -0.5f, 0, 0xff40ff40}, {0.87f, -0.5f, 0, 0xff4040ff}};
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(V));
            V tri2[3] = {{0.2f, 1.6f, 0, 0xffffff00}, {0.0f, 1.3f, 0, 0xffffff00}, {0.4f, 1.3f, 0, 0xffffff00}};
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri2, sizeof(V));
            dev->SetVertexShader(nullptr);
        }
        // 5) skinned triangle (bottom right): bone rotates 40 degrees per tick; slerp must keep its size
        if (vsSkin) {
            float ba = tick * 0.698f, cb = cosf(ba), sbn = sinf(ba);
            float vp[16] = {0.25f, 0, 0, 0.55f, 0, 0.25f, 0, -0.55f, 0, 0, 1, 0.5f, 0, 0, 0, 1};  // rows
            float idx[4] = {0, 0, 0, 0};
            float bone[12] = {cb, -sbn, 0, 0, sbn, cb, 0, 0, 0, 0, 1, 0};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vsSkin);
            dev->SetVertexShaderConstantF(0, vp, 4);
            dev->SetVertexShaderConstantF(4, idx, 1);
            dev->SetVertexShaderConstantF(8, bone, 3);
            V st[3] = {{0, 1, 0, 0xffff80ff}, {-0.87f, -0.5f, 0, 0xffff80ff}, {0.87f, -0.5f, 0, 0xffffffff}};
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, st, sizeof(V));
            dev->SetVertexShader(nullptr);
        }
        // 7) foliage cards (bottom middle): 60 camera-facing quads at fixed world positions, orbiting camera,
        //    drawn back-to-front so the draw order reshuffles as the camera turns (like MHF's leaf billboards)
        if (vs && billboards) {
            // three "trees" (clusters of 20 cards) around the orbit; cards outside the view are culled, so the set
            // of drawn cards changes as the camera turns (leftover cards in both frames are different cards)
            struct BB { float x, y, z, d; int k; } bb[60];
            float ang = tick * 0.12f, ca2 = cosf(ang), sa2 = sinf(ang);
            int nb = 0;
            for (int k = 0; k < 60; k++) {
                float cx = (k / 20 - 1) * 2.5f;
                float x = cx + (((k * 37) % 17) / 16.0f - 0.5f) * 1.2f, z = (((k * 23) % 13) / 12.0f - 0.5f) * 1.2f;
                float y = (((k * 11) % 7) / 6.0f - 0.5f) * 1.0f;
                float vx = ca2 * x - sa2 * z, vz = sa2 * x + ca2 * z + 4.0f;
                if (vz < 0.5f || std::fabs(0.6f * vx / vz) > 0.7f) continue;  // culled
                bb[nb++] = {x, y, z, vz, k};
            }
            std::sort(bb, bb + nb, [](const BB& a, const BB& b) { return a.d > b.d; });
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vs);
            for (int bi = 0; bi < nb; bi++) {
                const BB& b = bb[bi];
                // world: translate to (x,y,z), face the camera (rotate by -ang about Y), scale
                float s = 0.15f;
                float W[4][4] = {{ca2 * s, 0, sa2 * s, b.x}, {0, s, 0, b.y}, {-sa2 * s, 0, ca2 * s, b.z}, {0, 0, 0, 1}};
                float Vw[4][4] = {{ca2, 0, -sa2, 0}, {0, 1, 0, 0}, {sa2, 0, ca2, 4.0f}, {0, 0, 0, 1}};
                float Pm[4][4] = {{0.6f, 0, 0, 0}, {0, 0.9f, 0, 0}, {0, 0, 1.0f, -0.1f}, {0, 0, 1, 0}};
                float VW[4][4] = {}, M[4][4] = {};
                for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++) VW[r][c] += Vw[r][k] * W[k][c];
                for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++) M[r][c] += Pm[r][k] * VW[k][c];
                dev->SetVertexShaderConstantF(0, &M[0][0], 4);
                V card[6] = {{-1, -1, 0, 0xff30c030}, {1, -1, 0, 0xff30c030}, {-1, 1, 0, 0xff60ff60},
                             {1, -1, 0, 0xff30c030}, {1, 1, 0, 0xff60ff60}, {-1, 1, 0, 0xff60ff60}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, card, sizeof(V));
            }
            dev->SetVertexShader(nullptr);
        }
        // 8) culling swap in game-like units (perspective, w = view depth): the camera yaws 3.4 degrees per tick;
        //    three copies of one tree mesh sit exactly one view-width apart and only the copy inside the view is
        //    drawn, so when one leaves at the left edge the next enters at the right edge in the same tick. A fourth
        //    copy is always drawn (like an uncull'd tree behind the camera), which makes the mesh a group of two whose
        //    median match distance is the bad pair itself. Blending the swapped copies slides a ghost tree through
        //    the middle of the in-between frames. Six static posts (always drawn) give the camera-motion estimate.
        if (vs10 && swapTest) {
            float yaw = tick * 0.06f, cy = cosf(yaw), sy = sinf(yaw);
            float Vw[4][4] = {{cy, 0, -sy, 0}, {0, 1, 0, -150.0f}, {sy, 0, cy, 0}, {0, 0, 0, 1}};
            float Pm[4][4] = {{1.0f, 0, 0, 0}, {0, 1.333f, 0, 0}, {0, 0, 1.0f, -1.0f}, {0, 0, 1, 0}};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vs10);
            auto disc = [&](float wx, float wy, float wz, float r, int tris, DWORD col) {
                float W[4][4] = {{r, 0, 0, wx}, {0, r, 0, wy}, {0, 0, r, wz}, {0, 0, 0, 1}};
                float VW[4][4] = {}, M[4][4] = {};
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) VW[i][j] += Vw[i][k] * W[k][j];
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) M[i][j] += Pm[i][k] * VW[k][j];
                dev->SetVertexShaderConstantF(10, &M[0][0], 4);
                V f[3 * 24];
                for (int t = 0; t < tris; t++) {
                    float a0 = t * 6.2832f / tris, a1 = (t + 1) * 6.2832f / tris;
                    f[t * 3 + 0] = {0, 0, 0, col};
                    f[t * 3 + 1] = {cosf(a0), sinf(a0), 0, col};
                    f[t * 3 + 2] = {cosf(a1), sinf(a1), 0, col};
                }
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, f, sizeof(V));
            };
            {  // a big camera-facing fog card at a fixed world spot, culled in and out every 3 ticks
                float ca = 0.9f, cs = sinf(ca), cc = cosf(ca);
                float rel = fmodf(ca - yaw + 3.1416f * 41, 6.2832f) - 3.1416f;
                if (std::fabs(rel) < 0.9f && (tick / 3) % 2 == 0) {
                    float r = 1100.0f, wx = cs * 1400.0f, wz = cc * 1400.0f;
                    // world = translate * rotY(yaw) * scale: turned to face the camera
                    float W[4][4] = {{cy * r, 0, sy * r, wx}, {0, r, 0, 200.0f}, {-sy * r, 0, cy * r, wz}, {0, 0, 0, 1}};
                    float VW[4][4] = {}, M[4][4] = {};
                    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) VW[i][j] += Vw[i][k] * W[k][j];
                    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) M[i][j] += Pm[i][k] * VW[k][j];
                    dev->SetVertexShaderConstantF(10, &M[0][0], 4);
                    V card[6] = {{-1, -1, 0, 0xff6070a0}, {1, -1, 0, 0xff6070a0}, {-1, 1, 0, 0xff6070a0},
                                 {1, -1, 0, 0xff6070a0}, {1, 1, 0, 0xff6070a0}, {-1, 1, 0, 0xff6070a0}};
                    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, card, sizeof(V));
                }
            }
            for (int p = 0; p < 6; p++) {  // posts: distinct meshes all around the camera
                float pa = p * 1.0472f + 0.5f;
                disc(sinf(pa) * 2000.0f, 0, cosf(pa) * 2000.0f, 120.0f, 12 + p, 0xff909090);
            }
            disc(sinf(3.1416f) * 1500.0f, 250.0f, cosf(3.1416f) * 1500.0f, 450.0f, 20, 0xff30c030);  // always drawn
            for (int k = 0; k < 3; k++) {
                float rel = 0.3f + k * 1.7102f - yaw;
                rel = fmodf(rel + 3.1416f, 6.2832f);
                if (rel < 0) rel += 6.2832f;
                rel -= 3.1416f;
                if (std::fabs(rel) > 0.8551f) continue;  // culled: outside the view
                float wa = 0.3f + k * 1.7102f;
                disc(sinf(wa) * 1500.0f, 250.0f, cosf(wa) * 1500.0f, 450.0f, 20, 0xff30c030);
            }
            dev->SetVertexShader(nullptr);
        }
        // 8b) labelTest 5: a fast camera flick - the camera orbits its target up to 22 degrees per tick (like a mouse flick
        //     in MHF's third-person camera) over a field of static posts of distinct meshes and a ring of identical ones
        if (vs10 && (labelTest == 5 || labelTest == 11)) {  // (11: titles over the posts, below)
            float yaw = 1.1f * sinf(tick * 0.35f), cy = cosf(yaw), sy = sinf(yaw);
            float dist = 700.0f, tx = 0, ty = 100.0f, tz = 0;
            float cx = tx - sy * dist, cz = tz - cy * dist, cyy = 260.0f;  // camera position (looks along (sy, 0, cy))
            float R[3][3] = {{cy, 0, -sy}, {0, 1, 0}, {sy, 0, cy}};
            float Vw[4][4] = {{R[0][0], R[0][1], R[0][2], -(R[0][0] * cx + R[0][1] * cyy + R[0][2] * cz)},
                              {R[1][0], R[1][1], R[1][2], -(R[1][0] * cx + R[1][1] * cyy + R[1][2] * cz)},
                              {R[2][0], R[2][1], R[2][2], -(R[2][0] * cx + R[2][1] * cyy + R[2][2] * cz)},
                              {0, 0, 0, 1}};
            float Pm[4][4] = {{1.0f, 0, 0, 0}, {0, 1.333f, 0, 0}, {0, 0, 1.0f, -1.0f}, {0, 0, 1, 0}};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vs10);
            auto disc = [&](float wx, float wy, float wz, float r, int tris, DWORD col) {
                float W[4][4] = {{r, 0, 0, wx}, {0, r, 0, wy}, {0, 0, r, wz}, {0, 0, 0, 1}};
                float VW[4][4] = {}, M[4][4] = {};
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) VW[i][j] += Vw[i][k] * W[k][j];
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) M[i][j] += Pm[i][k] * VW[k][j];
                if (M[3][3] < 50.0f) return;  // (behind or at the camera: not drawn)
                dev->SetVertexShaderConstantF(10, &M[0][0], 4);
                V f[3 * 24];
                for (int t = 0; t < tris; t++) {
                    float a0 = t * 6.2832f / tris, a1 = (t + 1) * 6.2832f / tris;
                    f[t * 3 + 0] = {0, 0, 0, col};
                    f[t * 3 + 1] = {cosf(a0), sinf(a0), 0, col};
                    f[t * 3 + 2] = {cosf(a1), sinf(a1), 0, col};
                }
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, f, sizeof(V));
            };
            for (int p = 0; p < 16; p++) {  // distinct posts around the target
                float pa = p * 0.3927f, pr = 300.0f + (p % 4) * 220.0f;
                disc(tx + sinf(pa) * pr, 60.0f + (p % 3) * 70.0f, tz + cosf(pa) * pr, 40.0f + (p % 5) * 8.0f, 8 + p, 0xff000000u | (0x404040u + p * 0x0a0806u));
            }
            for (int p = 0; p < 16; p += 3) {  // flame-like cards above some posts: camera-facing (rotY(yaw)), anchored in the world
                float pa = p * 0.3927f, pr = 300.0f + (p % 4) * 220.0f, r = 30.0f;
                float wx = tx + sinf(pa) * pr, wy = 60.0f + (p % 3) * 70.0f + 60.0f, wz = tz + cosf(pa) * pr;
                float W[4][4] = {{cy * r, 0, sy * r, wx}, {0, r, 0, wy}, {-sy * r, 0, cy * r, wz}, {0, 0, 0, 1}};
                float VW[4][4] = {}, M[4][4] = {};
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) VW[i][j] += Vw[i][k] * W[k][j];
                for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) M[i][j] += Pm[i][k] * VW[k][j];
                if (M[3][3] < 50.0f) continue;
                dev->SetVertexShaderConstantF(10, &M[0][0], 4);
                V card[6] = {{-1, -1, 0, 0xffff8020}, {1, -1, 0, 0xffff8020}, {-1, 1, 0, 0xffffd040},
                             {1, -1, 0, 0xffff8020}, {1, 1, 0, 0xffffd040}, {-1, 1, 0, 0xffffd040}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, card, sizeof(V));
            }
            for (int k = 0; k < 24; k++) {  // a ring of identical small discs (a repeated mesh, like grass clumps)
                float ka = k * 0.2618f;
                disc(tx + sinf(ka) * 1300.0f, 30.0f, tz + cosf(ka) * 1300.0f, 30.0f, 6, 0xff30a030);
            }
            dev->SetVertexShader(nullptr);
            if (labelTest == 11) {  // every post carries the same title (a crowd's shared title), drawn over it in screen space
                struct VG { float x, y, z, rhw; DWORD c; float u, v; };
                dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
                float SW = 800, SH = 600;  // (the back buffer: the window can be resized meanwhile)
                IDirect3DSurface9* bbs = nullptr;
                if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bbs)) && bbs) {
                    D3DSURFACE_DESC bd;
                    bbs->GetDesc(&bd);
                    SW = (float)bd.Width, SH = (float)bd.Height;
                    bbs->Release();
                }
                static const int title[6] = {4, 0, 5, 2, 1, 3};
                for (int p = 0; p < 16; p++) {
                    float pa = p * 0.3927f, pr = 300.0f + (p % 4) * 220.0f, r = 40.0f + (p % 5) * 8.0f;
                    float wx = tx + sinf(pa) * pr, wy = 60.0f + (p % 3) * 70.0f + r + 25.0f, wz = tz + cosf(pa) * pr;
                    float cv[4];
                    for (int i = 0; i < 4; i++) {
                        float vv = Vw[i][0] * wx + Vw[i][1] * wy + Vw[i][2] * wz + Vw[i][3];
                        cv[i] = vv;
                    }
                    float cx4 = Pm[0][0] * cv[0] + Pm[0][1] * cv[1] + Pm[0][2] * cv[2] + Pm[0][3] * cv[3];
                    float cy4 = Pm[1][0] * cv[0] + Pm[1][1] * cv[1] + Pm[1][2] * cv[2] + Pm[1][3] * cv[3];
                    float cw4 = Pm[3][0] * cv[0] + Pm[3][1] * cv[1] + Pm[3][2] * cv[2] + Pm[3][3] * cv[3];
                    if (cw4 < 50.0f) continue;
                    float sx = (cx4 / cw4 + 1) * 0.5f * SW - 36.0f, sy = (1 - cy4 / cw4) * 0.5f * SH - 8.0f;
                    for (int pass = 0; pass < 2; pass++)
                        for (int g = 0; g < 6; g++) {
                            float x0 = floorf(sx + g * 12.0f + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f), y0 = floorf(sy + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f);
                            if (x0 < 0 || x0 + 9 > SW || y0 < 0 || y0 + 16 > SH) continue;
                            DWORD c = pass ? 0xffffe080 : 0xff202020;
                            float u = title[g] * 0.1f;
                            VG q[4] = {{x0, y0, 0, 1, c, u, 0}, {x0 + 9, y0, 0, 1, c, u + 0.1f, 0}, {x0, y0 + 16, 0, 1, c, u, 1}, {x0 + 9, y0 + 16, 0, 1, c, u + 0.1f, 1}};
                            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
                        }
                }
            }
        }
        // 8c) labelTest 6: the same fast flick, drawn like MHF's main pass - the camera's view-projection uploaded once
        //     (c0-c3, shared by every draw) and each object placed by its own (bone) matrix in the palette
        if (vsSkin && labelTest == 6) {
            float yaw = 1.1f * sinf(tick * 0.35f), cy = cosf(yaw), sy = sinf(yaw);
            float dist = 700.0f, tx = 0, tz = 0;
            float cx = tx - sy * dist, cz = tz - cy * dist, cyy = 260.0f;
            float R[3][3] = {{cy, 0, -sy}, {0, 1, 0}, {sy, 0, cy}};
            float Vw[4][4] = {{R[0][0], R[0][1], R[0][2], -(R[0][0] * cx + R[0][1] * cyy + R[0][2] * cz)},
                              {R[1][0], R[1][1], R[1][2], -(R[1][0] * cx + R[1][1] * cyy + R[1][2] * cz)},
                              {R[2][0], R[2][1], R[2][2], -(R[2][0] * cx + R[2][1] * cyy + R[2][2] * cz)},
                              {0, 0, 0, 1}};
            float Pm[4][4] = {{1.0f, 0, 0, 0}, {0, 1.333f, 0, 0}, {0, 0, 1.0f, -1.0f}, {0, 0, 1, 0}};
            float VP[4][4] = {};
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) VP[i][j] += Pm[i][k] * Vw[k][j];
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vsSkin);
            dev->SetVertexShaderConstantF(0, &VP[0][0], 4);  // the camera, once
            float idx[4] = {0, 0, 0, 0};
            dev->SetVertexShaderConstantF(4, idx, 1);
            auto disc = [&](float wx, float wy, float wz, float r, int tris, DWORD col) {
                float vx = VP[3][0] * wx + VP[3][1] * wy + VP[3][2] * wz + VP[3][3];
                if (vx < 50.0f) return;  // (behind the camera: not drawn)
                float pal[12] = {r, 0, 0, wx, 0, r, 0, wy, 0, 0, r, wz};
                dev->SetVertexShaderConstantF(8, pal, 3);
                V f[3 * 24];
                for (int t = 0; t < tris; t++) {
                    float a0 = t * 6.2832f / tris, a1 = (t + 1) * 6.2832f / tris;
                    f[t * 3 + 0] = {0, 0, 0, col};
                    f[t * 3 + 1] = {cosf(a0), sinf(a0), 0, col};
                    f[t * 3 + 2] = {cosf(a1), sinf(a1), 0, col};
                }
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, f, sizeof(V));
            };
            for (int p = 0; p < 16; p++) {
                float pa = p * 0.3927f, pr = 300.0f + (p % 4) * 220.0f;
                disc(tx + sinf(pa) * pr, 60.0f + (p % 3) * 70.0f, tz + cosf(pa) * pr, 40.0f + (p % 5) * 8.0f, 8 + p, 0xff000000u | (0x404040u + p * 0x0a0806u));
            }
            for (int k = 0; k < 24; k++) {
                float ka = k * 0.2618f;
                disc(tx + sinf(ka) * 1300.0f, 30.0f, tz + cosf(ka) * 1300.0f, 30.0f, 6, 0xff30a030);
            }
            dev->SetVertexShader(nullptr);
        }
        // 8d) labelTest 7: braziers in the same flick, drawn like MHF's main pass - three fires of 18 flame particles each
        //     (a camera-facing card per particle, additive), rising, dying and respawning, drawn back to front by their
        //     depth in the view, so a camera turn reorders them; 8: the same particles in a fixed order
        if (vsSkin && (labelTest == 7 || labelTest == 8)) {
            float yaw = 1.1f * sinf(tick * 0.35f), cy = cosf(yaw), sy = sinf(yaw);
            float dist = 700.0f, tx = 0, tz = 0;
            float cx = tx - sy * dist, cz = tz - cy * dist, cyy = 260.0f;
            float R[3][3] = {{cy, 0, -sy}, {0, 1, 0}, {sy, 0, cy}};
            float Vw[4][4] = {{R[0][0], R[0][1], R[0][2], -(R[0][0] * cx + R[0][1] * cyy + R[0][2] * cz)},
                              {R[1][0], R[1][1], R[1][2], -(R[1][0] * cx + R[1][1] * cyy + R[1][2] * cz)},
                              {R[2][0], R[2][1], R[2][2], -(R[2][0] * cx + R[2][1] * cyy + R[2][2] * cz)},
                              {0, 0, 0, 1}};
            float Pm[4][4] = {{1.0f, 0, 0, 0}, {0, 1.333f, 0, 0}, {0, 0, 1.0f, -1.0f}, {0, 0, 1, 0}};
            float VP[4][4] = {};
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) VP[i][j] += Pm[i][k] * Vw[k][j];
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vsSkin);
            dev->SetVertexShaderConstantF(0, &VP[0][0], 4);
            float idx[4] = {0, 0, 0, 0};
            dev->SetVertexShaderConstantF(4, idx, 1);
            auto disc = [&](float wx, float wy, float wz, float r, int tris, DWORD col) {
                float vx = VP[3][0] * wx + VP[3][1] * wy + VP[3][2] * wz + VP[3][3];
                if (vx < 50.0f) return;
                float pal[12] = {r, 0, 0, wx, 0, r, 0, wy, 0, 0, r, wz};
                dev->SetVertexShaderConstantF(8, pal, 3);
                V f[3 * 24];
                for (int t = 0; t < tris; t++) {
                    float a0 = t * 6.2832f / tris, a1 = (t + 1) * 6.2832f / tris;
                    f[t * 3 + 0] = {0, 0, 0, col};
                    f[t * 3 + 1] = {cosf(a0), sinf(a0), 0, col};
                    f[t * 3 + 2] = {cosf(a1), sinf(a1), 0, col};
                }
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, f, sizeof(V));
            };
            for (int p = 0; p < 16; p++) {
                float pa = p * 0.3927f, pr = 300.0f + (p % 4) * 220.0f;
                disc(tx + sinf(pa) * pr, 60.0f + (p % 3) * 70.0f, tz + cosf(pa) * pr, 40.0f + (p % 5) * 8.0f, 8 + p, 0xff000000u | (0x404040u + p * 0x0a0806u));
            }
            struct Pt { float x, y, z, r, d; DWORD col; };
            Pt pts[3 * 18];
            int n = 0;
            for (int b = 0; b < 3; b++) {
                float ba = b * 2.0944f + 0.4f, bx = tx + sinf(ba) * 420.0f, bz = tz + cosf(ba) * 420.0f, by = 80.0f;
                disc(bx, by - 15.0f, bz, 35.0f, 10, 0xff604020);  // the bowl
                for (int i = 0; i < 18; i++) {
                    const int life = 18;
                    int age = (tick + i * 7 + b * 5) % life, gen = (tick + i * 7 + b * 5) / life;
                    uint32_t h = (uint32_t)gen * 2654435761u ^ (uint32_t)(i * 40503 + b * 977);
                    h ^= h >> 13;
                    h *= 0x5bd1e995u;
                    float jx = ((h & 255) / 255.0f - 0.5f) * 40.0f, jz = (((h >> 8) & 255) / 255.0f - 0.5f) * 40.0f;
                    float a = age / (float)life;
                    Pt& p = pts[n++];
                    p.x = bx + jx * (1.0f - 0.5f * a);
                    p.z = bz + jz * (1.0f - 0.5f * a);
                    p.y = by + a * 110.0f;
                    p.r = 14.0f * (1.0f - 0.6f * a);
                    int k = (int)(200 * (1.0f - a));
                    p.col = 0xff000000u | (uint32_t)k << 16 | (uint32_t)(k * 2 / 5) << 8 | (uint32_t)(k / 10);
                    p.d = VP[3][0] * p.x + VP[3][1] * p.y + VP[3][2] * p.z + VP[3][3];
                }
            }
            if (labelTest == 7) std::sort(pts, pts + n, [](const Pt& a, const Pt& b) { return a.d > b.d; });
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            for (int i = 0; i < n; i++) {
                const Pt& p = pts[i];
                if (p.d < 50.0f) continue;
                float pal[12] = {cy * p.r, 0, sy * p.r, p.x, 0, p.r, 0, p.y, -sy * p.r, 0, cy * p.r, p.z};
                dev->SetVertexShaderConstantF(8, pal, 3);
                V card[6] = {{-1, -1, 0, p.col}, {1, -1, 0, p.col}, {-1, 1, 0, p.col}, {1, -1, 0, p.col}, {1, 1, 0, p.col}, {-1, 1, 0, p.col}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, card, sizeof(V));
            }
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
            dev->SetVertexShader(nullptr);
        }
        // 9) name tag: six identical glyph quads (same mesh and shader, distinct colours like distinct letters)
        //    0.05 apart, following a character that runs back and forth up to 0.12 per tick - further than the
        //    letter spacing, so nearest-position matching pairs each letter with its neighbour
        if (vs && tagTest) {
            float ox = 0.55f * sinf(tick * 0.22f);
            static const DWORD cols[6] = {0xffff4040, 0xff40ff40, 0xff4040ff, 0xffffff40, 0xffff40ff, 0xff40ffff};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vs);
            for (int g = 0; g < 6; g++) {
                float m[16] = {0.02f, 0, 0, ox + (g - 2.5f) * 0.05f, 0, 0.03f, 0, -0.75f, 0, 0, 1, 0.5f, 0, 0, 0, 1};
                dev->SetVertexShaderConstantF(0, m, 4);
                V q6[6] = {{-1, -1, 0, cols[g]}, {1, -1, 0, cols[g]}, {-1, 1, 0, cols[g]}, {1, -1, 0, cols[g]}, {1, 1, 0, cols[g]}, {-1, 1, 0, cols[g]}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q6, sizeof(V));
            }
            dev->SetVertexShader(nullptr);
        }
        // 10) screen-space label like MHF's: one pre-transformed quad per glyph (the texcoords tell the glyph), the
        //     word "Small EX" with its two identical l's 12 px apart, sliding up to 44 px per tick
        if (labelTest == 1 || labelTest == 2) {
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            float lx = 330.0f + 200.0f * sinf(tick * 0.22f);
            static const int glyph[8] = {0, 1, 2, 3, 3, -1, 4, 5};  // S m a l l _ E X
            static const DWORD gcol[6] = {0xffffffff, 0xffffc0c0, 0xffc0ffc0, 0xffc0c0ff, 0xffffffc0, 0xffc0ffff};
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            for (int k = 0; k < 8; k++) {
                if (glyph[k] < 0) continue;
                int gk = glyph[k];
                if (labelTest == 2 && (tick / 7) % 2 == 1) gk = gk == 1 ? 4 : gk == 4 ? 1 : gk;  // m <-> E
                float x0 = lx + k * 12.0f, y0 = 300.0f, u = gk * 0.1f;
                DWORD c = gcol[glyph[k]];
                VG q[4] = {{x0, y0, 0, 1, c, u, 0}, {x0 + 9, y0, 0, 1, c, u + 0.1f, 0}, {x0, y0 + 16, 0, 1, c, u, 1}, {x0 + 9, y0 + 16, 0, 1, c, u + 0.1f, 1}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
            }
        }
        // 10b) labelTest 3: a name label like "Gatekeeper" (one letter four times, 12 px apart) swinging up to ~300 px per
        //      tick, as in a fast camera turn, past static HUD text made of the same glyphs
        if (labelTest == 3) {
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            static const DWORD gcol[6] = {0xffffffff, 0xffffc0c0, 0xffc0ffc0, 0xffc0c0ff, 0xffffffc0, 0xffc0ffff};
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            auto glyphQuad = [&](float x0, float y0, int gk) {
                float u = gk * 0.1f;
                VG q[4] = {{x0, y0, 0, 1, gcol[gk], u, 0}, {x0 + 9, y0, 0, 1, gcol[gk], u + 0.1f, 0}, {x0, y0 + 16, 0, 1, gcol[gk], u, 1},
                           {x0 + 9, y0 + 16, 0, 1, gcol[gk], u + 0.1f, 1}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
            };
            static const int hud[6] = {4, 2, 4, 4, 1, 4};
            for (int k = 0; k < 6; k++) glyphQuad(120.0f + k * 12.0f, 330.0f, hud[k]);
            for (int k = 0; k < 6; k++) glyphQuad(560.0f + k * 12.0f, 280.0f, hud[5 - k]);
            static const int gate[10] = {0, 2, 3, 4, 1, 4, 4, 5, 4, 3};  // G a t e k e e p e r
            float lx = 330.0f + 330.0f * sinf(tick * 0.9f), ly = 300.0f + 40.0f * sinf(tick * 0.5f);
            for (int k = 0; k < 10; k++) glyphQuad(lx + k * 12.0f, ly, gate[k]);
        }
        // 10c) labelTest 4: what a fast camera turn does to MHF's labels: each drawn as a dark shadow string 2 px off, then
        //      the text; two labels on one line that drift apart (different depths) and merge into one string when close;
        //      glyphs off the screen edge not drawn; every glyph rounded to the pixel on its own; static HUD text nearby
        if (labelTest == 4) {
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            auto glyphQuad = [&](float x0, float y0, int gk, DWORD c) {
                float u = gk * 0.1f;
                VG q[4] = {{x0, y0, 0, 1, c, u, 0}, {x0 + 9, y0, 0, 1, c, u + 0.1f, 0}, {x0, y0 + 16, 0, 1, c, u, 1}, {x0 + 9, y0 + 16, 0, 1, c, u + 0.1f, 1}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
            };
            static const DWORD tcol[6] = {0xffffffff, 0xffffc0c0, 0xffc0ffc0, 0xffc0c0ff, 0xffffffc0, 0xffc0ffff};
            auto label = [&](float ax, float ay, const int* g, int n) {
                for (int pass = 0; pass < 2; pass++)  // shadow, then text
                    for (int k = 0; k < n; k++) {
                        if (g[k] < 0) continue;
                        float x0 = floorf(ax + k * 12.0f + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f), y0 = floorf(ay + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f);
                        if (x0 < 0 || x0 + 9 > 800) continue;  // off the screen: not drawn
                        glyphQuad(x0, y0, g[k], pass ? tcol[g[k]] : 0xff202020);
                    }
            };
            static const int hud[6] = {1, 2, 3, 3, 2, 1};
            label(140.3f, 340.0f, hud, 6);
            static const int clan[9] = {0, 3, 2, 4, -1, 5, 2, 3, 3};  // C l a n _ H a l l
            static const int hall[9] = {5, 2, 3, 3, -1, 0, 3, 2, 4};  // H a l l _ C l a n
            float ax = 330.0f + 380.0f * sinf(tick * 0.9f + 0.3f), ay = 300.0f + 30.0f * sinf(tick * 0.5f);
            float bx = 330.0f + 120.0f + 330.0f * sinf(tick * 0.9f + 0.3f) * 0.85f;  // further away: moves less
            label(ax + 0.37f, ay + 0.21f, clan, 9);
            label(bx + 0.71f, ay + 0.21f, hall, 9);
        }
        // 10b) labelTest 9: a crowd's labels - six identical titles (the same glyphs, shadowed) 110 px apart at slightly
        //      different depths, swept by a fast camera turn further per tick than they are apart: the nearest copy
        //      of a title in the previous tick is usually another player's
        // 10c) labelTest 12: four different labels in a fast camera sweep, drawn from a glyph atlas that is reshuffled
        //      every 5 ticks (MHF rewrites its text atlas each frame; new text moves letters to other slots): all keys change
        // 10d) labelTest 14: like 12, but drawn from a real glyph atlas (a managed texture) that is rewritten when the
        //      letters move to other slots, every 5 ticks - the same letter looks the same wherever it is
        if (labelTest == 14) {
            static IDirect3DTexture9* atlas = nullptr;
            static int atlasSlot = -1;
            const int CW = 12, CH = 16, AW = 128, AH = 16;
            if (!atlas) dev->CreateTexture(AW, AH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &atlas, nullptr);
            int slot = (tick / 5) * 3;
            if (atlas && slot != atlasSlot) {  // re-slot: cell c holds letter (c - slot) mod 10
                atlasSlot = slot;
                D3DLOCKED_RECT lr;
                if (SUCCEEDED(atlas->LockRect(0, &lr, nullptr, 0))) {
                    for (int y = 0; y < AH; y++) {
                        DWORD* row = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
                        for (int x = 0; x < AW; x++) {
                            int c = x / CW, letter = ((c - slot) % 10 + 10) % 10, lx = x % CW;
                            bool on = c < 10 && ((lx * 7 + y * 3 + letter * 5) % (3 + letter % 4) == 0 || lx == letter % CW);
                            row[x] = on ? 0xffffffff : 0x00000000;
                        }
                    }
                    atlas->UnlockRect(0);
                }
            }
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            dev->SetTexture(0, atlas);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            static const int names[4][7] = {{1, 5, 0, 4, 2, 6, 3}, {3, 3, 0, 1, 5, 4, 0}, {7, 2, 8, 2, 9, 1, 4}, {0, 6, 6, 9, 3, 8, 5}};
            float shift = 300.0f * sinf(tick * 0.9f);
            for (int k = 0; k < 4; k++) {
                float ax = 60.0f + k * 170.0f + (1.0f - 0.05f * k) * shift, ay = 230.0f + 20.0f * k;
                for (int pass = 0; pass < 2; pass++)
                    for (int g = 0; g < 7; g++) {
                        float x0 = floorf(ax + g * 12.0f + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f), y0 = floorf(ay + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f);
                        if (x0 < 0 || x0 + 12 > 800) continue;
                        int cell = (names[k][g] + slot) % 10;
                        float u0 = cell * (float)CW / AW, u1 = u0 + (float)CW / AW;
                        DWORD c = pass ? 0xffffe080 : 0xff202020;
                        VG q[4] = {{x0, y0, 0, 1, c, u0, 0}, {x0 + 12, y0, 0, 1, c, u1, 0}, {x0, y0 + 16, 0, 1, c, u0, 1}, {x0 + 12, y0 + 16, 0, 1, c, u1, 1}};
                        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
                    }
            }
            dev->SetTexture(0, nullptr);
        }
        if (labelTest == 12) {
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            static const int names[4][7] = {{1, 5, 0, 4, 2, 6, 3}, {3, 3, 0, 1, 5, 4, 0}, {7, 2, 8, 2, 9, 1, 4}, {0, 6, 6, 9, 3, 8, 5}};
            float shift = 300.0f * sinf(tick * 0.9f);
            int slot = (tick / 5) * 3;
            for (int k = 0; k < 4; k++) {
                float ax = 60.0f + k * 170.0f + (1.0f - 0.05f * k) * shift, ay = 230.0f + 20.0f * k;
                for (int pass = 0; pass < 2; pass++)
                    for (int g = 0; g < 7; g++) {
                        float x0 = floorf(ax + g * 12.0f + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f), y0 = floorf(ay + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f);
                        if (x0 < 0 || x0 + 9 > 800) continue;
                        float u = ((names[k][g] + slot) % 10) * 0.1f;
                        DWORD c = pass ? 0xffffe080 : 0xff202020;
                        VG q[4] = {{x0, y0, 0, 1, c, u, 0}, {x0 + 9, y0, 0, 1, c, u + 0.1f, 0}, {x0, y0 + 16, 0, 1, c, u, 1}, {x0 + 9, y0 + 16, 0, 1, c, u + 0.1f, 1}};
                        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
                    }
            }
        }
        if (labelTest == 9 || labelTest == 10 || labelTest == 13) {  // (10: the titles alone, no unique name on screen; 13: like a menu, slowly)
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            auto glyphQuad = [&](float x0, float y0, int gk, DWORD c) {
                float u = gk * 0.1f;
                VG q[4] = {{x0, y0, 0, 1, c, u, 0}, {x0 + 9, y0, 0, 1, c, u + 0.1f, 0}, {x0, y0 + 16, 0, 1, c, u, 1}, {x0 + 9, y0 + 16, 0, 1, c, u + 0.1f, 1}};
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
            };
            static const int title[6] = {4, 0, 5, 2, 1, 3};
            static const int name1[5] = {1, 5, 0, 4, 2}, name2[7] = {3, 3, 0, 1, 5, 4, 0};  // (two players' own names: unique)
            float shift = labelTest == 13 ? 30.0f * sinf(tick * 0.2f) : 300.0f * sinf(tick * 0.9f);
            auto lbl = [&](float ax, float ay, const int* g, int n, DWORD c) {
                for (int pass = 0; pass < 2; pass++)  // shadow, then text
                    for (int k = 0; k < n; k++) {
                        float x0 = floorf(ax + k * 12.0f + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f), y0 = floorf(ay + 0.5f) - 0.5f + (pass ? 0.0f : 2.0f);
                        if (x0 < 0 || x0 + 9 > 800) continue;
                        glyphQuad(x0, y0, g[k], pass ? c : 0xff202020);
                    }
            };
            for (int k = 0; k < 6; k++) {
                float f = 1.0f - 0.05f * k;
                lbl(60.0f + k * 110.0f + f * shift, 250.0f + 6.0f * k, title, 6, 0xffffe080);
            }
            if (labelTest == 9) {
                lbl(200.0f + 0.97f * shift, 210.0f, name1, 5, 0xffffffff);
                lbl(470.0f + 0.82f * shift, 320.0f, name2, 7, 0xffffffff);
            }
        }
        // 11) static menu text with a glyph cache that reshuffles: every 10 ticks two letters trade their atlas slots
        //     (texcoords), so an unchanged letter shares its key with a different letter 48 px away
        if (menuTest) {
            struct VG { float x, y, z, rhw; DWORD c; float u, v; };
            static const DWORD mcol[8] = {0xffffffff, 0xffff8080, 0xff80ff80, 0xff8080ff, 0xffffff80, 0xffff80ff, 0xff80ffff, 0xffc0c0c0};
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            bool swapped = (tick / 10) % 2 == 1;
            for (int line = 0; line < 3; line++)
                for (int k = 0; k < 8; k++) {
                    int slot = line * 8 + k;
                    if (swapped && line == 1 && k == 2) slot = 8 + 6;
                    else if (swapped && line == 1 && k == 6) slot = 8 + 2;
                    float x0 = 300.0f + k * 12.0f, y0 = 528.0f + line * 22.0f, u = slot * 0.04f;
                    VG q[4] = {{x0, y0, 0, 1, mcol[k], u, 0}, {x0 + 9, y0, 0, 1, mcol[k], u + 0.04f, 0}, {x0, y0 + 16, 0, 1, mcol[k], u, 1}, {x0 + 9, y0 + 16, 0, 1, mcol[k], u + 0.04f, 1}};
                    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VG));
                }
        }
        // 12) skipped setups: like MHF, objects are set up (colour constant uploaded) and then not drawn; one or two
        //     such leftovers precede the grey quad, alternating per tick. The quad must stay the same grey.
        if (vsTint && skipTest) {
            float m[16] = {0.06f, 0, 0, -0.89f, 0, 0.075f, 0, 0.92f, 0, 0, 1, 0.5f, 0, 0, 0, 1};
            float skipA[4] = {0.45f, 0.5f, 0.6f, 1}, skipB[4] = {0.6f, 0.45f, 0.5f, 1}, grey[4] = {0.5f, 0.5f, 0.5f, 1};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vsTint);
            dev->SetVertexShaderConstantF(0, m, 4);
            dev->SetVertexShaderConstantF(4, skipA, 1);
            if (tick & 1) dev->SetVertexShaderConstantF(4, skipB, 1);
            dev->SetVertexShaderConstantF(4, grey, 1);
            V q6[6] = {{-1, -1, 0, 0xffffffff}, {1, -1, 0, 0xffffffff}, {-1, 1, 0, 0xffffffff},
                       {1, -1, 0, 0xffffffff}, {1, 1, 0, 0xffffffff}, {-1, 1, 0, 0xffffffff}};
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q6, sizeof(V));
            dev->SetVertexShader(nullptr);
        }
        // 13) a steadily ramping non-position constant that starts near zero (like a scrolling fog offset): the
        //     in-between frames must show the in-between value, not the next tick's
        if (vsTint && rampTest) {
            float m[16] = {0.06f, 0, 0, -0.89f, 0, 0.075f, 0, 0.92f, 0, 0, 1, 0.5f, 0, 0, 0, 1};
            float v = 0.02f + 0.025f * (float)(tick % 36);
            float tint[4] = {v, v, v, v};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShader(vsTint);
            dev->SetVertexShaderConstantF(0, m, 4);
            dev->SetVertexShaderConstantF(4, tint, 1);
            V q6[6] = {{-1, -1, 0, 0xffffffff}, {1, -1, 0, 0xffffffff}, {-1, 1, 0, 0xffffffff},
                       {1, -1, 0, 0xffffffff}, {1, 1, 0, 0xffffffff}, {-1, 1, 0, 0xffffffff}};
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q6, sizeof(V));
            dev->SetVertexShader(nullptr);
        }
        // 14) a value uploaded once and read by a later draw (like MHF's shadow lookups and light colours): on even
        //     ticks an unrelated draw (which never exists two ticks in a row, so it is never matched) comes first
        if (vsTint && vs && liveTest) {
            float m[16] = {0.06f, 0, 0, -0.89f, 0, 0.075f, 0, 0.92f, 0, 0, 1, 0.5f, 0, 0, 0, 1};
            float v = 0.3f + 0.012f * (float)(tick % 50);
            float tint[4] = {v, v, v, 1};
            dev->SetVertexDeclaration(decl);
            dev->SetVertexShaderConstantF(4, tint, 1);
            V q6[6] = {{-1, -1, 0, 0xffffffff}, {1, -1, 0, 0xffffffff}, {-1, 1, 0, 0xffffffff},
                       {1, -1, 0, 0xffffffff}, {1, 1, 0, 0xffffffff}, {-1, 1, 0, 0xffffffff}};
            if (tick % 2 == 0) {
                float m2[16] = {0.04f, 0, 0, 0.5f, 0, 0.05f, 0, 0.9f, 0, 0, 1, 0.5f, 0, 0, 0, 1};
                dev->SetVertexShader(vs);
                dev->SetVertexShaderConstantF(0, m2, 4);
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q6, sizeof(V));
            }
            dev->SetVertexShader(vsTint);
            dev->SetVertexShaderConstantF(0, m, 4);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q6, sizeof(V));
            dev->SetVertexShader(nullptr);
        }
        // 6) camera-only objects (top right): a rotating "camera" matrix shared by three static objects drawn with
        //    their own combined matrix; the third exists only on even ticks (culled in/out) and must still follow
        //    the camera in the in-between frames
        if (vs) {
            float ca = tick * 0.1f, cc = cosf(ca), cs = sinf(ca);
            for (int o = 0; o < 3; o++) {
                if (o == 2 && (tick & 1)) continue;
                float ox = 0.15f * o;  // static world offset per object
                // combined matrix = camera rotation * translation(ox) * scale, centre (0.6, 0.6)
                float m[16] = {cc * 0.08f, -cs * 0.08f, 0, 0.6f + cc * ox, cs * 0.08f, cc * 0.08f, 0, 0.6f + cs * ox, 0, 0, 1, 0.5f, 0, 0, 0, 1};
                dev->SetVertexDeclaration(decl);
                dev->SetVertexShader(vs);
                dev->SetVertexShaderConstantF(0, m, 4);
                // distinct meshes (different triangle counts), like separate objects in the game
                V q3[12];
                for (int k = 0; k < 4; k++) {
                    q3[k * 3 + 0] = {0, 1, 0, 0xff00ffff};
                    q3[k * 3 + 1] = {-0.87f, -0.5f, 0, 0xff00ffff};
                    q3[k * 3 + 2] = {0.87f, -0.5f, 0, 0xff00ffff};
                }
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, o + 2, q3, sizeof(V));
            }
            dev->SetVertexShader(nullptr);
        }
        // 2) fixed-function SetTransform quad (right)
        D3DMATRIX w{};
        float c = cosf(-a), s = sinf(-a);
        w._11 = c * 0.3f; w._12 = s * 0.3f; w._21 = -s * 0.3f; w._22 = c * 0.3f; w._33 = 1; w._44 = 1; w._41 = 0.5f; w._42 = 0.3f; w._43 = 0.5f;
        D3DMATRIX id{}; id._11 = id._22 = id._33 = id._44 = 1;
        dev->SetTransform(D3DTS_WORLD, &w);
        dev->SetTransform(D3DTS_VIEW, &id);
        dev->SetTransform(D3DTS_PROJECTION, &id);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        V q[4] = {{-1, -1, 0, 0xffffff00}, {1, -1, 0, 0xffffff00}, {-1, 1, 0, 0xff00ffff}, {1, 1, 0, 0xff00ffff}};
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        // 3) pre-transformed UP quad moving horizontally (bottom)
        float x = 100 + fmodf(tick * 12.0f, 600.0f);
        VT u[4] = {{x, 450, 0, 1, 0xffffffff}, {x + 60, 450, 0, 1, 0xffffffff}, {x, 510, 0, 1, 0xffff8000}, {x + 60, 510, 0, 1, 0xffff8000}};
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, u, sizeof(VT));
        // 4) a batch of 12 small screen quads rebuilt in a different order every tick (like distance-sorted
        //    grass/flowers): must NOT be interpolated, or the quads smear into streaks
        VT batch[12 * 6];
        for (int i = 0; i < 12; i++) {
            int slot = (i * 5 + tick) % 12;  // rotate the order each tick
            float bx = 80.0f + slot * 55.0f, by = 40.0f;
            VT q6[6] = {{bx, by, 0, 1, 0xff80ff80}, {bx + 20, by, 0, 1, 0xff80ff80}, {bx, by + 20, 0, 1, 0xff80ff80},
                        {bx + 20, by, 0, 1, 0xff80ff80}, {bx + 20, by + 20, 0, 1, 0xff80ff80}, {bx, by + 20, 0, 1, 0xff80ff80}};
            memcpy(batch + i * 6, q6, sizeof q6);
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 24, batch, sizeof(VT));
        dev->EndScene();
        // limiter, same shape as MHF's
        DWORD now = timeGetTime();
        int wait = (int)(last + 33 - now);
        if (wait > 0) Sleep(wait);
        last = timeGetTime();
        dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    printf("ticks: %d in %d s (%.1f Hz)\n", tick, seconds, tick / (double)seconds);
    fflush(stdout);  // (exit markers: a hung run's output shows how far it got)
    dev->Release();
    printf("device released\n");
    fflush(stdout);
    d3d->Release();
    printf("exiting\n");
    fflush(stdout);
    return 0;
}
