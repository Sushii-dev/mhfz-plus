// Triggers and stick clicks of an Xbox-type controller for the game (pad_triggers=1): the math, tested on its own
// (test\padtest.cpp); fps.cpp's DirectInput hook applies it.
//
// MHF reads controllers through DirectInput. An Xbox-type controller - every controller Steam Input hands to games,
// a DualSense too - has its two triggers on one shared axis there and only ten buttons, so the game's key settings
// can't put L2/R2 on the triggers, and the buttons it then uses for L2/R2 are the stick clicks. With this on, the
// buttons the game's own keyconfig.cfg names for L2, R2, L3 and R3 are set from XInput as the game reads the
// controller: the left and right trigger (pressed past 30 of 255) and the left and right stick click. Based on the
// trigger fix in TYCHKA's Frontier Helper (L2/R2), used with permission under MIT.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct PadMap { int l2 = -1, r2 = -1, l3 = -1, r3 = -1; };  // button indexes, -1: not assigned

// One ASSIGN_xx="0x..." attribute of the <controller ...> tag: a single button bit (0..15) -> its index.
static inline int PadAssign(const char* tag, const char* name) {
    char key[40];
    snprintf(key, sizeof key, " %s=\"", name);
    const char* p = strstr(tag, key);
    if (!p) return -1;
    char* end;
    unsigned long mask = strtoul(p + strlen(key), &end, 16);
    if (*end != '"' || !mask || mask > 0xFFFF || (mask & (mask - 1))) return -1;  // (higher bits: the D-pad and sticks)
    int i = 0;
    while ((mask >> i) != 1) i++;
    return i;
}
// keyconfig.cfg's first <controller ...> tag (the file is the game's; only its ASCII attributes are read).
static inline PadMap PadParse(const char* cfg) {
    PadMap m;
    const char* a = strstr(cfg, "<controller ");
    const char* b = a ? strchr(a, '>') : nullptr;
    if (!b || b - a > 4000) return m;
    char tag[4096];
    memcpy(tag, a, b - a);
    tag[b - a] = 0;
    m.l2 = PadAssign(tag, "ASSIGN_L2");
    m.r2 = PadAssign(tag, "ASSIGN_R2");
    m.l3 = PadAssign(tag, "ASSIGN_L3");
    m.r3 = PadAssign(tag, "ASSIGN_R3");
    return m;
}
// Sets the game's L2/R2/L3/R3 buttons (DirectInput's 0x80 = down) in a joystick state's buttons; count: 32 for
// DIJOYSTATE, 128 for DIJOYSTATE2. A button index two of them share is left alone. Returns how many were set.
static inline int PadFill(uint8_t* buttons, int count, const PadMap& m, bool lt, bool rt, bool ls, bool rs) {
    const int idx[4] = {m.l2, m.r2, m.l3, m.r3};
    const bool down[4] = {lt, rt, ls, rs};
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (idx[i] < 0 || idx[i] >= count) continue;
        bool shared = false;
        for (int j = 0; j < 4; j++) shared |= j != i && idx[j] == idx[i];
        if (shared) continue;
        buttons[idx[i]] = down[i] ? 0x80 : 0;
        n++;
    }
    return n;
}
