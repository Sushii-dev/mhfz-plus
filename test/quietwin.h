// Test windows that stay out of the way of whoever is using the PC: a borderless tool window that never takes the
// keyboard focus and that window managers leave alone (komorebi and the like tile and focus normal windows), on a
// second monitor if there is one (else the bottom-right corner of the main one), with the process at below-normal
// priority. Its drawing area is what the old w x h framed window had, so the back buffer and the results stay the same.
// MHFZ_TEST_LOUD=1 gives the old behaviour (a normal framed window at 100,100).
#pragma once
#include <windows.h>

static BOOL CALLBACK QwOtherMonitor(HMONITOR m, HDC, LPRECT, LPARAM out) {
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoA(m, &mi) && !(mi.dwFlags & MONITORINFOF_PRIMARY)) { *(RECT*)out = mi.rcWork; return FALSE; }
    return TRUE;
}

static HWND QuietWindow(const char* cls, const char* title, int w, int h, HINSTANCE inst) {
    char v[8] = "";
    if (GetEnvironmentVariableA("MHFZ_TEST_LOUD", v, sizeof v) && v[0] == '1')
        return CreateWindowA(cls, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, w, h, 0, 0, inst, 0);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    RECT frame = {0, 0, 0, 0};
    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);  // (the old window's frame: what its client area lacked)
    int cw = w - (frame.right - frame.left), ch = h - (frame.bottom - frame.top);
    RECT r = {0, 0, 0, 0};
    EnumDisplayMonitors(nullptr, nullptr, QwOtherMonitor, (LPARAM)&r);
    if (r.right - r.left < cw || r.bottom - r.top < ch) SystemParametersInfoA(SPI_GETWORKAREA, 0, &r, 0);
    HWND hw = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, title, WS_POPUP, r.right - cw, r.bottom - ch, cw, ch,
                              0, 0, inst, 0);
    if (hw) ShowWindow(hw, SW_SHOWNOACTIVATE);
    return hw;
}
