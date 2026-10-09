"""Find IDirect3DDevice9 vtable calls: mov r,[r+disp]; call r   or   call [r+disp]  near loads of the device global."""
import re, sys, struct
from collections import Counter, defaultdict
from mh import *

DEV = 0x1e811a3c
NAMES = {0x44: "Present", 0xa4: "BeginScene", 0xa8: "EndScene", 0xac: "Clear", 0xb0: "SetTransform",
         0x144: "DrawPrimitive", 0x148: "DrawIndexedPrimitive", 0x14c: "DrawPrimitiveUP",
         0x150: "DrawIndexedPrimitiveUP", 0x178: "SetVertexShaderConstantF", 0x15c: "SetVertexDeclaration",
         0x170: "SetVertexShader", 0x1ac: "SetPixelShader", 0x1b4: "SetPixelShaderConstantF",
         0x190: "SetStreamSource", 0x1a0: "SetIndices", 0x104: "SetTexture", 0xe4: "SetRenderState",
         0x94: "SetRenderTarget", 0x9c: "SetDepthStencilSurface", 0xbc: "SetViewport", 0x114: "SetSamplerState",
         0x88: "StretchRect", 0x5c: "CreateTexture", 0x68: "CreateVertexBuffer", 0x6c: "CreateIndexBuffer",
         0x10c: "SetTextureStageState", 0x64: "CreateCubeTexture", 0x74: "CreateDepthStencilSurface", 0x70: "CreateRenderTarget",
         0x48: "GetBackBuffer", 0x40: "Reset", 0xc0: "SetMaterial", 0x164: "SetFVF"}

refs = list(find_all(struct.pack("<I", DEV)))
print("refs to device global:", len(refs))
funcs = defaultdict(Counter)
for r in refs:
    # disassemble a window after the reference
    for i in md.disasm(img.read(r - 2, 0x60), r - 2):
        m = re.match(r"(?:dword ptr )?\[(e\w\w) \+ (0x[0-9a-f]+)\]", i.op_str.split(", ")[-1])
        if i.mnemonic in ("mov", "call") and m:
            off = int(m.group(2), 16)
            if off in NAMES and 0x40 <= off <= 0x1c0:
                f = func_start(r) or 0
                funcs[f][NAMES[off]] += 1
                break
for f, c in sorted(funcs.items()):
    if any(k in c for k in ("BeginScene", "EndScene", "DrawIndexedPrimitive", "DrawPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP", "Present", "SetVertexShaderConstantF")):
        print(hex(f), dict(c))
