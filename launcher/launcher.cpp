// MHFZ Plus launcher: starts MezeLounge or Rain with the optional English patch and the high-frame-rate mod, and
// stays open until the game closes (add it to Steam as a non-Steam game: Steam then tracks the whole session).
//
//   MHFZ-Plus.exe [meze|rain] [--no-english] [--no-fps] [--restore] [--prepare] [--attach]
//
// Files next to the exe:
//   MHFZ-Plus.ini            settings (game folders, what to use); written by the installer
//   fps\d3d9.dll             the frame-rate mod;   fps\mhfz-fps.ini  its default settings
//   english\<server>\manifest.txt + *.delta    the English patch as differences to the original game files
//   cache\<server>\...       the English files, built from your own originals on first use
//   backup\<server>\...      your original files (put back after every session and by --restore)
//
// MezeLounge: its launcher restores the Japanese files at login; this puts the English ones back right after.
// Rain: its launcher checks every game file before starting the game, so the originals stay in place for that
// check; the English files (and the frame-rate mod) go in when its log says the check passed, and come out again
// when the game closes.
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>
#include <map>

typedef std::wstring wstr;

static wstr g_base;       // folder of the exe
static FILE* g_log;

static void Log(const wchar_t* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(g_log, L"%02d:%02d:%02d.%03d  ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(g_log, fmt, ap);
    va_end(ap);
    fputwc(L'\n', g_log);
    fflush(g_log);
}
static void Fail(const wchar_t* fmt, ...) {
    wchar_t msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 2047, fmt, ap);
    va_end(ap);
    msg[2047] = 0;
    Log(L"ERROR: %ls", msg);
    MessageBoxW(nullptr, msg, L"MHFZ Plus", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
static void Warn(const wchar_t* msg) {
    Log(L"WARNING: %ls", msg);
    MessageBoxW(nullptr, msg, L"MHFZ Plus", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

// ------------------------------------------------------------------------------------------------ paths and files
static wstr Join(const wstr& a, const wstr& b) {
    if (a.empty()) return b;
    if (b.size() > 1 && (b[1] == L':' || (b[0] == L'\\' && b[1] == L'\\'))) return b;  // already absolute
    wstr r = a;
    if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
    return r + b;
}
static wstr Parent(const wstr& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == wstr::npos ? wstr() : p.substr(0, k);
}
static wstr Leaf(const wstr& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == wstr::npos ? p : p.substr(k + 1);
}
static wstr Full(const wstr& p) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 4, buf, nullptr);
    return n ? wstr(buf, n) : p;
}
static bool Exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static void MakeDirs(const wstr& dir) {
    if (dir.empty() || Exists(dir)) return;
    MakeDirs(Parent(dir));
    CreateDirectoryW(dir.c_str(), nullptr);
}
static bool ReadAll(const wstr& p, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (1ll << 31);
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        size_t done = 0;
        while (ok && done < out.size()) {
            DWORD want = (DWORD)std::min<size_t>(out.size() - done, 1 << 24);
            ok = ReadFile(h, out.data() + done, want, &got, nullptr) && got;
            done += got;
        }
    }
    CloseHandle(h);
    return ok;
}
static bool WriteAll(const wstr& p, const std::vector<uint8_t>& data) {
    MakeDirs(Parent(p));
    wstr tmp = p + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        DWORD want = (DWORD)std::min<size_t>(data.size() - done, 1 << 24);
        ok = WriteFile(h, data.data() + done, want, &put, nullptr) && put;
        done += put;
    }
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
}
// Copies with retries (the game or its launcher may hold the file for a moment).
static bool CopyRetry(const wstr& src, const wstr& dst, int tries = 40) {
    MakeDirs(Parent(dst));
    for (int i = 0; i < tries; i++) {
        SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) return true;
        Sleep(250);
    }
    return false;
}
struct Stamp { uint64_t size = 0, mtime = 0; bool ok = false; };
static Stamp StampOf(const wstr& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    Stamp s;
    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) {
        s.size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        s.mtime = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        s.ok = true;
    }
    return s;
}
static bool SameStamp(const wstr& a, const wstr& b) {
    Stamp x = StampOf(a), y = StampOf(b);
    return x.ok && y.ok && x.size == y.size && x.mtime == y.mtime;
}

