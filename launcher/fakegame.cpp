// Simulation of the game programs MHFZ Plus starts, for testing without the game. Behaviour by exe name:
//   mhf.exe                    MezeLounge: after 2 s "login" puts the originals back (from ..\pristine), then
//                              reports which files are English/original at 6 s and exits at 8 s
//   rain-server-launcher.exe   Rain launcher: checks every game file against ..\..\pristine (and no d3d9.dll unless
//                              allow-d3d9.txt exists), logs like the real one, starts ..\Client\client.exe, waits for it
//   client.exe                 Rain client: at 1 s ("GET_TXT") reports which files are English and whether d3d9.dll
//                              is present, exits at 4 s
// Reports are appended to report.txt in the simulation root.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

static std::wstring Dir(const std::wstring& p) { return p.substr(0, p.find_last_of(L'\\')); }
static std::wstring g_root;
static void Report(const wchar_t* who, const std::wstring& msg) {
    FILE* f = _wfopen((g_root + L"\\report.txt").c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%02d:%02d:%02d.%03d %ls: %ls\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, who, msg.c_str());
    fclose(f);
}
static bool SameFile(const std::wstring& a, const std::wstring& b) {
    HANDLE x = CreateFileW(a.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE y = CreateFileW(b.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    bool same = x != INVALID_HANDLE_VALUE && y != INVALID_HANDLE_VALUE;
    if (same) {
        LARGE_INTEGER sx, sy;
        GetFileSizeEx(x, &sx);
        GetFileSizeEx(y, &sy);
        same = sx.QuadPart == sy.QuadPart;
        std::vector<char> bx(1 << 20), by(1 << 20);
        DWORD gx, gy;
        while (same && ReadFile(x, bx.data(), 1 << 20, &gx, nullptr) && ReadFile(y, by.data(), 1 << 20, &gy, nullptr) && gx) {
            same = gx == gy && memcmp(bx.data(), by.data(), gx) == 0;
        }
    }
    if (x != INVALID_HANDLE_VALUE) CloseHandle(x);
    if (y != INVALID_HANDLE_VALUE) CloseHandle(y);
    return same;
}
// Compares every file under game\dat with pristine (originals) and english (expected English files).
static void Census(const std::wstring& game, const std::wstring& pristine, const std::wstring& english, int& orig, int& eng, int& other) {
    orig = eng = other = 0;
    std::vector<std::wstring> stack{L"dat"};
    while (!stack.empty()) {
        std::wstring rel = stack.back();
        stack.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((pristine + L"\\" + rel + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring r = rel + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { stack.push_back(r); continue; }
            if (SameFile(game + L"\\" + r, pristine + L"\\" + r)) orig++;
            else if (SameFile(game + L"\\" + r, english + L"\\" + r)) eng++;
            else other++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
static void CopyTree(const std::wstring& from, const std::wstring& to, const std::wstring& rel = L"") {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((from + rel + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyTree(from, to, rel + L"\\" + n);
        else CopyFileW((from + rel + L"\\" + n).c_str(), (to + rel + L"\\" + n).c_str(), FALSE);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring me = exe, name = me.substr(me.find_last_of(L'\\') + 1), dir = Dir(me);
    if (_wcsicmp(name.c_str(), L"mhf.exe") == 0) {
        g_root = Dir(dir);  // sim\meze\mhf.exe
        std::wstring pristine = g_root + L"\\pristine-meze", english = g_root + L"\\english-meze";
        int o, e, x;
        Census(dir, pristine, english, o, e, x);
        Report(L"mhf", L"start: " + std::to_wstring(e) + L" English, " + std::to_wstring(o) + L" original, " + std::to_wstring(x) + L" other; d3d9.dll " + (GetFileAttributesW((dir + L"\\d3d9.dll").c_str()) != INVALID_FILE_ATTRIBUTES ? L"present" : L"absent"));
        Sleep(2000);
        CopyTree(pristine, dir);  // login: the launcher puts the originals back
        Report(L"mhf", L"login: originals restored by the launcher");
        Sleep(4000);
        Census(dir, pristine, english, o, e, x);
        Report(L"mhf", L"in game: " + std::to_wstring(e) + L" English, " + std::to_wstring(o) + L" original, " + std::to_wstring(x) + L" other");
        Sleep(2000);
        Report(L"mhf", L"exit");
        return 0;
    }
    if (_wcsicmp(name.c_str(), L"rain-server-launcher.exe") == 0) {
        std::wstring rain = Dir(dir);  // sim\rain\Launcher\x.exe
        g_root = Dir(rain);
        std::wstring client = rain + L"\\Client", pristine = g_root + L"\\pristine-rain", english = g_root + L"\\english-rain";
        CreateDirectoryW((dir + L"\\Logs").c_str(), nullptr);
        std::wstring log = dir + L"\\Logs\\rfllog-sim.log";
        FILE* f = _wfopen(log.c_str(), L"w");
        fprintf(f, "[INFO][2026/10/07 01:00:00] Log file open.\n");
        fflush(f);
        Sleep(1500);  // login
        fprintf(f, "[GAME][2026/10/07 01:00:01] File verification process started. (7346 entries)\n");
        fflush(f);
        Sleep(800);
        int o, e, x;
        Census(client, pristine, english, o, e, x);
        bool dll = GetFileAttributesW((client + L"\\d3d9.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
        bool allowDll = GetFileAttributesW((g_root + L"\\allow-d3d9.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (e || x || (dll && !allowDll)) {
            fprintf(f, "[GAME][2026/10/07 01:00:02] invalid file format[3]\n");
            fclose(f);
            Report(L"rain-launcher", L"CHECK FAILED: " + std::to_wstring(e) + L" English, " + std::to_wstring(x) + L" other, d3d9.dll " + (dll ? L"present" : L"absent"));
            Sleep(3000);
            return 1;
        }
        fprintf(f, "[GAME][2026/10/07 01:00:02] Game file verification successful.\n[GAME][2026/10/07 01:00:02] Game execution process started.\n");
        fflush(f);
        Report(L"rain-launcher", L"check passed (" + std::to_wstring(o) + L" originals), starting client");
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi = {};
        std::wstring c = client + L"\\client.exe";
        std::vector<wchar_t> cmd(c.begin(), c.end());
        cmd.push_back(0);
        if (CreateProcessW(c.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, client.c_str(), &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        fprintf(f, "[GAME][2026/10/07 01:00:09] Game process closed. code=102, signal=null\n");
        fclose(f);
        Report(L"rain-launcher", L"exit");
        return 0;
    }
    if (_wcsicmp(name.c_str(), L"client.exe") == 0) {
        std::wstring rain = Dir(dir);
        g_root = Dir(rain);
        std::wstring pristine = g_root + L"\\pristine-rain", english = g_root + L"\\english-rain";
        bool dll0 = GetFileAttributesW((dir + L"\\d3d9.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
        Sleep(1000);  // client.dll arrives over IPC and loads d3d9.dll; text follows
        bool dll = GetFileAttributesW((dir + L"\\d3d9.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
        int o, e, x;
        Census(dir, pristine, english, o, e, x);
        Report(L"client", L"GET_TXT: " + std::to_wstring(e) + L" English, " + std::to_wstring(o) + L" original, " + std::to_wstring(x) + L" other; d3d9.dll at start " + (dll0 ? L"present" : L"absent") + L", at 1 s " + (dll ? L"present" : L"absent"));
        Sleep(3000);
        Report(L"client", L"exit");
        return 0;
    }
    return 2;
}
