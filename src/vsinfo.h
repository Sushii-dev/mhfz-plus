// Vertex shader bytecode analysis: which float constant registers can influence the output position.
// Only those get interpolated; constants that only feed texcoords / colours (UV scroll, flipbook frames,
// fades) must snap, or animated effects break.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

struct Mask256 {
    uint64_t b[4] = {0, 0, 0, 0};
    void set(int r) { if (r >= 0 && r < 256) b[r >> 6] |= 1ull << (r & 63); }
    void reset(int r) { if (r >= 0 && r < 256) b[r >> 6] &= ~(1ull << (r & 63)); }
    bool test(int r) const { return r >= 0 && r < 256 && ((b[r >> 6] >> (r & 63)) & 1); }
    void setFrom(int r) { for (; r < 256; r++) set(r); }
    void orr(const Mask256& o) { for (int i = 0; i < 4; i++) b[i] |= o.b[i]; }
    bool any() const { return b[0] | b[1] | b[2] | b[3]; }
    void clear() { b[0] = b[1] = b[2] = b[3] = 0; }
};

struct VSInfo {
    Mask256 pos;     // constants that reach the position output
    Mask256 reads;   // every constant the shader reads at all
    bool all = true; // analysis failed: treat every constant as positional (and read)
    int major = 0;
    int relBase = 256, relMax = -1;  // constants read through an index register (bone palettes): c[a0 + relBase..relMax]
};

static inline int VsRegType(DWORD t) { return (int)(((t >> 28) & 7) | ((t & 0x1800) >> 8)); }

// Pixel shaders: only which float constants they read (ps_2_0 and up; ps_1_x reads are not analysed).
struct PSInfo {
    Mask256 reads;
    bool all = true;
};
static void AnalyzePS(const DWORD* code, PSInfo& pi) {
    pi = PSInfo();
    if (!code || (code[0] & 0xFFFF0000) != 0xFFFF0000) return;
    int major = (code[0] >> 8) & 0xFF;
    if (major < 2) return;
    Mask256 reads;
    const DWORD* p = code + 1;
    for (int guard = 0; guard < 100000; guard++) {
        DWORD tok = *p;
        if (tok == 0x0000FFFF) {
            pi.reads = reads;
            pi.all = false;
            return;
        }
        DWORD op = tok & 0xFFFF;
        if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }  // comment
        int len = (int)((tok >> 24) & 0x0F);
        const DWORD* a = p + 1;
        p += 1 + len;
        if (op == 31 || op == 81 || op == 82 || op == 83) continue;  // dcl, def*
        int rows = op == 20 || op == 22 ? 4 : op == 21 || op == 23 ? 3 : op == 24 ? 2 : 1;  // m4x4 m3x4 / m4x3 m3x3 / m3x2
        for (int i = 0, si = -1; i < len; i++, si++) {  // si: source index (operand 0 is the destination)
            DWORD s = a[i];
            int type = VsRegType(s);
            int num = (int)(s & 0x7FF);
            bool rel = (s >> 13) & 1;
            if (type == 2) {
                int span = si == 1 ? rows : 1;
                if (rel) reads.setFrom(num);
                else for (int k = 0; k < span; k++) reads.set(num + k);
            }
            if (rel) i++;
        }
    }
}