// SHA-256 (Windows CNG), as lowercase hex
static std::string Sha256(const uint8_t* data, size_t n) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hh = nullptr;
    uint8_t digest[32] = {};
    std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0) {
            size_t done = 0;
            while (done < n) {
                ULONG part = (ULONG)std::min<size_t>(n - done, 1 << 24);
                BCryptHashData(hh, (PUCHAR)data + done, part, 0);
                done += part;
            }
            if (BCryptFinishHash(hh, digest, 32, 0) == 0) {
                static const char* hex = "0123456789abcdef";
                for (uint8_t b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
            }
            BCryptDestroyHash(hh);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return out;
}
static std::string FileSha256(const wstr& p) {
    std::vector<uint8_t> d;
    return ReadAll(p, d) ? Sha256(d.data(), d.size()) : std::string();
}

// ------------------------------------------------------------------------------------------------ processes
static bool AnyRunning(const std::vector<wstr>& names) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof pe;
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        wstr exe = pe.szExeFile;
        size_t dot = exe.find_last_of(L'.');
        wstr stem = dot == wstr::npos ? exe : exe.substr(0, dot);
        for (const wstr& n : names) if (_wcsicmp(stem.c_str(), n.c_str()) == 0) { found = true; break; }
    }
    CloseHandle(snap);
    return found;
}
static std::vector<wstr> SplitList(const wstr& s) {
    std::vector<wstr> out;
    wstr cur;
    for (wchar_t c : s + L",") {
        if (c == L',' || c == L';') {
            while (!cur.empty() && cur.back() == L' ') cur.pop_back();
            size_t a = cur.find_first_not_of(L' ');
            if (a != wstr::npos) out.push_back(cur.substr(a));
            cur.clear();
        } else cur += c;
    }
    return out;
}
static bool StartProgram(const wstr& exe, const wstr& args) {
    wstr cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    wstr dir = Parent(exe);
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        // some launchers ask for elevation
        if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
            SHELLEXECUTEINFOW sei = {sizeof sei};
            sei.lpVerb = L"runas";
            sei.lpFile = exe.c_str();
            sei.lpParameters = args.c_str();
            sei.lpDirectory = dir.c_str();
            sei.nShow = SW_SHOWNORMAL;
            return ShellExecuteExW(&sei);
        }
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ------------------------------------------------------------------------------------------------ settings
struct Profile {
    wstr name;           // meze / rain
    wstr game;           // folder holding dat\ and where d3d9.dll goes
    wstr start;          // program to start
    wstr startArgs;
    std::vector<wstr> procs;  // processes that mean "the session is still going"
    wstr client;         // Rain: the game process
    wstr launcherLogs;   // Rain: launcher log folder (empty = MezeLounge style)
    bool english = true, fps = true;
    bool englishSafe = false;  // english=safe: only the file types proven in game (menus/items, quests, town dialogue)
    bool fpsSwap = false;  // put the mod in only after the launcher's file check (and take it out afterwards)
};
static wstr Ini(const wstr& file, const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(sec, key, def, buf, 2048, file.c_str());
    return buf;
}
static bool LoadProfile(const wstr& name, Profile& p) {
    wstr ini = Join(g_base, L"MHFZ-Plus.ini");
    p.name = name;
    p.game = Ini(ini, name.c_str(), L"game", L"");
    if (p.game.empty()) return false;
    p.game = Full(Join(g_base, p.game));
    p.start = Full(Join(p.game, Ini(ini, name.c_str(), L"start", name == L"rain" ? L"..\\Launcher\\rain-server-launcher.exe" : L"mhf.exe")));
    p.startArgs = Ini(ini, name.c_str(), L"start_args", L"");
    p.procs = SplitList(Ini(ini, name.c_str(), L"processes", name == L"rain" ? L"rain-server-launcher,client" : L"mhf"));
    p.client = Ini(ini, name.c_str(), L"client", name == L"rain" ? L"client" : L"");
    wstr logs = Ini(ini, name.c_str(), L"launcher_logs", name == L"rain" ? L"..\\Launcher\\Logs" : L"");
    p.launcherLogs = logs.empty() ? wstr() : Full(Join(p.game, logs));
    wstr eng = Ini(ini, name.c_str(), L"english", L"1");
    p.englishSafe = _wcsicmp(eng.c_str(), L"safe") == 0;
    p.english = p.englishSafe || _wtoi(eng.c_str()) != 0;
    p.fps = _wtoi(Ini(ini, name.c_str(), L"fps", L"1").c_str()) != 0;
    wstr mode = Ini(ini, name.c_str(), L"fps_mode", name == L"rain" ? L"swap" : L"stay");
    p.fpsSwap = _wcsicmp(mode.c_str(), L"swap") == 0;
    return true;
}

// ------------------------------------------------------------------------------------------------ English patch
struct PatchFile {
    wstr rel;                      // path inside the game folder, e.g. dat\mhfdat.bin
    std::string origHash, newHash;
    uint64_t origSize = 0, newSize = 0;
    wstr delta;                    // delta file name in the english folder of the server
};
static std::vector<PatchFile> g_files;

