// Offline pairing bench: runs the mod's BuildPairs on pairing captures from the game (paircap_trigger in
// mhfz-fps.ini writes paircap\cap_<tick>.bin) and prints the time per phase and the pairing hash. An optimisation
// must leave every hash unchanged; the hash also equals the game's own "pairhash" log line for that tick.
//
//   build:  build.ps1 -Bench   (-Bench -Profile: with the sampling profiler, build\prof\pairbench.exe + map for tools\prof.py)
//   run:    build\pairbench.exe [-n 30] [-t threads] cap_*.bin
#define PAIRBENCH
#include "../src/fps.cpp"
#include <map>

extern "C" {
void* g_real[32];
void LoadRealD3D9() {}
char g_d3d9Backend[MAX_PATH];
wchar_t g_d3d9BackendW[MAX_PATH];
HMODULE g_realD3D9;
bool UseSystemD3D9() { return false; }
}

struct Reader {
    std::vector<uint8_t> d;
    size_t p = 0;
    bool ok = true;
    uint32_t u32() {
        if (p + 4 > d.size()) { ok = false; return 0; }
        uint32_t v;
        memcpy(&v, d.data() + p, 4);
        p += 4;
        return v;
    }
    void raw(void* out, size_t n) {
        if (p + n > d.size()) { ok = false; return; }
        memcpy(out, d.data() + p, n);
        p += n;
    }
};

static bool LoadCapture(const char* path) {
    Reader r;
    FILE* fp = fopen(path, "rb");
    if (!fp) { printf("%s: cannot open\n", path); return false; }
    fseek(fp, 0, SEEK_END);
    r.d.resize((size_t)ftell(fp));
    fseek(fp, 0, SEEK_SET);
    fread(r.d.data(), 1, r.d.size(), fp);
    fclose(fp);
    char magic[4];
    r.raw(magic, 4);
    if (memcmp(magic, "MHPC", 4) != 0 || r.u32() != 1) { printf("%s: not a pairing capture\n", path); return false; }
    uint32_t cs = r.u32();
    std::vector<uint8_t> cfg(cs);
    r.raw(cfg.data(), cs);
    if (cs == sizeof(Config)) memcpy(&C, cfg.data(), cs);
    else { C = Config(); C.pair_threads = 0; }  // (another layout: the current defaults, not misread fields)
    g_vsInfo.clear();
    uint32_t n = r.u32(), sz = r.u32();
    if (sz != sizeof(VSInfo)) { printf("%s: VSInfo size %u != %zu\n", path, sz, sizeof(VSInfo)); return false; }
    for (uint32_t k = 0; k < n && r.ok; k++) { void* key = (void*)(uintptr_t)r.u32(); r.raw(&g_vsInfo[key], sizeof(VSInfo)); }
    g_psInfo.clear();
    n = r.u32();
    sz = r.u32();
    if (sz != sizeof(PSEntry)) { printf("%s: PSEntry size %u != %zu\n", path, sz, sizeof(PSEntry)); return false; }
    for (uint32_t k = 0; k < n && r.ok; k++) { void* key = (void*)(uintptr_t)r.u32(); r.raw(&g_psInfo[key], sizeof(PSEntry)); }
    if (r.u32() != sizeof(DrawRec)) { printf("%s: DrawRec size differs\n", path); return false; }
    for (int f = 0; f < 3; f++) {  // P2, P, N -> g_fr[0..2]
        Frame& F = g_fr[f];
        F.refs.clear();
        F.serial = r.u32();
        F.valid = r.u32() != 0;
        F.nvscf = r.u32();
        F.startVS = (void*)(uintptr_t)r.u32();
        F.startPS = (void*)(uintptr_t)r.u32();
        F.buf.resize(r.u32());
        r.raw(F.buf.data(), F.buf.size());
        F.draws.resize(r.u32());
        r.raw(F.draws.data(), F.draws.size() * sizeof(DrawRec));
    }
    g_cur = 2;
    g_lastPairsCur = r.u32();
    g_lastPairsPrev = r.u32();
    g_lastPairs.clear();
    n = r.u32();
    g_lastPairs.Reset(n);
    for (uint32_t k = 0; k < n && r.ok; k++) { uint32_t a = r.u32(); g_lastPairs.Set(a, r.u32()); }
    if (!r.ok) printf("%s: truncated\n", path);
    return r.ok;
}