// Where a pixel shader samples a texture, and what decides it (shadow map reuse in in-between frames: the
// constants that place the shadow lookup must keep the real frame's values). Semantics as (usage << 4 | index),
// usage as in D3DDECLUSAGE: sm2 outputs oT# = TEXCOORD#, oD# = COLOR#; sm3 by their dcl.
static inline int Sem(int usage, int index) { return (usage & 15) << 4 | (index & 15); }
// vertex shader: the constants reaching each output semantic
struct VsOutInfo {
    bool ok = false;
    int n = 0;
    uint8_t sem[16];
    Mask256 c[16];
    const Mask256* find(int s) const {
        for (int i = 0; i < n; i++)
            if (sem[i] == s) return &c[i];
        return nullptr;
    }
};
// pixel shader: per sampler, the input semantics and pixel shader constants its coordinates come from
struct PsLookInfo {
    bool ok = false;
    Mask256 in[16], c[16];
};
static void AnalyzeVsOut(const DWORD* code, VsOutInfo& vo) {
    vo = VsOutInfo();
    if (!code || (code[0] & 0xFFFF0000) != 0xFFFE0000) return;
    int major = (code[0] >> 8) & 0xFF;
    if (major < 2) return;  // (vs_1_x: not analysed)
    Mask256 temp[32], addr, pred, loop;
    int outSem[16];
    for (int& s : outSem) s = -1;
    auto out = [&](int sem) -> Mask256* {
        if (sem < 0) return nullptr;
        for (int i = 0; i < vo.n; i++)
            if (vo.sem[i] == sem) return &vo.c[i];
        if (vo.n == 16) return nullptr;
        vo.sem[vo.n] = (uint8_t)sem;
        vo.c[vo.n].clear();
        return &vo.c[vo.n++];
    };
    for (int pass = 0; pass < 2; pass++) {  // two passes: loop-carried temps
        const DWORD* p = code + 1;
        for (int guard = 0;; guard++) {
            if (guard >= 100000) return;
            DWORD tok = *p;
            if (tok == 0x0000FFFF) break;
            DWORD op = tok & 0xFFFF;
            if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }
            int len = (int)((tok >> 24) & 0x0F);
            const DWORD* a = p + 1;
            p += 1 + len;
            if (op == 31) {  // dcl: sm3 output semantics
                if (major >= 3 && len >= 2 && VsRegType(a[1]) == 6) outSem[a[1] & 15] = Sem((int)(a[0] & 0x1F), (int)((a[0] >> 16) & 0xF));
                continue;
            }
            if (op == 81 || op == 82 || op == 83 || len == 0) continue;
            bool hasDest = !(op == 40 || op == 41 || op == 42 || op == 43 || op == 44 || op == 45 || op == 27 ||
                             op == 29 || op == 38 || op == 39 || op == 25 || op == 26 || op == 30 || op == 28 || op == 96);
            int rows = op == 20 || op == 22 ? 4 : op == 21 || op == 23 ? 3 : op == 24 ? 2 : 1;
            Mask256 src;
            int si = 0;
            int first = hasDest ? 1 : 0;
            if (hasDest && ((a[0] >> 13) & 1)) first = 2;
            for (int i = first; i < len; i++, si++) {
                DWORD s = a[i];
                int type = VsRegType(s);
                int num = (int)(s & 0x7FF);
                bool rel = (s >> 13) & 1;
                int span = si == 1 ? rows : 1;
                if (type == 2) {
                    if (rel) { src.setFrom(num); src.orr(addr); src.orr(loop); }
                    else for (int k = 0; k < span; k++) src.set(num + k);
                } else if (type == 0) {
                    for (int k = 0; k < span; k++) src.orr(temp[(num + k) & 31]);
                } else if (type == 3) src.orr(addr);
                else if (type == 19) src.orr(pred);
                else if (type == 15) src.orr(loop);
                if (rel) i++;
            }
            if (!hasDest) {
                if (op == 27 || op == 38) loop.orr(src);
                continue;
            }
            DWORD d = a[0];
            int type = VsRegType(d);
            int num = (int)(d & 0x7FF);
            bool full = ((d >> 16) & 0xF) == 0xF;
            Mask256* o = nullptr;
            if (type == 0) { if (full) temp[num & 31] = src; else temp[num & 31].orr(src); }
            else if (type == 3) addr = src;
            else if (type == 19) pred = src;
            else if (type == 6) o = out(major >= 3 ? outSem[num & 15] : Sem(5, num));  // oT# / o#
            else if (type == 5) o = out(Sem(10, num));                               // oD#
            if (o) o->orr(src);
        }
    }
    vo.ok = true;
}
static void AnalyzePsLook(const DWORD* code, PsLookInfo& pl) {
    pl = PsLookInfo();
    if (!code || (code[0] & 0xFFFF0000) != 0xFFFF0000) return;
    int major = (code[0] >> 8) & 0xFF;
    if (major < 2) return;  // (ps_1_x: not analysed)
    struct Dep { Mask256 in, c; void orr(const Dep& o) { in.orr(o.in); c.orr(o.c); } };
    Dep temp[32], pred;
    int inSem[16];
    for (int i = 0; i < 16; i++) inSem[i] = major >= 3 ? -1 : Sem(10, i);  // ps_2: v# = COLOR#
    for (int pass = 0; pass < 2; pass++) {
        const DWORD* p = code + 1;
        for (int guard = 0;; guard++) {
            if (guard >= 100000) return;
            DWORD tok = *p;
            if (tok == 0x0000FFFF) break;
            DWORD op = tok & 0xFFFF;
            if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }
            int len = (int)((tok >> 24) & 0x0F);
            const DWORD* a = p + 1;
            p += 1 + len;
            if (op == 31) {  // dcl: sm3 input semantics
                if (major >= 3 && len >= 2 && VsRegType(a[1]) == 1) inSem[a[1] & 15] = Sem((int)(a[0] & 0x1F), (int)((a[0] >> 16) & 0xF));
                continue;
            }
            if (op == 81 || op == 82 || op == 83 || len == 0) continue;
            bool hasDest = !(op == 40 || op == 41 || op == 42 || op == 43 || op == 44 || op == 45 || op == 27 ||
                             op == 29 || op == 38 || op == 39 || op == 25 || op == 26 || op == 30 || op == 28 || op == 96 || op == 65);
            int rows = op == 20 || op == 22 ? 4 : op == 21 || op == 23 ? 3 : op == 24 ? 2 : 1;
            bool tex = op == 66 || op == 93 || op == 95;  // texld (texldp/texldb), texldd, texldl: dst, coord, sampler
            Dep src, coord;
            int si = 0, first = hasDest ? 1 : 0, sampler = -1;
            for (int i = first; i < len; i++, si++) {
                DWORD s = a[i];
                int type = VsRegType(s);
                int num = (int)(s & 0x7FF);
                bool rel = (s >> 13) & 1;
                int span = si == 1 ? rows : 1;
                Dep d;
                if (type == 2) {
                    if (rel) d.c.setFrom(num);
                    else for (int k = 0; k < span; k++) d.c.set(num + k);
                } else if (type == 0) {
                    for (int k = 0; k < span; k++) d.orr(temp[(num + k) & 31]);
                } else if (type == 3) d.in.set(Sem(5, num));  // t# = TEXCOORD#
                else if (type == 1) { if (inSem[num & 15] >= 0) d.in.set(inSem[num & 15]); else d.in.setFrom(0); }
                else if (type == 19) d.orr(pred);
                else if (type == 10) sampler = num;
                if (tex && si == 0) coord.orr(d);
                src.orr(d);
                if (rel) i++;
            }
            if (tex && sampler >= 0 && sampler < 16) {
                pl.in[sampler].orr(coord.in);
                pl.c[sampler].orr(coord.c);
            }
            if (!hasDest) continue;
            DWORD d = a[0];
            int type = VsRegType(d);
            int num = (int)(d & 0x7FF);
            bool full = ((d >> 16) & 0xF) == 0xF;
            if (type == 0) { if (full) temp[num & 31] = src; else temp[num & 31].orr(src); }
            else if (type == 19) pred = src;
        }
    }
    pl.ok = true;
}