static bool LoadManifest(const wstr& server) {
    g_files.clear();
    wstr path = Join(g_base, L"english\\" + server + L"\\manifest.txt");
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        // rel|origSize|origSha256|newSize|newSha256|deltaFile
        char* fld[6] = {};
        int n = 0;
        fld[n++] = line;
        for (char* c = line; *c && n < 6; c++) if (*c == '|') { *c = 0; fld[n++] = c + 1; }
        if (n < 6) continue;
        for (char* c = fld[5]; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
        PatchFile pf;
        int wl = MultiByteToWideChar(CP_UTF8, 0, fld[0], -1, nullptr, 0);
        std::vector<wchar_t> w(wl);
        MultiByteToWideChar(CP_UTF8, 0, fld[0], -1, w.data(), wl);
        pf.rel = w.data();
        pf.origSize = _strtoui64(fld[1], nullptr, 10);
        pf.origHash = fld[2];
        pf.newSize = _strtoui64(fld[3], nullptr, 10);
        pf.newHash = fld[4];
        wl = MultiByteToWideChar(CP_UTF8, 0, fld[5], -1, nullptr, 0);
        w.assign(wl, 0);
        MultiByteToWideChar(CP_UTF8, 0, fld[5], -1, w.data(), wl);
        pf.delta = w.data();
        g_files.push_back(pf);
    }
    fclose(f);
    return !g_files.empty();
}

// Delta format: "MHFD" u32 version(1) u32 origSize u32 newSize, then ops until END:
//   1 COPY  u32 offset u32 length   (from the original)       2 ADD  u32 length + bytes       0 END
static bool ApplyDelta(const std::vector<uint8_t>& orig, const std::vector<uint8_t>& delta, std::vector<uint8_t>& out) {
    auto rd = [&](size_t& p, uint32_t& v) {
        if (p + 4 > delta.size()) return false;
        memcpy(&v, delta.data() + p, 4);
        p += 4;
        return true;
    };
    if (delta.size() < 16 || memcmp(delta.data(), "MHFD", 4) != 0) return false;
    size_t p = 4;
    uint32_t ver, osz, nsz;
    if (!rd(p, ver) || !rd(p, osz) || !rd(p, nsz) || ver != 1 || osz != orig.size()) return false;
    out.clear();
    out.reserve(nsz);
    while (p < delta.size()) {
        uint8_t op = delta[p++];
        if (op == 0) break;
        uint32_t a, b;
        if (op == 1) {
            if (!rd(p, a) || !rd(p, b) || (uint64_t)a + b > orig.size()) return false;
            out.insert(out.end(), orig.begin() + a, orig.begin() + a + b);
        } else if (op == 2) {
            if (!rd(p, a) || p + a > delta.size()) return false;
            out.insert(out.end(), delta.begin() + p, delta.begin() + p + a);
            p += a;
        } else return false;
    }
    return out.size() == nsz;
}

// The original copy of a game file: from our backup, or the game folder if it is still the original.
static bool OriginalOf(const Profile& pr, const PatchFile& pf, std::vector<uint8_t>& data, bool& fromGame) {
    wstr bak = Join(g_base, L"backup\\" + pr.name + L"\\" + pf.rel);
    if (ReadAll(bak, data) && Sha256(data.data(), data.size()) == pf.origHash) { fromGame = false; return true; }
    wstr game = Join(pr.game, pf.rel);
    if (ReadAll(game, data) && Sha256(data.data(), data.size()) == pf.origHash) { fromGame = true; return true; }
    return false;
}

