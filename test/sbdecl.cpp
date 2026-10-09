// Does a D3DSBT_ALL state block restore the vertex declaration / FVF / shaders it captured? (Windows' d3d9 vs DXVK)
//   sbdecl.exe [d3d9.dll path|-]
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

int main(int argc, char** argv) {
    const char* dll = argc > 1 && strcmp(argv[1], "-") ? argv[1] : "d3d9.dll";
    HMODULE m = LoadLibraryA(dll);
    auto create = (IDirect3D9*(WINAPI*)(UINT))GetProcAddress(m, "Direct3DCreate9");
    IDirect3D9* d3d = create(D3D_SDK_VERSION);
    HWND w = CreateWindowA("STATIC", "sbdecl", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, 0, 0, 0, 0);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = w;
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) return 1;
    D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    IDirect3DVertexDeclaration9* decl;
    dev->CreateVertexDeclaration(el, &decl);
    const DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
    IDirect3DStateBlock9* sb;
    dev->CreateStateBlock(D3DSBT_ALL, &sb);
    for (int c = 0; c < 2; c++) {
        // captured: c == 0 the declaration, c == 1 the FVF; then the other one is set, then the block applied
        if (c == 0) dev->SetVertexDeclaration(decl); else dev->SetFVF(fvf);
        sb->Capture();
        if (c == 0) dev->SetFVF(fvf); else dev->SetVertexDeclaration(decl);
        sb->Apply();
        IDirect3DVertexDeclaration9* cur = nullptr;
        DWORD curFvf = 0;
        dev->GetVertexDeclaration(&cur);
        dev->GetFVF(&curFvf);
        printf("%s: captured %s -> after apply: declaration %s, FVF %08lx\n", dll, c == 0 ? "declaration" : "FVF",
               cur == decl ? "= the captured one" : cur ? "another (FVF's)" : "none", curFvf);
        if (cur) cur->Release();
    }
    // vertex shader + constants: captured with no shader, then a shader + constants set, then the block applied
    typedef HRESULT(WINAPI * AsmFn)(LPCSTR, UINT, const void*, void*, DWORD, void**, void**);
    HMODULE dx = LoadLibraryA("d3dx9_43.dll");
    AsmFn as = dx ? (AsmFn)GetProcAddress(dx, "D3DXAssembleShader") : nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    const char* src = "vs_2_0\ndcl_position v0\nmov oPos, v0\n";
    void* code = nullptr;
    if (as && SUCCEEDED(as(src, (UINT)strlen(src), nullptr, nullptr, 0, &code, nullptr)) && code) {
        auto getPtr = (void*(WINAPI*)(IUnknown*))((*(void***)code)[3]);
        dev->CreateVertexShader((const DWORD*)getPtr((IUnknown*)code), &vs);
    }
    for (int c = 0; c < 2; c++) {
        float a[4] = {1, 2, 3, 4}, b[4] = {9, 9, 9, 9}, r[4] = {};
        dev->SetVertexShader(c == 0 ? nullptr : vs);
        dev->SetVertexShaderConstantF(1, a, 1);
        sb->Capture();
        dev->SetVertexShader(c == 0 ? vs : nullptr);
        dev->SetVertexShaderConstantF(1, b, 1);
        sb->Apply();
        IDirect3DVertexShader9* cur = nullptr;
        dev->GetVertexShader(&cur);
        dev->GetVertexShaderConstantF(1, r, 1);
        printf("%s: captured shader %s, c1=1 -> after apply: shader %s, c1=%g\n", dll, c == 0 ? "none" : "vs",
               cur == (c == 0 ? nullptr : vs) ? "restored" : "NOT restored", r[0]);
        if (cur) cur->Release();
    }
    return 0;
}