// sm1 parameter counts (sm2+ encodes the length in the opcode token)
static int Sm1Params(DWORD op) {
    switch (op) {
    case 0: return 0;                                   // nop
    case 1: case 6: case 7: case 14: case 15: case 16: case 19: case 78: case 79: return 2;
    case 2: case 3: case 5: case 8: case 9: case 10: case 11: case 12: case 13: case 17:
    case 20: case 21: case 22: case 23: case 24: return 3;
    case 4: case 18: return 4;                          // mad, lrp
    case 31: return 2;                                  // dcl
    case 81: return 5;                                  // def
    default: return -1;
    }
}

static void AnalyzeVS(const DWORD* code, VSInfo& vi) {
    vi = VSInfo();
    if (!code || (code[0] & 0xFFFF0000) != 0xFFFE0000) return;
    int major = (code[0] >> 8) & 0xFF;
    vi.major = major;
    Mask256 temp[32], addr, pred, loop, pos, reads;
    int outPos = -1;
    for (int pass = 0; pass < 2; pass++) {  // two passes: loop-carried temps
        const DWORD* p = code + 1;
        for (int guard = 0; guard < 100000; guard++) {
            DWORD tok = *p;
            if (tok == 0x0000FFFF) break;
            DWORD op = tok & 0xFFFF;
            if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }  // comment
            int len = major >= 2 ? (int)((tok >> 24) & 0x0F) : Sm1Params(op);
            if (len < 0) return;  // unknown sm1 opcode -> fallback "all"
            const DWORD* a = p + 1;
            p += 1 + len;
            if (op == 31) {  // dcl
                if (major >= 3 && len >= 2 && VsRegType(a[1]) == 6 && (a[0] & 0x1F) == 0 && ((a[0] >> 16) & 0xF) == 0)
                    outPos = (int)(a[1] & 0x7FF);
                continue;
            }
            if (op == 81 || op == 82 || op == 83 || len == 0) continue;  // def*, no-operand ops
            bool hasDest = !(op == 40 || op == 41 || op == 42 || op == 43 || op == 44 || op == 45 || op == 27 ||
                             op == 29 || op == 38 || op == 39 || op == 25 || op == 26 || op == 30 || op == 28 || op == 96);
            int rows = op == 20 || op == 22 ? 4 : op == 21 || op == 23 ? 3 : op == 24 ? 2 : 1;  // m4x4 m3x4 / m4x3 m3x3 / m3x2
            Mask256 src;
            int si = 0;
            int first = hasDest ? 1 : 0;
            if (hasDest && major >= 2 && ((a[0] >> 13) & 1)) first = 2;  // relative dest carries an extra token
            for (int i = first; i < len; i++, si++) {
                DWORD s = a[i];
                int type = VsRegType(s);
                int num = (int)(s & 0x7FF);
                bool rel = (s >> 13) & 1;
                int span = (si == 1) ? rows : 1;  // matrix ops read rows from the 2nd source
                if (type == 2) {                   // c#
                    if (rel) {
                        src.setFrom(num); src.orr(addr); src.orr(loop);
                        vi.relBase = std::min(vi.relBase, num);
                        vi.relMax = std::max(vi.relMax, num + span - 1);
                        reads.setFrom(num);
                    }
                    else for (int k = 0; k < span; k++) { src.set(num + k); reads.set(num + k); }
                } else if (type == 0) {
                    for (int k = 0; k < span; k++) src.orr(temp[(num + k) & 31]);
                } else if (type == 3) src.orr(addr);
                else if (type == 19) src.orr(pred);
                else if (type == 15) src.orr(loop);
                if (rel && major >= 2) i++;  // relative-address token follows
            }
            if (!hasDest) {  // loop/rep/if read their operands into control flow; conservatively feed loop counter
                if (op == 27 || op == 38) loop.orr(src);
                continue;
            }
            DWORD d = a[0];
            int type = VsRegType(d);
            int num = (int)(d & 0x7FF);
            bool full = ((d >> 16) & 0xF) == 0xF;
            if (type == 0) { if (full) temp[num & 31] = src; else temp[num & 31].orr(src); }
            else if (type == 3) addr = src;
            else if (type == 19) pred = src;
            else if (type == 4 && num == 0) pos.orr(src);              // oPos (sm1/sm2)
            else if (type == 6 && major >= 3 && num == outPos) pos.orr(src);
        }
    }
    int rb = vi.relBase, rm = vi.relMax;
    vi = VSInfo();
    vi.major = major;
    vi.relBase = rb;
    vi.relMax = rm;
    vi.pos = pos;
    vi.reads = reads;
    vi.all = false;
}