// Builds cache\<server>\ from the deltas and the user's originals; also makes sure every original is backed up.
// Files whose original doesn't match (different game version) are left out, with one warning.
// Cache and backup are checked by hash every time (~0.2 s), so a newer patch or a damaged file is rebuilt.
static std::vector<PatchFile> g_ready;  // files that can be swapped (cache and backup both present)
static bool PrepareEnglish(const Profile& pr, bool quiet) {
    g_ready.clear();
    if (!LoadManifest(pr.name)) {
        if (!quiet) Warn(L"The English patch files are missing (english\\...\\manifest.txt). Starting without it.");
        return false;
    }
    int built = 0, skipped = 0;
    wstr firstBad;
    for (const PatchFile& pf : g_files) {
        if (pr.englishSafe) {
            wstr r = pf.rel;
            for (auto& c : r) c = towlower(c);
            if (r != L"dat\\mhfdat.bin" && r != L"dat\\mhfpac.bin" && r != L"dat\\mhfinf.bin" && r.rfind(L"dat\\stage\\", 0) != 0) continue;
        }
        wstr cache = Join(g_base, L"cache\\" + pr.name + L"\\" + pf.rel);
        wstr bak = Join(g_base, L"backup\\" + pr.name + L"\\" + pf.rel);
        bool haveCache = StampOf(cache).size == pf.newSize && FileSha256(cache) == pf.newHash;
        bool haveBak = StampOf(bak).size == pf.origSize && FileSha256(bak) == pf.origHash;
        if (haveCache && haveBak) { g_ready.push_back(pf); continue; }
        std::vector<uint8_t> orig, delta, out;
        bool fromGame = false;
        if (!OriginalOf(pr, pf, orig, fromGame)) {
            skipped++;
            if (firstBad.empty()) firstBad = pf.rel;
            Log(L"english: no matching original for %ls (different game version?) - left out", pf.rel.c_str());
            continue;
        }
        if (!haveBak && !WriteAll(bak, orig)) { Log(L"english: cannot write backup %ls", bak.c_str()); skipped++; continue; }
        if (!haveCache) {
            if (!ReadAll(Join(g_base, L"english\\" + pr.name + L"\\" + pf.delta), delta) || !ApplyDelta(orig, delta, out) ||
                Sha256(out.data(), out.size()) != pf.newHash) {
                Log(L"english: building %ls failed", pf.rel.c_str());
                skipped++;
                continue;
            }
            if (!WriteAll(cache, out)) { Log(L"english: cannot write %ls", cache.c_str()); skipped++; continue; }
            built++;
        }
        g_ready.push_back(pf);
    }
    Log(L"english: %d files ready (%d built now), %d left out", (int)g_ready.size(), built, skipped);
    if (skipped && !quiet) {
        wchar_t msg[1024];
        swprintf(msg, 1024,
                 L"%d of %d English files don't match your game files (first: %ls) and are left out.\n\n"
                 L"Your client is probably a different version than the patch was made for. The rest of the "
                 L"translation still works.",
                 skipped, (int)g_files.size(), firstBad.c_str());
        Warn(msg);
    }
    return !g_ready.empty();
}
// What a game file is now. Only files that are exactly our original or our English version are ever overwritten:
// anything else is a game update (or another mod) and is left alone.
enum FileState { F_MISSING, F_UNKNOWN, F_ORIGINAL, F_ENGLISH };
static FileState StateOf(const Profile& pr, const PatchFile& pf) {
    wstr g = Join(pr.game, pf.rel);
    Stamp s = StampOf(g);
    if (!s.ok) return F_MISSING;
    // copies keep the source's timestamp, so after our own copies size + time identify the file
    if (SameStamp(g, Join(g_base, L"cache\\" + pr.name + L"\\" + pf.rel))) return F_ENGLISH;
    if (SameStamp(g, Join(g_base, L"backup\\" + pr.name + L"\\" + pf.rel))) return F_ORIGINAL;
    if (s.size != pf.origSize && s.size != pf.newSize) return F_UNKNOWN;
    std::string h = FileSha256(g);
    return h == pf.origHash ? F_ORIGINAL : h == pf.newHash ? F_ENGLISH : F_UNKNOWN;
}
// A game file that changed into something we don't know: stop swapping it for the rest of this session.
static void DropFile(size_t k, const wchar_t* why) {
    Log(L"english: %ls is %ls - left alone (game update? the English patch may need updating)", g_ready[k].rel.c_str(), why);
    g_ready.erase(g_ready.begin() + k);
}
static int ApplyEnglish(const Profile& pr) {
    int ok = 0;
    for (size_t k = 0; k < g_ready.size();) {
        const PatchFile& pf = g_ready[k];
        wstr cache = Join(g_base, L"cache\\" + pr.name + L"\\" + pf.rel), dst = Join(pr.game, pf.rel);
        FileState st = StateOf(pr, pf);
        if (st == F_UNKNOWN || st == F_MISSING) { DropFile(k, st == F_MISSING ? L"missing" : L"a version we don't know"); continue; }
        if (SameStamp(cache, dst) || CopyRetry(cache, dst)) ok++;  // (an English file by hash gets our timestamp too)
        else Log(L"english: could not put in %ls (file locked)", pf.rel.c_str());
        k++;
    }
    return ok;
}
// align: also copy over originals that only differ in timestamp, so later checks need no hashing
static int RestoreOriginals(const Profile& pr, bool all, bool align = false) {
    int ok = 0;
    if (all && g_files.empty()) LoadManifest(pr.name);
    for (const PatchFile& pf : all ? g_files : g_ready) {
        wstr bak = Join(g_base, L"backup\\" + pr.name + L"\\" + pf.rel), dst = Join(pr.game, pf.rel);
        if (!Exists(bak)) continue;
        FileState st = StateOf(pr, pf);
        if (st == F_ORIGINAL && (!align || SameStamp(bak, dst))) { ok++; continue; }
        if (st != F_ENGLISH && st != F_ORIGINAL) {
            Log(L"english: %ls is %ls - not touched", pf.rel.c_str(), st == F_MISSING ? L"missing" : L"a version we don't know (game update?)");
            continue;
        }
        if (CopyRetry(bak, dst)) ok++;
        else Log(L"english: could not put back %ls (file locked)", pf.rel.c_str());
    }
    return ok;
}