// -seq ticks.seq: the rolling recorder's last ticks (retro, saved with the Delete key), matched one after the other as
// the game did, with per-tick statistics; -detail adds, per tick, the constant registers that changed but were not
// blended, the uploads left without a counterpart, and the unmatched draws per render target.
static int g_benchNonpos = -1;
static std::vector<Frame> g_seq;
static std::vector<uint32_t> g_seqTick;
static bool LoadSequence(const char* path) {
    Reader r;
    FILE* fp = fopen(path, "rb");
    if (!fp) { printf("%s: cannot open\n", path); return false; }
    fseek(fp, 0, SEEK_END);
    r.d.resize((size_t)ftell(fp));
    fseek(fp, 0, SEEK_SET);
    fread(r.d.data(), 1, r.d.size(), fp);
    fclose(fp);
    char magic[4];
    r.raw(magic, 4);
    uint32_t ver = 0;
    if (memcmp(magic, "MHSQ", 4) != 0 || ((ver = r.u32()) != 1 && ver != 2)) { printf("%s: not a tick sequence\n", path); return false; }
    uint32_t cs = r.u32();
    std::vector<uint8_t> cfg(cs);
    r.raw(cfg.data(), cs);
    if (cs == sizeof(Config)) memcpy(&C, cfg.data(), cs);
    else { C = Config(); printf("%s: settings layout differs (%u bytes, bench %zu): current defaults used\n", path, cs, sizeof(Config)); }
    g_vsInfo.clear();
    uint32_t n = r.u32(), sz = r.u32();
    if (sz != sizeof(VSInfo)) { printf("%s: VSInfo size %u != %zu\n", path, sz, sizeof(VSInfo)); return false; }
    for (uint32_t k = 0; k < n && r.ok; k++) { void* key = (void*)(uintptr_t)r.u32(); r.raw(&g_vsInfo[key], sizeof(VSInfo)); }
    g_psInfo.clear();
    n = r.u32();
    sz = r.u32();
    if (sz != sizeof(PSEntry)) { printf("%s: PSEntry size %u != %zu\n", path, sz, sizeof(PSEntry)); return false; }
    for (uint32_t k = 0; k < n && r.ok; k++) { void* key = (void*)(uintptr_t)r.u32(); r.raw(&g_psInfo[key], sizeof(PSEntry)); }
    if (r.u32() != sizeof(DrawRec)) { printf("%s: DrawRec size differs\n", path); return false; }
    uint32_t count = r.u32();
    g_seq.assign(count, Frame());
    g_seqTick.assign(count, 0);
    for (uint32_t f = 0; f < count && r.ok; f++) {
        Frame& F = g_seq[f];
        g_seqTick[f] = r.u32();
        F.serial = r.u32();
        F.valid = true;
        F.nvscf = r.u32();
        F.startVS = (void*)(uintptr_t)r.u32();
        F.startPS = (void*)(uintptr_t)r.u32();
        F.buf.resize(r.u32());
        r.raw(F.buf.data(), F.buf.size());
        F.draws.resize(r.u32());
        r.raw(F.draws.data(), F.draws.size() * sizeof(DrawRec));
        if (ver >= 2) {  // (the glyph keys as the game made them: the atlas itself is not saved)
            F.ckeys.resize(r.u32());
            r.raw(F.ckeys.data(), F.ckeys.size() * 8);
            F.ckeysDone = !F.ckeys.empty() && F.ckeys.size() == F.draws.size();
        }
    }
    if (!r.ok) printf("%s: truncated\n", path);
    return r.ok;
}
static void SeqDetail(const Frame& N, const Frame& P) {
    // constant registers that changed between the paired uploads but are not blended (snap at the tick)
    std::map<std::pair<int, int>, int> snapped;  // (VS 0 / PS 1, register) -> count
    std::vector<char> paired(N.buf.size() / 4 + 1, 0);
    for (const Pair& p : g_pairs) {
        paired[p.cur >> 2] = 1;
        const Cmd* c = (const Cmd*)(N.buf.data() + p.cur);
        if ((c->op != OP_VSCF && c->op != OP_PSCF) || p.prev == NONE || p.synth != NONE) continue;
        auto* q = (const PConst*)(c + 1);
        const float* v = (const float*)(q + 1);
        const float* a = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + p.prev) + 1) + 1);
        for (UINT r = 0; r < q->count; r++) {
            if (!memcmp(v + r * 4, a + r * 4, 16)) continue;
            bool lerped = p.mi != NONE && g_pairMask[p.mi].test((int)r);
            if (!lerped) snapped[{c->op == OP_PSCF, (int)(q->start + r)}]++;
        }
    }
    // uploads with no counterpart at all (kept at the new value); large ones (bone palettes) separately, with whether an
    // identical upload elsewhere in the frame (the same character in another pass) has one
    std::map<std::pair<int, int>, int> lone;
    std::unordered_map<uint64_t, int> pairedBig;  // content hash of paired uploads of 12+ registers
    for (const Pair& p : g_pairs) {
        const Cmd* c = (const Cmd*)(N.buf.data() + p.cur);
        if (c->op != OP_VSCF || ((const PConst*)(c + 1))->count < 12) continue;
        auto* q = (const PConst*)(c + 1);
        uint64_t h = 1469598103934665603ull;
        const uint32_t* w = (const uint32_t*)(q + 1);
        for (UINT k = 0; k < q->count * 4; k++) h = (h ^ w[k]) * 1099511628211ull;
        pairedBig[h ^ q->start]++;
    }
    auto content = [](const Cmd* c) {
        auto* q = (const PConst*)(c + 1);
        uint64_t h = 1469598103934665603ull ^ ((uint64_t)c->op << 40) ^ ((uint64_t)q->start << 20) ^ q->count;
        const uint32_t* w = (const uint32_t*)(q + 1);
        for (UINT k = 0; k < q->count * 4; k++) h = (h ^ w[k]) * 1099511628211ull;
        return h;
    };
    std::unordered_map<uint64_t, int> inP;  // the previous frame's uploads by content: an unpaired upload found there is unchanged
    for (uint32_t off = 0; off < P.buf.size();) {
        const Cmd* c = (const Cmd*)(P.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_VSCF || c->op == OP_PSCF) inP[content(c)]++;
        off += c->size;
    }
    int bigLone = 0, bigLoneTwin = 0, bigAll = 0, unchanged = 0;
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        if (!c->size) break;
        bool same = (c->op == OP_VSCF || c->op == OP_PSCF) && inP.count(content(c));
        if ((c->op == OP_VSCF || c->op == OP_PSCF) && !paired[off >> 2]) {
            if (same) unchanged++;
            else lone[{c->op == OP_PSCF, (int)((const PConst*)(c + 1))->start}]++;
        }
        if (c->op == OP_VSCF && ((const PConst*)(c + 1))->count >= 12) {
            auto* q = (const PConst*)(c + 1);
            bigAll++;
            if (!paired[off >> 2] && !same) {
                bigLone++;
                uint64_t h = 1469598103934665603ull;
                const uint32_t* w = (const uint32_t*)(q + 1);
                for (UINT k = 0; k < q->count * 4; k++) h = (h ^ w[k]) * 1099511628211ull;
                bigLoneTwin += pairedBig.count(h ^ q->start) > 0;
            }
        }
        off += c->size;
    }
    printf("    uploads of 12+ registers: %d, changed and without counterpart %d, of those with a paired identical twin %d | unpaired but unchanged uploads %d\n",
           bigAll, bigLone, bigLoneTwin, unchanged);
    std::map<void*, std::pair<int, int>> perRT;  // render target -> (draws, unmatched)
    for (size_t i = 0; i < N.draws.size(); i++) {
        auto& e = perRT[N.draws[i].rt];
        e.first++;
        e.second += g_match[i] < 0;
    }
    auto top = [](const std::map<std::pair<int, int>, int>& m, const char* what) {
        std::vector<std::pair<int, std::pair<int, int>>> v;
        for (auto& kv : m) v.push_back({kv.second, kv.first});
        std::sort(v.rbegin(), v.rend());
        printf("    %s:", what);
        for (size_t k = 0; k < v.size() && k < 24; k++) printf(" %s c%d x%d", v[k].second.first ? "PS" : "VS", v[k].second.second, v[k].first);
        printf("\n");
    };
    top(snapped, "changed but not blended (register x packets)");
    top(lone, "changed uploads without counterpart (start register x uploads)");
    printf("    unmatched draws per target:");
    for (auto& kv : perRT) if (kv.second.second) printf(" %08x %d/%d", (uint32_t)(uintptr_t)kv.first, kv.second.second, kv.second.first);
    printf("\n");
}
// Why changed uploads got no counterpart, per start register: no matched draw reads it / positional (OwnersA) and
// the counterpart draw owns no upload with the same start+count / the counterpart reads that start register but
// from an upload of another size / reads nothing there / other.
static void ExplainLone(const Frame& N, const Frame& P) {
    std::vector<char> paired(N.buf.size() / 4 + 1, 0);
    for (const Pair& p : g_pairs) paired[p.cur >> 2] = 1;
    std::unordered_map<uint64_t, int> inP;
    auto content = [](const Cmd* c) {
        auto* q = (const PConst*)(c + 1);
        uint64_t h = 1469598103934665603ull ^ ((uint64_t)c->op << 40) ^ ((uint64_t)q->start << 20) ^ q->count;
        const uint32_t* w = (const uint32_t*)(q + 1);
        for (UINT k = 0; k < q->count * 4; k++) h = (h ^ w[k]) * 1099511628211ull;
        return h;
    };
    for (uint32_t off = 0; off < P.buf.size();) {
        const Cmd* c = (const Cmd*)(P.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_VSCF || c->op == OP_PSCF) inP[content(c)]++;
        off += c->size;
    }
    std::unordered_map<uint32_t, std::vector<uint32_t>> readers;  // upload -> draws reading it (live and owner lists)
    for (uint32_t i = 0; i < N.draws.size(); i++) {
        for (uint32_t k = 0; k < g_liveN.count[i]; k++) readers[g_liveN.flat[g_liveN.begin[i] + k]].push_back(i);
        for (uint32_t k = 0; k < g_ownN.count[i]; k++) readers[g_ownN.flat[g_ownN.begin[i] + k]].push_back(i);
    }
    std::map<std::string, std::map<std::pair<int, int>, int>> why;
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        if (!c->size) break;
        uint32_t here = off;
        off += c->size;
        if ((c->op != OP_VSCF && c->op != OP_PSCF) || paired[here >> 2] || inP.count(content(c))) continue;
        auto* q = (const PConst*)(c + 1);
        std::pair<int, int> reg{c->op == OP_PSCF, (int)q->start};
        auto it = readers.find(here);
        if (it == readers.end()) { why["no draw reads it (overwritten first)"][reg]++; continue; }
        uint32_t i = NONE;
        for (uint32_t r : it->second) if (g_match[r] >= 0) { i = r; break; }
        if (i == NONE) { why["no reader matched"][reg]++; continue; }
        uint32_t j = (uint32_t)g_match[i];
        bool positional = c->op == OP_VSCF && g_pmask[q->ord].any();
        const Owners& lp = positional ? g_ownP : g_liveP;
        bool sameSlot = false, sameStart = false;
        for (uint32_t k = 0; k < lp.count[j]; k++) {
            const Cmd* pc = (const Cmd*)(P.buf.data() + lp.flat[lp.begin[j] + k]);
            auto* pq = (const PConst*)(pc + 1);
            if (pc->op != c->op) continue;
            if (pq->start == q->start && pq->count == q->count) sameSlot = true;
            else if (pq->start <= q->start && pq->start + pq->count > q->start) sameStart = true;
        }
        const char* w = sameSlot ? (positional ? "positional, counterpart has the slot (synth failed?)" : "counterpart has the slot (other reason)")
                        : sameStart ? (positional ? "positional, counterpart covers it with an upload of another range" : "counterpart covers it with an upload of another range")
                                    : (positional ? "positional, counterpart owns nothing there" : "counterpart reads nothing there");
        why[w][reg]++;
    }
    for (auto& kv : why) {
        int tot = 0;
        for (auto& r : kv.second) tot += r.second;
        printf("    %-70s %5d:", kv.first.c_str(), tot);
        std::vector<std::pair<int, std::pair<int, int>>> v;
        for (auto& r : kv.second) v.push_back({r.second, r.first});
        std::sort(v.rbegin(), v.rend());
        for (size_t k = 0; k < v.size() && k < 10; k++) printf(" %s c%d x%d", v[k].second.first ? "PS" : "VS", v[k].second.second, v[k].first);
        printf("\n");
    }
}
// -additive: the additively blended draws (DESTBLEND ONE: glows, flames, sparks) - matched or not, and for each the
// constant packets it reads that changed: paired and blended, paired but snapped, or without a counterpart
static void AdditiveReport(const Frame& N, const Frame& P) {
    std::vector<char> paired(N.buf.size() / 4 + 1, 0);
    std::unordered_map<uint32_t, const Pair*> pairOf;
    for (const Pair& p : g_pairs) { paired[p.cur >> 2] = 1; pairOf[p.cur] = &p; }
    DWORD dest = 0, src = 0;
    void* rt = nullptr;
    size_t di = 0;
    int nAdd = 0, nUnm = 0, nFlag = 0;
    int pk[4] = {};  // packets read: blended, snapped (paired, register not in mask), unpaired-changed, unchanged
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_RS) {
            auto* x = (const P2*)(c + 1);
            if (x->a == D3DRS_DESTBLEND) dest = x->b;
            if (x->a == D3DRS_SRCBLEND) src = x->b;
        } else if (c->op == OP_RT && ((const PPtr*)(c + 1))->a == 0) {
            rt = ((const PPtr*)(c + 1))->p;
        } else if (IsDraw(c->op)) {
            if (dest == D3DBLEND_ONE && di < N.draws.size()) {
                nAdd++;
                bool m = g_match[di] >= 0;
                nUnm += !m;
                nFlag += di < g_drawFlag.size() && g_drawFlag[di];
                // the packets this draw reads
                auto look = [&](const Owners& o) {
                    for (uint32_t k = 0; k < o.count[di]; k++) {
                        uint32_t po = o.flat[o.begin[di] + k];
                        auto it = pairOf.find(po);
                        if (it == pairOf.end()) { pk[3]++; continue; }  // (unpaired: unchanged or no counterpart - see -detail)
                        const Pair& pr = *it->second;
                        auto* q = (const PConst*)((const Cmd*)(N.buf.data() + po) + 1);
                        bool all = pr.mi != NONE;
                        for (UINT r = 0; r < q->count && all; r++) all = g_pairMask[pr.mi].test((int)r);
                        pk[all ? 0 : 1]++;
                        if (!all && pr.prev != NONE && pr.synth == NONE) {
                            const Cmd* pc = (const Cmd*)(P.buf.data() + pr.prev);
                            const float* a = (const float*)((const PConst*)(pc + 1) + 1);
                            const float* v = (const float*)(q + 1);
                            for (UINT r = 0; r < q->count; r++) {
                                if (pr.mi != NONE && g_pairMask[pr.mi].test((int)r)) continue;
                                if (!memcmp(a + r * 4, v + r * 4, 16)) continue;
                                printf("      draw %zu: %s c%u snapped: %.4g %.4g %.4g %.4g -> %.4g %.4g %.4g %.4g\n", di, kOp[((const Cmd*)(N.buf.data() + po))->op],
                                       q->start + r, a[r * 4], a[r * 4 + 1], a[r * 4 + 2], a[r * 4 + 3], v[r * 4], v[r * 4 + 1], v[r * 4 + 2], v[r * 4 + 3]);
                            }
                        }
                    }
                };
                look(g_ownN);
                look(g_liveN);
                if (nAdd <= 40)
                    printf("    additive draw %5zu (%s, rt %08x, vs %08x): %s%s, %u position packets, %u others\n", di, kOp[c->op], (uint32_t)(uintptr_t)rt,
                           (uint32_t)(uintptr_t)N.draws[di].vs, m ? "matched" : "UNMATCHED", di < g_drawFlag.size() && g_drawFlag[di] ? " NOT-LERPED" : "",
                           g_ownN.count[di], g_liveN.count[di]);
            }
            di++;
        }
        off += c->size;
    }
    printf("    additive draws: %d, unmatched %d, position not interpolated %d | packets read: fully blended %d, partly snapped %d, unpaired %d\n",
           nAdd, nUnm, nFlag, pk[0], pk[1], pk[3]);
}
// -campaths: the camera paths on real frames - per context the turn angle, and per pair following one where its
// origin lands mid-tick (t = 0.5) on the path versus the plain blend, in pixels of a 3440x1440 screen
static void CamPathReport(const Frame& N, const Frame& P) {
    PairCameras(N, P, 0.5f);
    int ctx = 0, solved = 0, paths = 0;
    for (auto& kv : g_cam) {
        ctx++;
        solved += kv.second.has;
        if (!kv.second.path) continue;
        paths++;
        const M4& D = kv.second.D;
        double cosA = (D.m[0][0] + D.m[1][1] + D.m[2][2] + D.m[3][3] - 2.0) * 0.5;
        printf("    context %016llx: turn %.2f deg, |D-I| %.3g\n", (unsigned long long)kv.first, std::acos(std::min(1.0, std::max(-1.0, cosA))) * 57.2958,
               NormInf(Comb(D, 1, Ident4(), -1)));
    }
    int n[3] = {}, bad = 0;
    double sum = 0, mx = 0;
    for (size_t i = 0; i < g_pairs.size(); i++) {
        if (!g_pairCam[i]) continue;
        n[g_pairCamMode[i]]++;
        const Pair& pr = g_pairs[i];
        auto* q = (const PConst*)((const Cmd*)(N.buf.data() + pr.cur) + 1);
        const float* v = (const float*)(q + 1);
        const float* a = pr.synth != NONE ? &g_synth[pr.synth] : (const float*)((const PConst*)((const Cmd*)(P.buf.data() + pr.prev) + 1) + 1);
        float path[16], lin[16];
        Mask256 all;
        for (int r = 0; r < 4; r++) all.set(r);
        LerpConsts(q, v, a, &all, nullptr, -1, 0.5f, path, false, g_pairCam[i], g_pairCamMode[i]);
        LerpConsts(q, v, a, &all, nullptr, -1, 0.5f, lin, false, nullptr, 0);
        auto scr = [](const float* m, double& x, double& y) {
            double w = m[15];
            if (std::fabs(w) < 1e-6) return false;
            x = (m[3] / w * 0.5 + 0.5) * 3440.0;
            y = (0.5 - m[7] / w * 0.5) * 1440.0;
            return w > 0;
        };
        double x0, y0, x1, y1;
        if (!scr(path, x0, y0) || !scr(lin, x1, y1)) continue;
        double d = std::sqrt((x0 - x1) * (x0 - x1) + (y0 - y1) * (y0 - y1));
        if (!std::isfinite(d) || d > 400) bad++;
        else { sum += d; mx = std::max(mx, d); }
    }
    printf("    %d contexts, %d estimated, %d with a path | pairs on a path: %d whole, %d cards | origin path vs blend: mean %.2f px, max %.2f px, %d odd\n",
           ctx, solved, paths, n[1], n[2], (n[1] + n[2] - bad) ? sum / (n[1] + n[2] - bad) : 0.0, mx, bad);
}
// -owners: the positional packets main-target draws read - per (start, count): how many uploads, how many draws read
// them, and how many readers the most-read upload has (a camera matrix uploaded once per pass: one upload, many readers)
static void OwnersReport(const Frame& N) {
    std::unordered_map<void*, int> perRT;
    for (const DrawRec& d : N.draws) perRT[d.rt]++;
    void* main = nullptr;
    int best = 0;
    for (auto& kv : perRT) if (kv.second > best) { best = kv.second; main = kv.first; }
    std::map<std::pair<UINT, UINT>, std::pair<int, int>> slot;  // (start, count) -> (uploads, reads)
    std::unordered_map<uint32_t, int> readsOf;
    for (size_t i = 0; i < N.draws.size(); i++) {
        if (N.draws[i].rt != main) continue;
        for (uint32_t k = 0; k < g_ownN.count[i]; k++) readsOf[g_ownN.flat[g_ownN.begin[i] + k]]++;
    }
    std::map<std::pair<UINT, UINT>, int> maxReads;
    for (auto& kv : readsOf) {
        auto* q = (const PConst*)((const Cmd*)(N.buf.data() + kv.first) + 1);
        auto& s = slot[{q->start, q->count}];
        s.first++;
        s.second += kv.second;
        maxReads[{q->start, q->count}] = std::max(maxReads[{q->start, q->count}], kv.second);
    }
    printf("    main target %08x (%d draws): positional packets by (start, count): uploads / reads / most reads of one upload\n", (uint32_t)(uintptr_t)main, best);
    for (auto& kv : slot)
        printf("      c%u x%u: %d / %d / %d\n", kv.first.first, kv.first.second, kv.second.first, kv.second.second, maxReads[kv.first]);
}
static int g_debugTextTick = -1;
static bool g_oldCands;
// The strings of N the text matcher left without a counterpart, and the strings of P laid out alike (same glyph
// count, height and spacing) near them - with how many of their glyphs share N's glyph keys (an atlas reshuffle
// changes the keys, not the layout)
static void TextOrphans(const Frame& N, const Frame& P) {
    std::vector<TextRun> rn, rp;
    std::vector<float> bn, bp;
    std::vector<uint32_t> cn, cp;
    BuildRuns(N, rn, bn, cn);
    BuildRuns(P, rp, bp, cp);
    int shown = 0;
    for (const TextRun& a : rn) {
        bool any = false;
        for (uint32_t m = 0; m < a.n; m++) any |= g_runMatch[a.first + m] != 0;
        if (any || shown >= 25) continue;
        shown++;
        const float* q0 = &bn[a.first * 4];
        printf("    orphan: %u glyphs at %.0f,%.0f (h %.0f, colour %08x), glyphs matched one by one: ", a.n, q0[0], q0[1], q0[3] - q0[1], cn[a.first]);
        int ones = 0;
        for (uint32_t m = 0; m < a.n; m++) ones += g_match[a.first + m] >= 0;
        printf("%d\n", ones);
        struct C2 { float d, dx, dy; int keys; };
        std::vector<C2> cs;
        for (const TextRun& b : rp) {
            if (b.n != a.n) continue;
            const float* r0 = &bp[b.first * 4];
            bool same = std::fabs((q0[3] - q0[1]) - (r0[3] - r0[1])) <= 1.5f;
            for (uint32_t m = 1; m < a.n && same; m++) {
                const float* q = &bn[(a.first + m) * 4];
                const float* r = &bp[(b.first + m) * 4];
                same = std::fabs((q[0] - q0[0]) - (r[0] - r0[0])) <= 1.5f && std::fabs((q[1] - q0[1]) - (r[1] - r0[1])) <= 1.5f;
            }
            if (!same) continue;
            int keys = 0;
            for (uint32_t m = 0; m < a.n; m++) keys += N.draws[a.first + m].key == P.draws[b.first + m].key;
            float dx = q0[0] - r0[0], dy = q0[1] - r0[1];
            cs.push_back({std::fabs(dx) + std::fabs(dy), dx, dy, keys});
        }
        std::sort(cs.begin(), cs.end(), [](const C2& x, const C2& y) { return x.d < y.d; });
        for (size_t k = 0; k < cs.size() && k < 3; k++) printf("      laid out alike in P: moved %.0f,%.0f, %d of %u glyph keys equal\n", cs[k].dx, cs[k].dy, cs[k].keys, a.n);
        if (cs.empty()) printf("      nothing laid out alike in P\n");
    }
}
// Blended constants that nearly cancel halfway: rows (4 floats) of a paired upload whose blend at t = 0.5 is much
// shorter than either end (a direction that turned round between the ticks: halfway it is about nothing)
static bool g_cancelScan;
static void CancelScan(const Frame& N, const Frame& P, uint32_t tick) {
    std::map<std::pair<int, UINT>, int> hits;  // (vs/ps, register) -> rows
    std::string ex;
    for (const Pair& pr : g_pairs) {
        if (pr.prev == NONE || pr.synth != NONE) continue;
        const Cmd* c = (const Cmd*)(N.buf.data() + pr.cur);
        if (c->op != OP_VSCF && c->op != OP_PSCF) continue;
        auto* q = (const PConst*)(c + 1);
        const float* a = (const float*)(q + 1);
        const float* b = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + pr.prev) + 1) + 1);
        for (UINT r = 0; r < q->count; r++) {
            const float* x = a + r * 4;
            const float* y = b + r * 4;
            double la = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]), lb = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
            double mx = 0.5 * (x[0] + y[0]), my = 0.5 * (x[1] + y[1]), mz = 0.5 * (x[2] + y[2]), lm = std::sqrt(mx * mx + my * my + mz * mz);
            if (la < 0.3 || lb < 0.3 || la > 3 || lb > 3 || lm > 0.35 * std::min(la, lb)) continue;
            hits[{c->op == OP_PSCF, q->start + r}]++;
            if (ex.size() < 400) {
                char s[200];
                snprintf(s, sizeof s, " | %s c%u: (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)", c->op == OP_PSCF ? "PS" : "VS", q->start + r, y[0], y[1], y[2], x[0], x[1], x[2]);
                ex += s;
            }
        }
    }
    printf("    near-cancelling halfway:");
    if (hits.empty()) printf(" none");
    for (auto& kv : hits) printf(" %s c%u x%d", kv.first.first ? "PS" : "VS", kv.first.second, kv.second);
    printf("%s\n", ex.c_str());
}
// Which draws of a frame sample a given texture (and which draw into it): its role in the frame
static uintptr_t g_texQuery;
static void TexQuery(const Frame& F, uint32_t tick) {
    void* tex[21] = {};
    memcpy(tex, F.startTex, sizeof tex);
    void* rt0 = F.rt[0];
    void *ps = F.startPS, *vs = F.startVS;
    uint32_t di = 0;
    int shown = 0, samples = 0, into = 0;
    printf("  tick %u: texture %08x\n", tick, (unsigned)g_texQuery);
    for (uint32_t off = 0; off < F.buf.size();) {
        const Cmd* c = (const Cmd*)(F.buf.data() + off);
        if (!c->size) break;
        switch (c->op) {
        case OP_RT: { auto* x = (const PPtr*)(c + 1); if (x->a == 0 && x->p) rt0 = x->p; break; }
        case OP_TEX: { auto* x = (const PPtr*)(c + 1); int s = SamplerSlot(x->a); if (s >= 0) tex[s] = x->p; break; }
        case OP_PS: ps = ((const PPtr*)(c + 1))->p; break;
        case OP_VS: vs = ((const PPtr*)(c + 1))->p; break;
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP: {
            uint16_t m = 0xFFFF;  // (any stage bound: the shader tables cannot be asked offline)
            bool drawsInto = false;  // (surfaces cannot be resolved to their textures offline)
            int st = -1;
            for (int s = 0; s < 16; s++) if (((m >> s) & 1) && (uintptr_t)tex[s] == g_texQuery) st = s;
            if (st >= 0 || drawsInto) {
                samples += st >= 0;
                into += drawsInto;
                if (shown++ < 14)
                    printf("    draw %u (%s): %s%s, target %p, vs %p ps %p\n", di, c->op == OP_DPUP || c->op == OP_DIPUP ? "UP" : "indexed", st >= 0 ? "samples it on stage " : "",
                           st >= 0 ? std::to_string(st).c_str() : "draws into it", rt0, vs, ps);
            }
            di++;
            break;
        }
        }
        off += c->size;
    }
    printf("    %d draws sample it, %d draw into it, of %u\n", samples, into, di);
}
// Screen quads of one tick that the matching moves (counterpart elsewhere): where from, where to, size, how matched
static int g_upTraceTick = -1;
static void UpTrace(const Frame& N, const Frame& P) {
    int shown = 0, moved = 0;
    for (uint32_t i = 0; i < N.draws.size(); i++) {
        float a[4], b[4];
        if (!QuadBox(N, i, a)) continue;
        int j = g_match[i];
        float dx = 0, dy = 0;
        const char* how = g_runMatch[i] == 1 ? "string" : g_runMatch[i] == 2 ? "moved with string/field" : g_runMatch[i] == 3 ? "pending" : j >= 0 ? "per quad" : "unmatched";
        if (j >= 0 && QuadBox(P, (uint32_t)j, b)) dx = a[0] - b[0], dy = a[1] - b[1];
        else if (g_runMatch[i] == 2) dx = -g_runSynth[i * 2], dy = -g_runSynth[i * 2 + 1];
        if (std::fabs(dx) + std::fabs(dy) <= 2.0f) continue;
        moved++;
        if (shown++ < 40)
            printf("    quad %u at %.0f,%.0f size %.0fx%.0f moved %.0f,%.0f (%s%s) key %016llx\n", i, a[0], a[1], a[2] - a[0], a[3] - a[1], dx, dy, how,
                   g_rejCur[i] ? ", refused per quad" : "", (unsigned long long)N.draws[i].key);
    }
    printf("    %d screen quads moved\n", moved);
}
// Blended constants of one tick at t = 0.33, 0.5, 0.67 (the mod's own blending, camera paths included): any whose
// value at 0.5 is not about halfway between the other two (a blend that misbehaves near the midpoint)
static int g_tScanTick = -1;
static void TScan(const Frame& N, const Frame& P) {
    const float ts[3] = {0.33f, 0.5f, 0.67f};
    std::vector<std::vector<float>> outs(3);
    std::vector<uint32_t> idx;
    for (size_t i = 0; i < g_pairs.size(); i++) {
        const Pair& pr = g_pairs[i];
        if (pr.upSynth != NONE || (pr.synth == NONE && pr.prev == NONE)) continue;
        const Cmd* c = (const Cmd*)(N.buf.data() + pr.cur);
        if (c->op != OP_VSCF && c->op != OP_PSCF) continue;
        idx.push_back((uint32_t)i);
    }
    for (int k = 0; k < 3; k++) {
        PairCameras(N, P, ts[k]);
        for (uint32_t i : idx) {
            const Pair& pr = g_pairs[i];
            auto* q = (const PConst*)((const Cmd*)(N.buf.data() + pr.cur) + 1);
            const float* a = pr.synth != NONE ? &g_synth[pr.synth] : (const float*)((const PConst*)((const Cmd*)(P.buf.data() + pr.prev) + 1) + 1);
            size_t at = outs[k].size();
            outs[k].resize(at + q->count * 4);
            LerpConsts(q, (const float*)(q + 1), a, pr.mi != NONE ? &g_pairMask[pr.mi] : nullptr, nullptr, pr.boneBase, ts[k], &outs[k][at], false,
                       g_pairCam[i], g_pairCamMode[i]);
        }
    }
    size_t at = 0;
    int bad = 0;
    std::map<std::pair<int, UINT>, int> regs;
    for (uint32_t i : idx) {
        const Pair& pr = g_pairs[i];
        const Cmd* c = (const Cmd*)(N.buf.data() + pr.cur);
        auto* q = (const PConst*)(c + 1);
        float worst = 0;
        UINT wr = 0;
        for (UINT e = 0; e < q->count * 4; e++) {
            float a = outs[0][at + e], m = outs[1][at + e], b = outs[2][at + e];
            float dev = std::fabs(m - 0.5f * (a + b)), tol = 0.1f * std::fabs(b - a) + 1e-3f * (std::fabs(m) + 1.0f);
            if (dev > tol && dev > worst) worst = dev, wr = e;
        }
        if (worst > 0) {
            regs[{c->op == OP_PSCF, q->start + wr / 4}]++;
            if (bad++ < 12)
                printf("    %s c%u (packet c%u x%u%s%s): at 0.33 %.4f, 0.5 %.4f, 0.67 %.4f\n", c->op == OP_PSCF ? "PS" : "VS", q->start + wr / 4, q->start, q->count,
                       g_pairCam[i] ? ", camera path" : "", pr.boneBase >= 0 ? ", bones" : "", outs[0][at + wr], outs[1][at + wr], outs[2][at + wr]);
        }
        at += q->count * 4;
    }
    printf("    %d of %zu blended packets off the line at t = 0.5;", bad, idx.size());
    for (auto& kv : regs) printf(" %s c%u x%d", kv.first.first ? "PS" : "VS", kv.first.second, kv.second);
    printf("\n");
}
// Blended constants of one tick whose two values look like integers or packed bits (a denormal: an int stored in a
// float) and differ: blending them makes values in between that may mean something else to the shader
static int g_intScanTick = -1;
static bool g_zeroScan;
static void IntScan(const Frame& N, const Frame& P) {
    std::map<std::pair<int, UINT>, int> regs;
    int shown = 0;
    for (size_t i = 0; i < g_pairs.size(); i++) {
        const Pair& pr = g_pairs[i];
        if (pr.upSynth != NONE || pr.prev == NONE || pr.synth != NONE) continue;
        const Cmd* c = (const Cmd*)(N.buf.data() + pr.cur);
        if (c->op != OP_VSCF && c->op != OP_PSCF) continue;
        auto* q = (const PConst*)(c + 1);
        const float* v = (const float*)(q + 1);
        const float* a = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + pr.prev) + 1) + 1);
        const Mask256* lm = pr.mi != NONE ? &g_pairMask[pr.mi] : nullptr;
        for (UINT r = 0; r < q->count; r++) {
            if (!lm || !lm->test((int)r)) continue;  // (not blended)
            for (int k = 0; k < 4; k++) {
                float x = a[r * 4 + k], y = v[r * 4 + k];
                if (x == y) continue;
                if (g_zeroScan) {  // (instead: a component that changes sign and passes zero near halfway)
                    if (x * y < 0 && std::fabs(x + y) < 0.15f * std::max(std::fabs(x), std::fabs(y)) && std::max(std::fabs(x), std::fabs(y)) > 1e-3f) {
                        regs[{c->op == OP_PSCF, q->start + r}]++;
                        if (shown < 16) {
                            Mask256 mm;
                            LerpStats lsx;
                            ConstLerpMask(c, (const Cmd*)(P.buf.data() + pr.prev), mm, nullptr, lsx);
                            printf("    (recomputed: blend %d, jumps %u; the pair's own mask: %d)\n", (int)mm.test((int)r), lsx.jump, (int)lm->test((int)r));
                        }
                        if (shown++ < 16)
                            printf("    %s c%u.%c: %g -> %g (zero at t = %.2f) | row (%g %g %g %g) -> (%g %g %g %g) | packet c%u x%u\n", c->op == OP_PSCF ? "PS" : "VS", q->start + r,
                                   "xyzw"[k], x, y, x / (x - y), a[r * 4], a[r * 4 + 1], a[r * 4 + 2], a[r * 4 + 3], v[r * 4], v[r * 4 + 1], v[r * 4 + 2], v[r * 4 + 3], q->start, q->count);
                    }
                    continue;
                }
                auto intlike = [](float f) {
                    uint32_t b;
                    memcpy(&b, &f, 4);
                    bool denorm = (b & 0x7f800000) == 0 && (b & 0x007fffff) != 0;
                    return denorm || (std::fabs(f) < 65536 && f == std::floor(f));
                };
                if (!intlike(x) || !intlike(y)) continue;
                regs[{c->op == OP_PSCF, q->start + r}]++;
                if (shown++ < 16) {
                    uint32_t bx, by;
                    memcpy(&bx, &x, 4), memcpy(&by, &y, 4);
                    printf("    %s c%u.%c: %g (bits %u) -> %g (bits %u)\n", c->op == OP_PSCF ? "PS" : "VS", q->start + r, "xyzw"[k], x, bx, y, by);
                }
            }
        }
    }
    printf("    integer-like blended values that change:");
    if (regs.empty()) printf(" none");
    for (auto& kv : regs) printf(" %s c%u x%d", kv.first.first ? "PS" : "VS", kv.first.second, kv.second);
    printf("\n");
}
// -drawinfo T i,j,...: the given draws of tick T's frame and their counterparts - keys, placement (the first 4x4's
// origin column, or the root bone), and the instance distances the group matching sees between them
static int g_drawInfoTick = -1;
static std::vector<uint32_t> g_drawInfoList;
static void DrawInfo(const Frame& N, const Frame& P) {
    auto place = [](const Frame& F, const Owners& o, uint32_t d) {
        static char s[160];
        if (const PConst* q = FirstMat4(F, o, d)) {
            const float* m = (const float*)(q + 1);
            snprintf(s, sizeof s, "4x4 c%u origin (%.4g %.4g %.4g %.4g)", q->start, m[3], m[7], m[11], m[15]);
        } else if (const PConst* b = FirstBones(F, o, d)) {
            const float* m = (const float*)(b + 1);
            snprintf(s, sizeof s, "bones c%u x%u root (%.4g %.4g %.4g)", b->start, b->count, m[3], m[7], m[11]);
        } else {
            snprintf(s, sizeof s, "no placement");
        }
        return s;
    };
    // the field's prediction for each listed draw, and how far its counterpart is from it (NDC)
    for (uint32_t i : g_drawInfoList) {
        if (i >= N.draws.size() || g_match[i] < 0) continue;
        const PConst* a = FirstMat4(N, g_ownN, i);
        if (!a) continue;
        std::vector<Inst> ci(1);
        InstOf(N, g_ownN, (int)i, a->start, nullptr, ci[0]);
        Inst pi;
        InstOf(P, g_ownP, g_match[i], a->start, nullptr, pi);
        float r0 = ci[0].ok && pi.ok ? InstDist(ci[0], pi) : -1;
        float rf = -1;
        if (FieldInsts(CamKey(N.draws[i].rt, a->start), ci, 0) && pi.ok) rf = InstDist(ci[0], pi);
        printf("  FIELD N %u <- P %d: distance as is %.6g, from the field's prediction %.6g\n", i, g_match[i], r0, rf);
    }
    for (uint32_t i : g_drawInfoList) {
        if (i >= N.draws.size()) continue;
        int j = g_match[i];
        printf("  N %u key %016llx key2 %016llx vs %p rt %p owners %u: %s | matched P %d%s\n", i, (unsigned long long)N.draws[i].key,
               (unsigned long long)N.draws[i].key2, N.draws[i].vs, N.draws[i].rt, g_ownN.count[i], place(N, g_ownN, i), j,
               i < g_rejCur.size() && g_rejCur[i] ? " (rejected)" : "");
        {
            const Cmd* dc = (const Cmd*)(N.buf.data() + N.draws[i].off);
            if (dc->op == OP_DPUP) {
                auto* x = (const PDPUP*)(dc + 1);
                const float* v = (const float*)((const uint8_t*)(x + 1) + x->posOff);
                printf("    DPUP stride %u rhw %d: first vertex (%.1f %.1f %.4g %.4g)\n", x->stride, (int)x->rhw, v[0], v[1], v[2], v[3]);
            }
        }
        if (j < 0)  // (previous draws of the same key, and whether they were taken)
            for (uint32_t k = 0; k < P.draws.size(); k++)
                if (P.draws[k].key == N.draws[i].key)
                    printf("    same key in P: %u (%s): %s\n", k, g_prevUsed[k] ? "taken" : "free", place(P, g_ownP, k));
        if (j >= 0) printf("    P %d key %016llx: %s\n", j, (unsigned long long)P.draws[j].key, place(P, g_ownP, (uint32_t)j));
    }
    // instance distances as MatchGroup computes them (with the camera estimate of the draw's context, when there is one)
    for (uint32_t i : g_drawInfoList) {
        const PConst* a = FirstMat4(N, g_ownN, i);
        bool bones = !a && (a = FirstBones(N, g_ownN, i));
        if (!a) continue;
        const M4* D = nullptr;
        if (!bones) {
            auto it = g_cam.find(CamKey(N.draws[i].rt, a->start));
            printf("    N %u: camera context %s, %zu candidates, estimate %s\n", i, it == g_cam.end() ? "absent" : "present",
                   it == g_cam.end() ? (size_t)0 : it->second.cand.size(), it != g_cam.end() && it->second.has ? "yes" : "no");
            if (it != g_cam.end() && it->second.has) D = &it->second.D;
            if (it != g_cam.end() && i == g_drawInfoList[0]) {  // (how near the candidates come to agreeing)
                std::vector<M4> sm;
                for (const CamCand& cc : it->second.cand) sm.push_back(CandMatrix(cc));
                for (double tol : {1e-3, 3e-3, 1e-2, 3e-2, 1e-1}) {
                    int best = 0, bi = -1;
                    for (size_t x = 0; x < sm.size(); x++) {
                        double scale = 0;
                        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) scale += std::fabs(sm[x].m[r][c]);
                        int v = 0;
                        for (size_t y = 0; y < sm.size(); y++) v += Dist(sm[x], sm[y]) < tol * scale;
                        if (v > best) best = v, bi = (int)x;
                    }
                    printf("      tolerance %g: best candidate %d has %d of %zu agreeing\n", tol, bi, best, sm.size());
                }
                if (!sm.empty()) {
                    const M4& m = sm[0];
                    printf("      candidate 0: [%.5g %.5g %.5g %.5g | %.5g %.5g %.5g %.5g | %.5g %.5g %.5g %.5g | %.5g %.5g %.5g %.5g]\n", m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3],
                           m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3], m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3], m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]);
                }
                for (size_t x = 0; x < sm.size(); x++) {
                    const CamCand& cc = it->second.cand[x];
                    const M4& m = sm[x];
                    printf("      cand %2zu %s: row0 [%.4g %.4g %.4g %.4g] row3 [%.4g %.4g %.4g %.4g] cur origin (%.4g %.4g %.4g %.4g)\n", x, cc.prev ? "moved" : "same ", m.m[0][0], m.m[0][1],
                           m.m[0][2], m.m[0][3], m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3], cc.cur ? cc.cur[3] : 0.0f, cc.cur ? cc.cur[7] : 0.0f, cc.cur ? cc.cur[11] : 0.0f, cc.cur ? cc.cur[15] : 0.0f);
                }
            }
        }
        Inst ci;
        InstOf(N, g_ownN, (int)i, a->start, D, ci, bones);
        for (uint32_t k : g_drawInfoList) {
            int j = g_match[k];
            if (j < 0) continue;
            Inst pi;
            InstOf(P, g_ownP, j, a->start, nullptr, pi, bones);
            if (ci.ok && pi.ok) printf("    dist N %u - P %d (counterpart of %u): %.6g\n", i, j, k, InstDist(ci, pi));
        }
    }
}
// -lerprows T: the pixel-shader rows tick T's in-between frames blend (changed between the paired uploads), each distinct
// (register, previous, current) once with how many uploads carry it
static int g_lerpRowsTick = -1;
static void LerpRows(const Frame& N, const Frame& P) {
    std::map<std::string, int> seen;
    for (const Pair& p : g_pairs) {
        const Cmd* c = (const Cmd*)(N.buf.data() + p.cur);
        if ((c->op != OP_PSCF && c->op != OP_VSCF) || p.prev == NONE || p.synth != NONE || p.mi == NONE) continue;
        auto* q = (const PConst*)(c + 1);
        const float* v = (const float*)(q + 1);
        const float* a = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + p.prev) + 1) + 1);
        for (UINT r = 0; r < q->count; r++) {
            if (!memcmp(v + r * 4, a + r * 4, 16) || !g_pairMask[p.mi].test((int)r)) continue;
            if (c->op == OP_VSCF && g_pmask[q->ord].test((int)(q->start + r))) continue;  // (positional: not this list)
            char s[256];
            snprintf(s, sizeof s, "%s c%-3u (%.4g %.4g %.4g %.4g) -> (%.4g %.4g %.4g %.4g)", c->op == OP_PSCF ? "PS" : "VS", q->start + r, a[r * 4], a[r * 4 + 1], a[r * 4 + 2], a[r * 4 + 3],
                     v[r * 4], v[r * 4 + 1], v[r * 4 + 2], v[r * 4 + 3]);
            seen[s]++;
        }
    }
    for (auto& kv : seen) printf("  %s  x%d\n", kv.first.c_str(), kv.second);
}
// -lerpdraw T REG X: the draws of tick T reading a PS upload whose register REG starts with X (one object's draws, e.g.
// the player's light position), and every constant row they read that changed and is blended or switched
static int g_lerpDrawTick = -1, g_lerpDrawReg = 0;
static std::vector<uint32_t> g_lerpDrawList;  // (-lerpdraw T -1 i,j,...: these draws instead)
static float g_lerpDrawX = 0;
static void LerpDraw(const Frame& N, const Frame& P) {
    std::unordered_map<uint32_t, const Pair*> byCur;
    for (const Pair& p : g_pairs) byCur[p.cur] = &p;
    // the register files as the frame goes: the upload in effect for each register at each draw
    uint32_t own[2][256];
    for (auto& r : own) for (auto& x : r) x = NONE;
    std::map<std::string, int> seen;
    std::vector<uint32_t> draws;
    uint32_t di = 0;
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_VSCF || c->op == OP_PSCF) {
            auto* q = (const PConst*)(c + 1);
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) own[c->op == OP_PSCF][r] = off;
        } else if (c->op == OP_DP || c->op == OP_DIP || c->op == OP_DPUP || c->op == OP_DIPUP) {
            uint32_t po = g_lerpDrawReg >= 0 ? own[1][g_lerpDrawReg] : NONE;
            bool sel = g_lerpDrawReg < 0 && std::find(g_lerpDrawList.begin(), g_lerpDrawList.end(), di) != g_lerpDrawList.end();
            if (po != NONE) {
                auto* q = (const PConst*)((const Cmd*)(N.buf.data() + po) + 1);
                sel = std::fabs(((const float*)(q + 1))[(g_lerpDrawReg - q->start) * 4] - g_lerpDrawX) < std::max(0.5f, 0.001f * std::fabs(g_lerpDrawX));
            }
            if (sel) {
                draws.push_back(di);
                for (int k = 0; k < 2; k++)
                    for (UINT reg = 0; reg < 256; reg++) {
                        uint32_t uo = own[k][reg];
                        if (uo == NONE) continue;
                        const Cmd* uc = (const Cmd*)(N.buf.data() + uo);
                        auto* q = (const PConst*)(uc + 1);
                        UINT r = reg - q->start;
                        if (uc->op == OP_VSCF && g_pmask[q->ord].test((int)reg)) continue;  // (positional)
                        const float* v = (const float*)(q + 1) + r * 4;
                        auto it = byCur.find(uo);
                        char s[256];
                        if (it == byCur.end() || it->second->prev == NONE || it->second->synth != NONE) continue;  // (no counterpart: kept)
                        const Pair& p = *it->second;
                        const float* a = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + p.prev) + 1) + 1) + r * 4;
                        if (!memcmp(v, a, 16)) continue;
                        bool lerp = p.mi != NONE && g_pairMask[p.mi].test((int)r);
                        snprintf(s, sizeof s, "%s c%-3u %s (%.4g %.4g %.4g %.4g) -> (%.4g %.4g %.4g %.4g)", k ? "PS" : "VS", reg, lerp ? "blended " : "switched", a[0],
                                 a[1], a[2], a[3], v[0], v[1], v[2], v[3]);
                        seen[s]++;
                    }
            }
            di++;
        }
        off += c->size;
    }
    printf("  draws:");
    for (uint32_t i : draws) printf(" %u(<-%d)", i, i < g_match.size() ? g_match[i] : -9);
    printf("\n");
    for (auto& kv : seen) printf("  %s  x%d\n", kv.first.c_str(), kv.second);
}
// -groupdump T KEY: the instances of one key in both frames - origin on the screen (x/w, y/w) and w - sorted by x
static int g_groupDumpTick = -2;
static uint64_t g_groupDumpKey;
static void GroupDump(const Frame& N, const Frame& P) {
    for (int f = 0; f < 2; f++) {
        const Frame& F = f ? P : N;
        const Owners& o = f ? g_ownP : g_ownN;
        std::vector<std::array<double, 5>> v;
        for (uint32_t i = 0; i < F.draws.size(); i++) {
            if ((F.draws[i].key >> 32) != (g_groupDumpKey >> 32) && F.draws[i].key != g_groupDumpKey) continue;
            const PConst* a = FirstMat4(F, o, i);
            if (!a) continue;
            const float* m = (const float*)(a + 1);
            if (!(m[15] > 1e-3f)) continue;
            v.push_back({m[3] / m[15], m[7] / m[15], m[15], (double)i, f ? (double)-1 : (double)(i < g_match.size() ? g_match[i] : -9)});
        }
        std::sort(v.begin(), v.end());
        printf("  %s: %zu instances\n", f ? "P (last tick)" : "N (this tick)", v.size());
        for (auto& e : v) printf("    x %7.3f y %7.3f w %8.1f  draw %4.0f%s\n", e[0], e[1], e[2], e[3], f ? "" : (" <- " + std::to_string((int)e[4])).c_str());
    }
}
// -slotcheck T: tick T's draws where a light position (PS c21-c23, c162) switched, and the light rows they read that are
// still blended (ranges c24-c26/c163, colours c146-c158) - what the slot coupling missed; -1: a count per tick
static int g_slotCheckTick = -2;
static int g_camPairsReport;  // (-campairs)
static void SlotCheck(const Frame& N, const Frame& P, uint32_t tick) {
    std::unordered_map<uint32_t, const Pair*> byCur;
    for (const Pair& p : g_pairs) byCur[p.cur] = &p;
    uint32_t own[256];
    for (auto& x : own) x = NONE;
    auto isPos = [](UINT r) { return (r >= 21 && r <= 23) || r == 162; };
    auto isLight = [](UINT r) { return (r >= 24 && r <= 26) || r == 163 || (r >= 146 && r <= 158); };
    // state of one register at a draw: 0 unchanged / no counterpart, 1 switched, 2 blended
    auto state = [&](UINT reg, const float** a, const float** v) {
        uint32_t uo = own[reg];
        if (uo == NONE) return 0;
        auto* q = (const PConst*)((const Cmd*)(N.buf.data() + uo) + 1);
        UINT r = reg - q->start;
        auto it = byCur.find(uo);
        if (it == byCur.end() || it->second->prev == NONE || it->second->synth != NONE) return 0;
        *v = (const float*)(q + 1) + r * 4;
        *a = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + it->second->prev) + 1) + 1) + r * 4;
        if (!memcmp(*v, *a, 16)) return 0;
        return it->second->mi != NONE && g_pairMask[it->second->mi].test((int)r) ? 2 : 1;
    };
    std::map<std::string, std::vector<uint32_t>> seen;
    uint32_t di = 0, hits = 0;
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_PSCF) {
            auto* q = (const PConst*)(c + 1);
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) own[r] = off;
        } else if (c->op == OP_DP || c->op == OP_DIP || c->op == OP_DPUP || c->op == OP_DIPUP) {
            bool sw = false;
            const float *a, *v;
            for (UINT r : {21u, 22u, 23u, 162u}) sw |= state(r, &a, &v) == 1;
            if (sw) {
                bool any = false;
                for (UINT r = 0; r < 256; r++) {
                    if (!isLight(r) && !isPos(r)) continue;
                    int s = state(r, &a, &v);
                    if (s != 2) continue;
                    float dl = 0, mag = 0;
                    for (int k = 0; k < 4; k++)
                        if (std::fabs(a[k]) < 1e20f && std::fabs(v[k]) < 1e20f) dl = std::max(dl, std::fabs(v[k] - a[k])), mag = std::max(mag, std::max(std::fabs(a[k]), std::fabs(v[k])));
                    if (dl <= 0.02f * mag && !isPos(r)) continue;
                    any = true;
                    if (g_slotCheckTick == (int)tick) {
                        char s[256];
                        snprintf(s, sizeof s, "c%-3u blended (%.4g %.4g %.4g %.4g) -> (%.4g %.4g %.4g %.4g)", r, a[0], a[1], a[2], a[3], v[0], v[1], v[2], v[3]);
                        seen[s].push_back(di);
                    }
                }
                hits += any;
            }
            di++;
        }
        off += c->size;
    }
    if (g_slotCheckTick == -1) { if (hits) printf("  slotcheck: %u draws with a switched light still blend light rows\n", hits); return; }
    for (auto& kv : seen) {
        printf("  %s  draws", kv.first.c_str());
        for (size_t k = 0; k < kv.second.size() && k < 12; k++) printf(" %u", kv.second[k]);
        printf("%s\n", kv.second.size() > 12 ? " ..." : "");
    }
}
// -passscan T: tick T's draws whose light position (PS c21-c23, c162; object-relative) is blended along a path that
// passes much nearer the object than either end - a lamp sliding through the object halfway; -1: a count per tick
static int g_passScanTick = -2;
static void PassScan(const Frame& N, const Frame& P, uint32_t tick) {
    std::unordered_map<uint32_t, const Pair*> byCur;
    for (const Pair& p : g_pairs) byCur[p.cur] = &p;
    uint32_t own[256];
    for (auto& x : own) x = NONE;
    uint32_t di = 0, hits = 0;
    for (uint32_t off = 0; off < N.buf.size();) {
        const Cmd* c = (const Cmd*)(N.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_PSCF) {
            auto* q = (const PConst*)(c + 1);
            for (UINT r = q->start; r < q->start + q->count && r < 256; r++) own[r] = off;
        } else if (c->op == OP_DP || c->op == OP_DIP || c->op == OP_DPUP || c->op == OP_DIPUP) {
            for (UINT reg : {21u, 22u, 23u, 162u}) {
                uint32_t uo = own[reg];
                if (uo == NONE) continue;
                auto* q = (const PConst*)((const Cmd*)(N.buf.data() + uo) + 1);
                UINT r = reg - q->start;
                auto it = byCur.find(uo);
                const char* why = nullptr;
                const float *v = (const float*)(q + 1) + r * 4, *a = nullptr;
                if (it == byCur.end()) why = "no pair";
                else if (it->second->prev == NONE || it->second->synth != NONE) why = "synth/none";
                else {
                    a = (const float*)((const PConst*)((const Cmd*)(P.buf.data() + it->second->prev) + 1) + 1) + r * 4;
                    if (!memcmp(v, a, 16) || it->second->mi == NONE || !g_pairMask[it->second->mi].test((int)r)) continue;
                }
                if (!a) continue;
                // (skinned objects get their lights in world space: measure from the bones' root instead of the origin)
                float o[3] = {0, 0, 0};
                if (const PConst* bq = FirstBones(N, g_ownN, di)) {
                    const float* bm = (const float*)(bq + 1);
                    o[0] = bm[3], o[1] = bm[7], o[2] = bm[11];
                }
                float A[3] = {a[0] - o[0], a[1] - o[1], a[2] - o[2]}, V[3] = {v[0] - o[0], v[1] - o[1], v[2] - o[2]};
                float la = std::sqrt(A[0] * A[0] + A[1] * A[1] + A[2] * A[2]), lv = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
                float m[3] = {(A[0] + V[0]) / 2, (A[1] + V[1]) / 2, (A[2] + V[2]) / 2};
                float lm = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                if (lm >= 0.7f * std::min(la, lv) || std::min(la, lv) < 30) continue;
                hits++;
                if (g_passScanTick == (int)tick)
                    printf("  draw %u<-%d PS c%u blended (%.4g %.4g %.4g) -> (%.4g %.4g %.4g): lengths %.0f / %.0f, halfway %.0f%s\n", di,
                           di < g_match.size() ? g_match[di] : -9, reg, a[0], a[1], a[2], v[0], v[1], v[2], la, lv, lm, why ? why : "");
            }
            di++;
        }
        off += c->size;
    }
    if (g_passScanTick == -1 && hits) printf("  passscan: %u light rows pass near their object halfway\n", hits);
}
// -textkeys T: tick T's screen strings - place, glyph size, how many glyphs have a pixel key, the texture they sample
static int g_textKeysTick = -1;
static void TextKeysReport(const Frame& N) {
    std::vector<TextRun> runs;
    std::vector<float> box;
    std::vector<uint32_t> col;
    BuildRuns(N, runs, box, col);
    std::map<std::string, int> sizes;
    for (const TextRun& r : runs) {
        int keyed = 0;
        for (uint32_t m = 0; m < r.n; m++) keyed += r.first + m < N.ckeys.size() && N.ckeys[r.first + m] != 0;
        const float* b = &box[r.first * 4];
        char s[200];
        snprintf(s, sizeof s, "glyph %2.0fx%2.0f px: %s", b[2] - b[0], b[3] - b[1], keyed == (int)r.n ? "keyed" : keyed ? "partly keyed" : "no keys");
        sizes[s] += r.n;
        if (keyed < (int)r.n && b[0] >= 0 && b[0] < 3440)
            printf("  n %2u at %5.0f,%4.0f glyph %2.0fx%2.0f col %08x: %d of %u keyed\n", r.n, b[0], b[1], b[2] - b[0], b[3] - b[1], col[r.first], keyed, r.n);
    }
    for (auto& kv : sizes) printf("  %s: %d glyphs\n", kv.first.c_str(), kv.second);
}
// -bonesless T: tick T's unmatched bone-driven draws, and the previous frame's bone palettes near their root (same slot)
static int g_bonelessTick = -1;
static void Boneless(const Frame& N, const Frame& P) {
    int shown = 0;
    for (uint32_t i = 0; i < N.draws.size() && shown < 25; i++) {
        if (g_match[i] >= 0) continue;
        const PConst* b = FirstBones(N, g_ownN, i);
        if (!b || FirstMat4(N, g_ownN, i)) continue;
        const float* m = (const float*)(b + 1);
        float best = 3e38f;
        int bj = -1;
        bool bjMatched = false;
        for (uint32_t j = 0; j < P.draws.size(); j++) {
            const PConst* c = FirstBones(P, g_ownP, j, b->start);
            if (!c || c->count != b->count) continue;
            const float* n = (const float*)(c + 1);
            float d = std::sqrt((m[3] - n[3]) * (m[3] - n[3]) + (m[7] - n[7]) * (m[7] - n[7]) + (m[11] - n[11]) * (m[11] - n[11]));
            if (d < best) best = d, bj = (int)j, bjMatched = g_prevUsed[j] != 0;
        }
        printf("  N %u key %016llx bones c%u x%u root (%.0f %.0f %.0f): nearest previous palette draw %d (key %016llx, %s) %.1f away\n", i,
               (unsigned long long)N.draws[i].key, b->start, b->count, m[3], m[7], m[11], bj, bj >= 0 ? (unsigned long long)P.draws[bj].key : 0ull,
               bjMatched ? "matched to another" : "free", best);
        shown++;
    }
}
// everything one tick's matching decided: the pairs, the lerp masks, the text decisions and the moved screen quads
// (an optimisation must keep it, tick for tick)
static uint64_t FullHash() { return PairHash(); }  // (PairHash covers all of it)
static bool g_seqHash;
static int g_benchField = -1;  // (-field: field_match on)
static bool g_benchNoSlot;     // (-noslot: slot_rows off)
static bool g_benchNoCamFit;   // (-nocamfit: cam_fit off)
static bool g_benchNoGroupCam; // (-nogroupcam: group_cam off)
static FILE* g_dumpMatch;      // (-dumpmatch file)
static double g_seqTicks;  // (ticks run, all repetitions)  // -hash: per tick, FullHash only
static void RunSequence(bool detail) {
    double phSum[16] = {}, msSum = 0, textSum = 0;
    for (size_t k = 1; k < g_seq.size(); k++) {
        g_fr[0] = k >= 2 ? g_seq[k - 2] : Frame();
        g_fr[1] = g_seq[k - 1];
        g_fr[2] = g_seq[k];
        g_cur = 2;
        Frame &N = g_fr[2], &P = g_fr[1];
        memset(&St, 0, sizeof St);
        int64_t a = Qpc();
        if (g_oldCands) C.debug_textcands = 1;
        if ((int)g_seqTick[k] == g_debugTextTick) { C.debug_text = 1; g_log = stdout; }
        BuildPairs(N, P);
        double ms = Ms(Qpc() - a);
        msSum += ms, textSum += g_textMs, g_seqTicks++;
        for (int q = 0; q < 16; q++) phSum[q] += St.phaseMs[q];
        if ((int)g_seqTick[k] == g_debugTextTick) { g_log = nullptr; TextOrphans(N, P); }
        SaveHistory(N, P);
        if (g_dumpMatch) {
            fprintf(g_dumpMatch, "%u", g_seqTick[k]);
            for (int m : g_match) fprintf(g_dumpMatch, " %d", m);
            fprintf(g_dumpMatch, "\n");
        }
        if (g_seqHash) { printf("tick %6u hash %016llx\n", g_seqTick[k], (unsigned long long)FullHash()); continue; }
        size_t unm = 0;
        for (int m : g_match) unm += m < 0;
        printf("tick %6u: %5zu draws, %4zu unmatched, rejected %llu, crossing %llu, camera-estimated %llu, unpositioned %llu, up-snaps %llu, "
               "screen dropped %llu moved %llu, text %llu strings %llu glyphs (%llu carried), pairs %zu, cut %d, slot jumps %llu (+%llu rows), field groups %llu, text guesses %llu layout %llu/%llu, ckeys %d, %.1f ms\n",
               g_seqTick[k], N.draws.size(), unm, (unsigned long long)St.rejected, (unsigned long long)St.crossing, (unsigned long long)St.synth,
               (unsigned long long)St.unpairedPos, (unsigned long long)St.upSnaps, (unsigned long long)St.screenDropped,
               (unsigned long long)St.screenSynth, (unsigned long long)St.textRuns, (unsigned long long)St.textRunQuads,
               (unsigned long long)St.textRunMoved, g_pairs.size(), (int)g_cut, (unsigned long long)St.slotJumps, (unsigned long long)St.slotRows, (unsigned long long)St.fieldGroups, (unsigned long long)St.textGuesses, (unsigned long long)St.textLayout, (unsigned long long)St.textNoLayout, (int)std::count_if(N.ckeys.begin(), N.ckeys.end(), [](uint64_t v) { return v != 0; }), ms);
        printf("    text matching %.2f ms: runs %.2f index %.2f pieces %.2f coverage %.2f claims %.2f field+copies %.2f apply %.2f layout %.2f\n", g_textMs, g_textLap[0], g_textLap[1], g_textLap[2], g_textLap[3], g_textLap[4], g_textLap[5], g_textLap[6], g_textLap[7]);
        memset(g_textLap, 0, sizeof g_textLap);
        if (detail) SeqDetail(N, P);
        if (g_cancelScan) CancelScan(N, P, g_seqTick[k]);
        if ((int)g_seqTick[k] == g_upTraceTick) UpTrace(N, P);
        if ((int)g_seqTick[k] == g_drawInfoTick) DrawInfo(N, P);
        if ((int)g_seqTick[k] == g_lerpRowsTick || g_lerpRowsTick == 0) LerpRows(N, P);
        if ((int)g_seqTick[k] == g_lerpDrawTick) LerpDraw(N, P);
        if ((int)g_seqTick[k] == g_slotCheckTick || g_slotCheckTick == -1) SlotCheck(N, P, g_seqTick[k]);
        if ((int)g_seqTick[k] == g_groupDumpTick) GroupDump(N, P);
        if (g_camPairsReport) {  // which positional uploads the in-between frames move along a camera path (at t = 0.5)
            St.camPathPairs = St.camShared = 0;
            PairCameras(N, P, 0.5f);
            size_t m4 = 0, shared = 0, ctx = 0, facing = 0;
            std::map<uint64_t, std::array<int, 3>> perCtx;  // camKey -> (on the context's path, shared own path, plain blend)
            for (size_t i = 0; i < g_pairs.size(); i++) {
                const Pair& pr = g_pairs[i];
                if (!pr.camKey || pr.upSynth != NONE) continue;
                m4++;
                auto cit = g_cam.find(pr.camKey);
                bool own = g_pairCam[i] && (cit == g_cam.end() || g_pairCam[i] != &cit->second);
                if (!g_pairCam[i]) perCtx[pr.camKey][2]++;
                else if (own) shared++, perCtx[pr.camKey][1]++;
                else if (g_pairCamMode[i] == 2) facing++, perCtx[pr.camKey][0]++;
                else ctx++, perCtx[pr.camKey][0]++;
            }
            int mixed = 0;
            for (auto& kv : perCtx) mixed += (kv.second[0] + kv.second[1] > 0) && kv.second[2] > 0;
            printf("  campairs: %zu 4x4 pairs: %zu on their context's path, %zu facing, %zu on another path (%llu joined, still under it), %zu plain; contexts mixing path and plain %d\n",
                   m4, ctx, facing, shared, (unsigned long long)St.camJoined, m4 - ctx - facing - shared, mixed);
            if (g_camPairsReport == 2) {
                for (auto& kv : perCtx)
                    printf("    ctx %016llx: path %d shared %d plain %d\n", (unsigned long long)kv.first, kv.second[0], kv.second[1], kv.second[2]);
                // each shared matrix on its own path: would the plain 4x4 pairs fit its motion (prev = D cur)?
                for (size_t i = 0; i < g_pairs.size(); i++) {
                    const Pair& pr = g_pairs[i];
                    auto cit = g_cam.find(pr.camKey);
                    if (!pr.camKey || !g_pairCam[i] || (cit != g_cam.end() && g_pairCam[i] == &cit->second)) continue;
                    auto* q = (const PConst*)((const Cmd*)(N.buf.data() + pr.cur) + 1);
                    const M4& D = g_pairCam[i]->D;
                    int fit = 0, fitT = 0, n = 0;
                    for (size_t j = 0; j < g_pairs.size(); j++) {
                        const Pair& o = g_pairs[j];
                        if (!o.camKey || g_pairCam[j] || o.prev == NONE || o.synth != NONE || o.upSynth != NONE) continue;
                        M4 Mp = FromRegs((const float*)((const PConst*)((const Cmd*)(P.buf.data() + o.prev) + 1) + 1));
                        M4 Mn = FromRegs((const float*)((const PConst*)((const Cmd*)(N.buf.data() + o.cur) + 1) + 1));
                        n++;
                        double e = NormInf(Comb(Mul(D, Mn), 1, Mp, -1)), s = std::max(1e-6, NormInf(Mp));
                        fit += e <= 2e-3 * s;
                        // (or in the transposed sense: the shared matrix a view matrix, the others clip-space - D acting on the right)
                        double e2 = NormInf(Comb(Mul(Mn, D), 1, Mp, -1));
                        fitT += e2 <= 2e-3 * s;
                    }
                    printf("    shared %s c%u (ctx %016llx): of %d plain 4x4 pairs, %d fit its motion, %d fit it from the right\n",
                           ((const Cmd*)(N.buf.data() + pr.cur))->op == OP_VSCF ? "VS" : "PS", q->start, (unsigned long long)pr.camKey, n, fit, fitT);
                    const float* v = (const float*)(q + 1);
                    printf("      cur [%.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g]\n", v[0], v[1], v[2], v[3], v[4], v[5],
                           v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
                    // is it one of its context's candidates?
                    auto c2 = g_cam.find(pr.camKey);
                    bool isCand = false;
                    if (c2 != g_cam.end())
                        for (const CamCand& cc : c2->second.cand) isCand |= cc.cur && !memcmp(cc.cur, v, 64);
                    printf("      among its context's %zu candidates: %s\n", c2 != g_cam.end() ? c2->second.cand.size() : (size_t)0, isCand ? "yes" : "no");
                    for (size_t j = 0, shown = 0; j < g_pairs.size() && shown < 2; j++) {
                        const Pair& o = g_pairs[j];
                        if (o.camKey != pr.camKey || g_pairCam[j] || o.prev == NONE) continue;
                        const float* w = (const float*)((const PConst*)((const Cmd*)(N.buf.data() + o.cur) + 1) + 1);
                        printf("      a plain one [%.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g]\n", w[0], w[1], w[2], w[3], w[4],
                               w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
                        shown++;
                    }
                }
            }
        }
        if ((int)g_seqTick[k] == g_passScanTick || g_passScanTick == -1) PassScan(N, P, g_seqTick[k]);
        if ((int)g_seqTick[k] == g_textKeysTick) TextKeysReport(N);
        if ((int)g_seqTick[k] == g_bonelessTick) Boneless(N, P);
        if ((int)g_seqTick[k] == g_tScanTick || g_tScanTick == 0) TScan(N, P);
        if ((int)g_seqTick[k] == g_intScanTick || g_intScanTick == 0) IntScan(N, P);
        if (g_texQuery && k + 1 == g_seq.size()) TexQuery(N, g_seqTick[k]);
    }
    // averages per tick: the whole matching and its phases
    double nk = (double)std::max<size_t>(g_seq.size() - 1, 1);
    printf("average %.2f ms per tick (text %.2f); phases (ms):", msSum / nk, textSum / nk);
    for (int q = 0; q < (int)(sizeof kPhase / sizeof *kPhase); q++) printf(" %s %.2f", kPhase[q], phSum[q] / nk);
    printf("\n");
}

// -redundancy: how many constant uploads of a frame write values the registers already hold (what a replay could
// skip without changing any GPU state); state-block applies forget everything
static void Redundancy(const char* name, const Frame& F) {
    static float vs[256][4], ps[256][4];
    bool vok[256] = {}, pok[256] = {};
    uint64_t calls[2] = {}, regs[2] = {}, sameRegs[2] = {}, sameCalls[2] = {};
    for (uint32_t off = 0; off < F.buf.size();) {
        const Cmd* c = (const Cmd*)(F.buf.data() + off);
        if (!c->size) break;
        if (c->op == OP_SBAPPLY) { memset(vok, 0, sizeof vok); memset(pok, 0, sizeof pok); }
        if (c->op == OP_VSCF || c->op == OP_PSCF) {
            int k = c->op == OP_PSCF;
            auto* q = (const PConst*)(c + 1);
            const float* v = (const float*)(q + 1);
            float (*reg)[4] = k ? ps : vs;
            bool* ok = k ? pok : vok;
            bool all = true;
            for (UINT r = 0; r < q->count && q->start + r < 256; r++) {
                bool same = ok[q->start + r] && memcmp(reg[q->start + r], v + r * 4, 16) == 0;
                sameRegs[k] += same;
                all &= same;
                memcpy(reg[q->start + r], v + r * 4, 16);
                ok[q->start + r] = true;
            }
            calls[k]++;
            regs[k] += q->count;
            sameCalls[k] += all;
        }
        off += c->size;
    }
    printf("%-22s VSCF %6llu calls (%5.1f%% fully redundant), %7llu regs (%5.1f%% same) | PSCF %6llu calls (%5.1f%%), %7llu regs (%5.1f%%)\n",
           name, (unsigned long long)calls[0], 100.0 * sameCalls[0] / std::max<uint64_t>(calls[0], 1), (unsigned long long)regs[0],
           100.0 * sameRegs[0] / std::max<uint64_t>(regs[0], 1), (unsigned long long)calls[1], 100.0 * sameCalls[1] / std::max<uint64_t>(calls[1], 1),
           (unsigned long long)regs[1], 100.0 * sameRegs[1] / std::max<uint64_t>(regs[1], 1));
}

// -states: per frame, how many state calls set what is already set (render/sampler/texture-stage states, textures,
// shaders, declarations, streams, indices) - what a replay could skip on top of the constant uploads
static void StateRedundancy(const char* name, const Frame& F) {
    std::unordered_map<uint64_t, uint64_t> cur;  // (op, slot) -> value hash
    uint64_t calls[OP_MAX] = {}, same[OP_MAX] = {};
    uint64_t draws = 0;
    for (uint32_t off = 0; off < F.buf.size();) {
        const Cmd* c = (const Cmd*)(F.buf.data() + off);
        if (!c->size) break;
        const uint8_t* p = (const uint8_t*)(c + 1);
        uint64_t slot = ~0ull, val = 0;
        switch (c->op) {
        case OP_RS: slot = ((const P2*)p)->a; val = ((const P2*)p)->b; break;
        case OP_SS: slot = ((uint64_t)((const P3*)p)->a << 16) | ((const P3*)p)->b; val = ((const P3*)p)->c; break;
        case OP_TSS: slot = ((uint64_t)((const P3*)p)->a << 16) | ((const P3*)p)->b; val = ((const P3*)p)->c; break;
        case OP_TEX: slot = ((const PPtr*)p)->a; val = (uintptr_t)((const PPtr*)p)->p; break;
        case OP_VS: case OP_PS: case OP_DECL: case OP_IDX: slot = 0; val = (uintptr_t)((const PPtr*)p)->p; break;
        case OP_FVF: slot = 0; val = ((const P2*)p)->a; break;
        case OP_SSRC: { auto* s = (const PSsrc*)p; slot = s->stream; val = Fnv(Fnv(Fnv(1, (uintptr_t)s->vb), s->off), s->stride); break; }
        case OP_SBAPPLY: cur.clear(); break;
        case OP_DP: case OP_DIP: case OP_DPUP: case OP_DIPUP: draws++; break;
        }
        if (slot != ~0ull) {
            uint64_t key = ((uint64_t)c->op << 48) ^ slot;
            auto it = cur.find(key);
            calls[c->op]++;
            if (it != cur.end() && it->second == val) same[c->op]++;
            cur[key] = val;
        }
        off += c->size;
    }
    printf("%-18s %5llu draws:", name, (unsigned long long)draws);
    for (int op : {OP_RS, OP_SS, OP_TSS, OP_TEX, OP_VS, OP_PS, OP_DECL, OP_FVF, OP_SSRC, OP_IDX})
        if (calls[op]) printf("  %s %llu (%.0f%% same)", kOp[op], (unsigned long long)calls[op], 100.0 * same[op] / calls[op]);
    printf("\n");
}

int main(int argc, char** argv) {
    QueryPerformanceFrequency(&g_qpf);
    g_t0 = Qpc();
    g_tid = GetCurrentThreadId();  // (the profiling build samples this thread)
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);  // profile output (mhfz-fps-prof.bin) next to the exe
    if (char* sl = strrchr(g_dir, '\\')) *sl = 0;
    g_modBase = (uintptr_t)GetModuleHandleA(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(g_modBase + ((IMAGE_DOS_HEADER*)g_modBase)->e_lfanew);
    g_modEnd = g_modBase + nt->OptionalHeader.SizeOfImage;
    g_codeBeg = g_modBase + nt->OptionalHeader.BaseOfCode;
    g_codeEnd = g_codeBeg + nt->OptionalHeader.SizeOfCode;
    int reps = 30, threads = 0, seqReps = 1;
    bool redundancy = false, states = false, detail = false, passes = false, additive = false, campaths = false, owners = false, textrep = false;
    const char* seq = nullptr;
    std::vector<const char*> files;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-seq") && i + 1 < argc) seq = argv[++i];
        else if (!strcmp(argv[i], "-detail")) detail = true;
        else if (!strcmp(argv[i], "-passes")) passes = true;
        else if (!strcmp(argv[i], "-additive")) additive = true;
        else if (!strcmp(argv[i], "-campaths")) campaths = true;
        else if (!strcmp(argv[i], "-owners")) owners = true;
        else if (!strcmp(argv[i], "-text")) textrep = true;
        else if (!strcmp(argv[i], "-nonpos") && i + 1 < argc) g_benchNonpos = atoi(argv[++i]);  // override lerp_nonpos
        else if (!strcmp(argv[i], "-texttick") && i + 1 < argc) g_debugTextTick = atoi(argv[++i]);  // the text matcher's choices for one tick of -seq
        else if (!strcmp(argv[i], "-oldcands")) g_oldCands = true;  // the old text candidate search
        else if (!strcmp(argv[i], "-tscan") && i + 1 < argc) g_tScanTick = atoi(argv[++i]);  // -seq: blends off the line at t = 0.5 (0: every tick)
        else if (!strcmp(argv[i], "-zeroscan") && i + 1 < argc) g_intScanTick = atoi(argv[++i]), g_zeroScan = true;  // -seq: blended values passing zero near halfway
        else if (!strcmp(argv[i], "-intscan") && i + 1 < argc) g_intScanTick = atoi(argv[++i]);  // -seq: integer-like blended values (0: every tick)
        else if (!strcmp(argv[i], "-cancel")) g_cancelScan = true;  // -seq: blended constants that nearly cancel halfway
        else if (!strcmp(argv[i], "-uptrace") && i + 1 < argc) g_upTraceTick = atoi(argv[++i]);  // -seq: the screen quads one tick's matching moves
        else if (!strcmp(argv[i], "-tex") && i + 1 < argc) g_texQuery = (uintptr_t)strtoull(argv[++i], nullptr, 16);  // -seq: the draws sampling / drawing into a texture (last tick)
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-checkpredict")) g_checkPredict = true;
        else if (!strcmp(argv[i], "-jumps")) g_debugJumps = true;
        else if (!strcmp(argv[i], "-field")) g_benchField = 1;  // -seq: field_match on
        else if (!strcmp(argv[i], "-noslot")) g_benchNoSlot = true;  // -seq: slot_rows off
        else if (!strcmp(argv[i], "-textold")) g_textOld = true;
        else if (!strcmp(argv[i], "-boneless") && i + 1 < argc) g_bonelessTick = atoi(argv[++i]);  // -seq: unmatched bone-driven draws  // -seq: the text matcher without the night17 changes
        else if (!strcmp(argv[i], "-textkeys") && i + 1 < argc) g_textKeysTick = atoi(argv[++i]);  // -seq: which strings have pixel keys
        else if (!strcmp(argv[i], "-lerpdraw") && i + 3 < argc)  // -seq: tick, PS register, value: one object's changed rows
        {
            g_lerpDrawTick = atoi(argv[++i]), g_lerpDrawReg = atoi(argv[++i]);
            if (g_lerpDrawReg < 0) for (char* s = argv[++i]; *s;) { g_lerpDrawList.push_back((uint32_t)strtoul(s, &s, 10)); if (*s == ',') s++; }
            else g_lerpDrawX = (float)atof(argv[++i]);
        }
        else if (!strcmp(argv[i], "-lerprows") && i + 1 < argc) g_lerpRowsTick = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-nocamfit")) g_benchNoCamFit = true;  // -seq: the camera estimate without the fit vote
        else if (!strcmp(argv[i], "-nogroupcam")) g_benchNoGroupCam = true;
        else if (!strcmp(argv[i], "-anchorall")) g_anchorAll = g_debugJumps = true;  // -seq: the origins' map in every context (debug)  // -seq: groups without their own camera estimate
        else if (!strcmp(argv[i], "-groupdump") && i + 2 < argc) g_groupDumpTick = atoi(argv[++i]), g_groupDumpKey = strtoull(argv[++i], nullptr, 16);  // -seq: one key's instances, both frames
        else if (!strcmp(argv[i], "-campairs")) g_camPairsReport = 1;  // -seq: per tick, which 4x4 uploads follow camera paths
        else if (!strcmp(argv[i], "-campairs2")) g_camPairsReport = 2;
        else if (!strcmp(argv[i], "-passscan") && i + 1 < argc) g_passScanTick = atoi(argv[++i]);  // -seq: lights blended through their object (-1: per tick)
        else if (!strcmp(argv[i], "-slotcheck") && i + 1 < argc) g_slotCheckTick = atoi(argv[++i]);  // -seq: light rows still blended where a light switched (-1: per tick)  // -seq: the PS rows a tick blends
        else if (!strcmp(argv[i], "-dumpmatch") && i + 1 < argc) g_dumpMatch = fopen(argv[++i], "w");  // -seq: per tick, every draw's counterpart
        else if (!strcmp(argv[i], "-drawinfo") && i + 2 < argc) {  // -seq: tick, then draws (comma-separated)
            g_drawInfoTick = atoi(argv[++i]);
            g_drawInfoField = true;
            for (char* s = argv[++i]; *s;) { g_drawInfoList.push_back((uint32_t)strtoul(s, &s, 10)); if (*s == ',') s++; }
        }  // -seq (with -t 1): the rows switched instead of blended  // -seq: check the text field's median against a full sort
        else if (!strcmp(argv[i], "-hash")) g_seqHash = true;  // -seq: per tick, a hash of everything decided
        else if (!strcmp(argv[i], "-rep") && i + 1 < argc) seqReps = atoi(argv[++i]);  // -seq: run the sequence this many times (profiling)
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) threads = atoi(argv[++i]);  // pairing threads (0 = automatic)
        else if (!strcmp(argv[i], "-redundancy")) redundancy = true;
        else if (!strcmp(argv[i], "-states")) states = true;
        else files.push_back(argv[i]);
    }
    if (seq) {
        if (!LoadSequence(seq)) return 1;
        C.pair_threads = threads;
        if (g_benchField >= 0) C.field_match = g_benchField;
        if (g_benchNoSlot) C.slot_rows = 0;
        if (g_benchNoCamFit) C.cam_fit = 0;
        if (g_benchNoGroupCam) C.group_cam = 0;
        for (int r = 0; r < std::max(seqReps, 1); r++) RunSequence(detail);
        if (g_checkPredict) printf("text field medians: %llu calls, %llu differ from a full sort\n", (unsigned long long)g_predictCalls, (unsigned long long)g_predictDiff);
#ifdef FPS_SAMPLER
        ProfSave();
#endif
        return 0;
    }
    double phaseTot[16] = {}, totAll = 0;
    int nAll = 0;
    for (const char* f : files) {
        if (!LoadCapture(f)) continue;
        C.pair_threads = threads;
        if (g_benchNonpos >= 0) C.lerp_nonpos = g_benchNonpos;
        if (states) {
            const char* lf = strrchr(f, '\\');
            StateRedundancy(lf ? lf + 1 : f, g_fr[2]);
            continue;
        }
        if (redundancy) {
            const char* lf = strrchr(f, '\\');
            Redundancy(lf ? lf + 1 : f, g_fr[2]);
            continue;
        }
        Frame &N = g_fr[2], &P = g_fr[1];
        std::vector<double> t;
        memset(&St, 0, sizeof St);
        BuildPairs(N, P);
        uint64_t first = PairHash();
        if (passes) {  // the per-frame scans of the matching, one at a time
            double tO = 1e9, tL = 1e9, tM = 1e9;
            for (int r = 0; r < 20; r++) {
                int64_t a = Qpc(); CollectOwners(N, g_ownN); int64_t b = Qpc(); CollectLive(N, g_liveN); int64_t c = Qpc(); ComputePosMasks(N); int64_t d = Qpc();
                tO = std::min(tO, Ms(b - a)); tL = std::min(tL, Ms(c - b)); tM = std::min(tM, Ms(d - c));
            }
            printf("%s: %zu draws, %zu KB: CollectOwners %.2f ms, CollectLive %.2f ms, ComputePosMasks %.2f ms\n", f, N.draws.size(), N.buf.size() / 1024, tO, tL, tM);
            continue;
        }
        if (textrep) {
            size_t up = 0, scr = 0;
            for (size_t d = 0; d < N.draws.size(); d++) { float b[4]; scr += QuadBox(N, (uint32_t)d, b); }
            for (char m : g_runMatch) up += m == 1;
            printf("%s: %zu screen quads, %llu strings matched whole (%llu quads), %llu carried, screen dropped %llu, moved %llu\n", f, scr,
                   (unsigned long long)St.textRuns, (unsigned long long)St.textRunQuads, (unsigned long long)St.textRunMoved,
                   (unsigned long long)St.screenDropped, (unsigned long long)St.screenSynth);
            continue;
        }
        if (owners) {
            printf("%s:\n", f);
            OwnersReport(N);
            continue;
        }
        if (campaths) {
            printf("%s:\n", f);
            CamPathReport(N, P);
            continue;
        }
        if (additive) {
            printf("%s:\n", f);
            AdditiveReport(N, P);
            continue;
        }
        if (detail) {
            printf("%s:\n", f);
            SeqDetail(N, P);
            ExplainLone(N, P);
            continue;
        }
        memset(&St, 0, sizeof St);
        for (int r = 0; r < reps; r++) {
            // as in the game: last tick's current frame (this P) already has its owner / live lists
            CollectOwners(P, g_ownN);
            CollectLive(P, g_liveN);
            g_ownNSerial = P.serial;
            int64_t a = Qpc();
            BuildPairs(N, P);
            t.push_back(Ms(Qpc() - a));
        }
        std::sort(t.begin(), t.end());
        if (PairHash() != first) printf("%s: NOT REPEATABLE (hash changed between runs)\n", f);
        const char* leaf = strrchr(f, '\\');
        printf("%-22s draws %5zu  pairs %6zu  median %6.2f ms  min %6.2f  hash %016llx\n", leaf ? leaf + 1 : f, N.draws.size(),
               g_pairs.size(), t[t.size() / 2], t[0], (unsigned long long)PairHash());
        for (int k = 0; k < 16; k++) phaseTot[k] += St.phaseMs[k] / reps;
        totAll += t[t.size() / 2];
        nAll++;
    }
    if (nAll) {
        printf("average median %.2f ms over %d captures; phases (ms):", totAll / nAll, nAll);
        for (int k = 0; k < (int)(sizeof kPhase / sizeof *kPhase); k++) printf(" %s %.2f", kPhase[k], phaseTot[k] / nAll);
        printf("\n");
    }
    return 0;
}