// ------------------------------------------------------------------------------------------------ frame-rate mod
// Only ever touches a d3d9.dll that is ours (same size and hash as fps\d3d9.dll, or one with our marker string),
// so another d3d9.dll (ReShade, dgVoodoo...) is never deleted or overwritten.
static bool IsOurDll(const wstr& path) {
    std::vector<uint8_t> d;
    if (!ReadAll(path, d)) return false;
    static const char mark[] = "mhfz-fps.ini";
    return std::search(d.begin(), d.end(), mark, mark + sizeof mark - 1) != d.end();
}
// Swap mode (Rain): outside a session the game folder holds nothing of ours, so the mod's settings, logs and any
// pairing captures move to fps-state\<server> in the MHFZ Plus folder when it comes out, and the settings come back
// with it.
static wstr StateDir(const Profile& pr) { return Join(g_base, L"fps-state\\" + pr.name); }
static void TidyFpsFiles(const Profile& pr) {
    wstr st = StateDir(pr);
    MakeDirs(st);
    wstr log = Join(pr.game, L"mhfz-fps.log");
    if (Exists(log)) MoveFileExW(Join(st, L"mhfz-fps.log").c_str(), Join(st, L"mhfz-fps.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    for (const wchar_t* f : {L"mhfz-fps.ini", L"mhfz-fps.log", L"mhfz-fps-dump.txt"}) {
        wstr src = Join(pr.game, f);
        if (Exists(src) && !MoveFileExW(src.c_str(), Join(st, f).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
            Log(L"fps: could not move %ls out of the game folder", f);
    }
    DeleteFileW(Join(pr.game, L"mhfz-fps.prev.log").c_str());  // (the last two sessions' logs are in fps-state)
    // logs of a d3d9 backend under the mod (DXVK writes <exe>_d3d9.log / _dxgi.log when its log is on)
    for (const wchar_t* pat : {L"*_d3d9.log", L"*_dxgi.log"}) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Join(pr.game, pat).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do MoveFileExW(Join(pr.game, fd.cFileName).c_str(), Join(st, fd.cFileName).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (const wchar_t* d : {L"paircap", L"retro", L"captures", L"selfcheck", L"trace"}) {
        wstr src = Join(pr.game, d);
        if (!Exists(src)) continue;
        wstr dst = Join(st, d);
        for (int k = 2; Exists(dst); k++) dst = Join(st, wstr(d) + L"-" + std::to_wstring(k));
        if (!MoveFileExW(src.c_str(), dst.c_str(), 0)) Log(L"fps: could not move the %ls folder out of the game folder", d);
    }
}
static bool PutFpsMod(const Profile& pr) {
    wstr src = Join(g_base, L"fps\\d3d9.dll"), dst = Join(pr.game, L"d3d9.dll");
    if (!Exists(src)) { Log(L"fps: fps\\d3d9.dll missing"); return false; }
    if (Exists(dst) && !IsOurDll(dst)) {
        Warn(L"There is already a different d3d9.dll in the game folder (another mod?). The frame-rate mod is not "
             L"installed this time; remove or rename that file to use it.");
        return false;
    }
    if (!(Exists(dst) && FileSha256(dst) == FileSha256(src)) && !CopyRetry(src, dst)) {
        Log(L"fps: could not copy d3d9.dll into %ls", pr.game.c_str());
        return false;
    }
    // the settings: the player's own (kept in fps-state\<server> between sessions in swap mode), else the defaults;
    // an ini already in the game folder is never overwritten
    wstr iniDst = Join(pr.game, L"mhfz-fps.ini"), kept = Join(StateDir(pr), L"mhfz-fps.ini"), iniSrc = Join(g_base, L"fps\\mhfz-fps.ini");
    if (!Exists(iniDst)) {
        if (pr.fpsSwap && Exists(kept)) CopyRetry(kept, iniDst);
        else if (Exists(iniSrc)) CopyRetry(iniSrc, iniDst);
    }
    return true;
}
static void RemoveFpsMod(const Profile& pr) {
    wstr dst = Join(pr.game, L"d3d9.dll");
    if (!Exists(dst)) { if (pr.fpsSwap) TidyFpsFiles(pr); return; }
    if (!IsOurDll(dst)) return;
    for (int i = 0; i < 40 && Exists(dst); i++) {
        if (DeleteFileW(dst.c_str())) break;
        Sleep(250);
    }
    if (Exists(dst)) Log(L"fps: could not remove d3d9.dll (in use)");
    else if (pr.fpsSwap) TidyFpsFiles(pr);
}

// ------------------------------------------------------------------------------------------------ Rain launcher log
// Counts "verification successful" / "invalid file" in the newest launcher log written since 'since'.
static void ScanLauncherLog(const wstr& dir, const FILETIME& since, int& passes, int& fails, wstr& lastBad) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Join(dir, L"*.log").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    wstr best;
    FILETIME bestT = since;
    do {
        if (CompareFileTime(&fd.ftLastWriteTime, &bestT) >= 0) { bestT = fd.ftLastWriteTime; best = Join(dir, fd.cFileName); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (best.empty()) return;
    std::vector<uint8_t> d;
    if (!ReadAll(best, d)) return;
    std::string s(d.begin(), d.end());
    passes = 0;
    fails = 0;
    for (size_t p = 0; (p = s.find("verification successful", p)) != std::string::npos; p++) passes++;
    for (size_t p = 0; (p = s.find("invalid file", p)) != std::string::npos; p++) {
        fails++;
        size_t e = s.find('\n', p);
        size_t b = s.rfind('\n', p);
        std::string line = s.substr(b == std::string::npos ? 0 : b + 1, (e == std::string::npos ? s.size() : e) - (b == std::string::npos ? 0 : b + 1));
        lastBad = wstr(line.begin(), line.end());
    }
}

// ------------------------------------------------------------------------------------------------ sessions
// Files of ours that are not used this session (english=safe, or not buildable) go back to the original.
static void RestoreUnused(const Profile& pr) {
    for (const PatchFile& pf : g_files) {
        bool used = false;
        for (const PatchFile& r : g_ready) used |= r.rel == pf.rel;
        wstr bak = Join(g_base, L"backup\\" + pr.name + L"\\" + pf.rel);
        if (!used && Exists(bak) && StateOf(pr, pf) == F_ENGLISH && CopyRetry(bak, Join(pr.game, pf.rel)))
            Log(L"english: %ls not used this time - original put back", pf.rel.c_str());
    }
}

static int RunMeze(const Profile& pr, bool english) {
    if (pr.fps) PutFpsMod(pr); else RemoveFpsMod(pr);
    if (english) {
        RestoreUnused(pr);
        Log(L"english: %d/%d files in place", ApplyEnglish(pr), (int)g_ready.size());
    } else RestoreOriginals(pr, true);
    if (!StartProgram(pr.start, pr.startArgs)) { Fail(L"Could not start %ls", pr.start.c_str()); return 1; }
    Log(L"started %ls", pr.start.c_str());
    Sleep(500);
    DWORD lastSeen = GetTickCount();
    std::map<wstr, DWORD> unknownSince;  // files in an unknown state, since when
    while (true) {
        if (AnyRunning(pr.procs)) lastSeen = GetTickCount();
        else if (GetTickCount() - lastSeen > 15000) break;
        if (english) {
            // the launcher put originals back (at login): wait until it has stopped writing, then put all English
            // files back at once, text files first (the manifest lists them first)
            std::vector<size_t> changed;
            for (size_t k = 0; k < g_ready.size(); k++)
                if (!SameStamp(Join(g_base, L"cache\\" + pr.name + L"\\" + g_ready[k].rel), Join(pr.game, g_ready[k].rel))) changed.push_back(k);
            if (!changed.empty()) {
                std::vector<uint64_t> prev(changed.size(), ~0ull);
                for (int round = 0; round < 25; round++) {
                    bool stable = true;
                    for (size_t k = 0; k < changed.size(); k++) {
                        Stamp s = StampOf(Join(pr.game, g_ready[changed[k]].rel));
                        uint64_t v = s.size ^ (s.mtime * 31);
                        if (v != prev[k]) stable = false;
                        prev[k] = v;
                    }
                    if (stable) break;
                    Sleep(60);
                }
                int ok = 0, later = 0;
                std::vector<size_t> drop;
                for (size_t k : changed) {
                    const PatchFile& pf = g_ready[k];
                    FileState st = StateOf(pr, pf);
                    if (st == F_ORIGINAL || st == F_ENGLISH) {  // (English by hash: copied for our timestamp)
                        unknownSince.erase(pf.rel);
                        if (CopyRetry(Join(g_base, L"cache\\" + pr.name + L"\\" + pf.rel), Join(pr.game, pf.rel))) ok++;
                    } else {
                        // still being written, or a new version from a game update: give it a few seconds, then leave it
                        auto it = unknownSince.emplace(pf.rel, GetTickCount()).first;
                        if (GetTickCount() - it->second > 5000) drop.push_back(k);
                        else later++;
                    }
                }
                for (size_t i = drop.size(); i-- > 0;) DropFile(drop[i], L"a version we don't know");
                if (ok || drop.size()) Log(L"launcher replaced %d file(s) -> %d English file(s) put back%ls", (int)changed.size(), ok,
                                           later ? L", some still changing" : L"");
            }
        }
        Sleep(100);
    }
    Log(L"game closed");
    return 0;
}

static int RunRain(const Profile& pr, bool english, bool attach) {
    FILETIME t0;
    GetSystemTimeAsFileTime(&t0);
    int passes = 0, fails = 0;
    wstr lastBad;
    bool inGame = false, everInGame = false;
    auto applyAll = [&](const wchar_t* why) {
        DWORD a = GetTickCount();
        bool mod = pr.fps && pr.fpsSwap ? PutFpsMod(pr) : false;  // first: the game loads it before any text
        int n = english ? ApplyEnglish(pr) : 0;
        Log(L"%ls -> English %d/%d files%ls (%u ms, game %ls)", why, n, english ? (int)g_ready.size() : 0,
            mod ? L", frame-rate mod in" : L"", GetTickCount() - a, AnyRunning({pr.client}) ? L"running" : L"not started yet");
    };
    auto restoreAll = [&]() {
        if (english) RestoreOriginals(pr, false);
        if (pr.fps && pr.fpsSwap) RemoveFpsMod(pr);
    };
    if (attach) {
        if (!AnyRunning({pr.client})) { Fail(L"The game (%ls) is not running.", pr.client.c_str()); return 1; }
        FILETIME old = t0;
        ULARGE_INTEGER u;
        u.LowPart = old.dwLowDateTime;
        u.HighPart = old.dwHighDateTime;
        u.QuadPart -= 12ull * 3600 * 10000000;
        old.dwLowDateTime = u.LowPart;
        old.dwHighDateTime = u.HighPart;
        ScanLauncherLog(pr.launcherLogs, old, passes, fails, lastBad);
        applyAll(L"attached to the running game");
        inGame = true;
    } else {
        // originals in place for the launcher's check; the mod stays out unless told to stay
        RestoreOriginals(pr, true, true);
        if (pr.fps && !pr.fpsSwap) PutFpsMod(pr);
        else RemoveFpsMod(pr);
        if (!StartProgram(pr.start, pr.startArgs)) { Fail(L"Could not start %ls", pr.start.c_str()); return 1; }
        Log(L"originals in place, started %ls - press Play as usual", pr.start.c_str());
    }
    DWORD lastSeen = GetTickCount();
    while (true) {
        int p = passes, f = fails;
        ScanLauncherLog(pr.launcherLogs, t0, p, f, lastBad);
        if (p > passes) { passes = p; applyAll(L"launcher check passed"); }
        if (f > fails) { fails = f; Log(L"LAUNCHER CHECK FAILED: %ls", lastBad.c_str()); }
        if (AnyRunning({pr.client})) {
            if (!inGame) Log(L"game started");
            inGame = true;
        } else if (inGame) {
            inGame = false;
            restoreAll();
            Log(L"game closed -> originals restored");
        }
        // done once nothing runs any more: after a game, 10 s (Steam shows "running" until then); before the game
        // started, a minute (a launcher may restart itself to update)
        if (AnyRunning(pr.procs)) lastSeen = GetTickCount();
        else if (GetTickCount() - lastSeen > (everInGame ? 10000u : 60000u)) break;
        everInGame |= inGame;
        Sleep(10);
    }
    restoreAll();
    Log(L"launcher closed, originals in place");
    return 0;
}

// Can this process create files in dir? (A game under Program Files needs admin rights.)
static bool CanWrite(const wstr& dir) {
    HANDLE h = CreateFileW(Join(dir, L"mhfz-plus-write-test.tmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
static void OpenLog(const wstr& name) {
    wstr log = Join(g_base, L"MHFZ-Plus-" + name + L".log");
    MoveFileExW(log.c_str(), Join(g_base, L"MHFZ-Plus-" + name + L".prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    g_log = _wfopen(log.c_str(), L"w, ccs=UTF-8");
}
static const wchar_t* kVersion = L"1.3";

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
    g_base = Parent(exe);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wstr server;
    bool noEnglish = false, noFps = false, restore = false, prepare = false, attach = false;
    for (int i = 1; i < argc; i++) {
        wstr a = argv[i];
        for (auto& c : a) c = towlower(c);
        if (a == L"--no-english" || a == L"--no-translation") noEnglish = true;
        else if (a == L"--no-fps") noFps = true;
        else if (a == L"--restore") restore = true;
        else if (a == L"--prepare") prepare = true;
        else if (a == L"--attach") attach = true;
        else if (a == L"--version") { MessageBoxW(nullptr, (L"MHFZ Plus " + wstr(kVersion)).c_str(), L"MHFZ Plus", MB_OK); return 0; }
        else if (!a.empty() && a[0] != L'-') server = a;
    }
    wstr ini = Join(g_base, L"MHFZ-Plus.ini");
    if (server.empty()) server = Ini(ini, L"general", L"server", L"meze");

    // --restore all: every configured server (used by the uninstaller and the "restore" shortcut)
    if (restore && server == L"all") {
        OpenLog(L"restore");
        Log(L"MHFZ Plus %ls: restore all", kVersion);
        int rc = 0;
        for (const wchar_t* s : {L"meze", L"rain"}) {
            Profile pr;
            if (!LoadProfile(s, pr) || !Exists(pr.game)) continue;
            HANDLE m = CreateMutexW(nullptr, TRUE, (L"MHFZ-Plus-" + wstr(s)).c_str());
            if (GetLastError() == ERROR_ALREADY_EXISTS || AnyRunning(pr.procs)) {
                Log(L"%ls: game or MHFZ Plus still running - skipped", s);
                rc = 1;
            } else {
                g_files.clear();
                int n = RestoreOriginals(pr, true);
                RemoveFpsMod(pr);
                Log(L"%ls: %d original files in place, frame-rate mod removed", s, n);
            }
            if (m) CloseHandle(m);
        }
        if (g_log) fclose(g_log);
        return rc;
    }

    OpenLog(server);
    Log(L"MHFZ Plus %ls, server %ls", kVersion, server.c_str());
    Profile pr;
    if (!LoadProfile(server, pr)) {
        Fail(L"No game folder set for \"%ls\" in MHFZ-Plus.ini. Run Install.cmd first.", server.c_str());
        return 1;
    }
    if (noFps) pr.fps = false;
    bool english = pr.english && !noEnglish;
    if (!Exists(pr.game)) { Fail(L"Game folder not found: %ls", pr.game.c_str()); return 1; }

    // one session per server at a time
    HANDLE mutex = CreateMutexW(nullptr, TRUE, (L"MHFZ-Plus-" + server).c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!restore && !prepare) Fail(L"MHFZ Plus is already running for %ls.", server.c_str());
        return 1;
    }

    if (restore) {
        if (AnyRunning(pr.procs)) { Fail(L"Close the game and its launcher first."); return 1; }
        int n = RestoreOriginals(pr, true);
        RemoveFpsMod(pr);
        Log(L"restore: %d original files in place, frame-rate mod removed", n);
        return 0;
    }
    if (prepare) {
        PrepareEnglish(pr, true);
        Log(L"prepare: %d files ready", (int)g_ready.size());
        return g_ready.empty() && !g_files.empty() ? 2 : 0;
    }
    if (!attach && AnyRunning(pr.procs)) {
        Fail(L"The game or its launcher is already running. Close it and start it from MHFZ Plus.");
        return 1;
    }
    if ((english || pr.fps) && !(CanWrite(pr.game) && (!english || !Exists(Join(pr.game, L"dat")) || CanWrite(Join(pr.game, L"dat"))))) {
        Fail(L"MHFZ Plus cannot change files in the game folder:\n%ls\n\nMove the game to a folder outside \"Program Files\", "
             L"or run MHFZ Plus as administrator. Starting the game without English and the frame-rate mod.", pr.game.c_str());
        english = false;
        pr.fps = false;
    }
    if (english && !PrepareEnglish(pr, false)) english = false;
    if (!english) Log(L"english: off");
    int rc = pr.launcherLogs.empty() ? RunMeze(pr, english) : RunRain(pr, english, attach);
    if (mutex) CloseHandle(mutex);
    if (g_log) fclose(g_log);
    return rc;
}