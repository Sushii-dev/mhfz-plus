// pad_triggers test (src\padfix.h), windowless: the game's keyconfig.cfg read for its L2/R2/L3/R3 buttons, and those
// buttons set from an Xbox-type controller's triggers and stick clicks. Exit code: the number of failed checks.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "../src/padfix.h"

static int g_fail;
static void Check(bool ok, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    printf("  %s ", ok ? "ok  " : "FAIL");
    vprintf(fmt, a);
    printf("\n");
    va_end(a);
    if (!ok) g_fail++;
}

int main() {
    printf("padfix.h\n");
    // (the shape of the game's file: a <controller> tag with button bits, D-pad and stick bits above 0xFFFF, then more)
    const char* xbox =
        "<controller guid=\"00000000-0000-0000-0000-000000000000\" name=\"Controller (XBOX 360 For Windows)\" axes=\"0x00010205\" "
        "dead_zone=\"20\" operate_type=\"1\" ASSIGN_DLU=\"0x00100000\" ASSIGN_DRD=\"0x00000001\" ASSIGN_R2=\"0x00000200\" "
        "ASSIGN_L2=\"0x00000100\" ASSIGN_R1=\"0x00000020\" ASSIGN_L1=\"0x00000010\" ASSIGN_L3=\"0x00000400\" ASSIGN_R3=\"0x00000800\" "
        "ASSIGN_LC=\"0x00000040\" ASSIGN_RC=\"0x00000080\" ASSIGN_ALU=\"0x01000000\"/>\n\t<keyboard operate_type=\"0\"/>\n";
    PadMap m = PadParse(xbox);
    Check(m.l2 == 8 && m.r2 == 9 && m.l3 == 10 && m.r3 == 11, "keyconfig read: L2 %d, R2 %d, L3 %d, R3 %d (want 8 9 10 11)", m.l2, m.r2, m.l3, m.r3);
    Check(PadAssign(" ASSIGN_ALU=\"0x01000000\"", "ASSIGN_ALU") == -1, "stick and D-pad bits are not buttons");
    Check(PadAssign(" ASSIGN_L2=\"0x00000300\"", "ASSIGN_L2") == -1, "two bits at once are not one button");
    Check(PadAssign(" ASSIGN_L2=\"0x00000000\"", "ASSIGN_L2") == -1, "no bit is no button");
    Check(PadAssign(" XASSIGN_L2=\"0x00000100\"", "ASSIGN_L2") == -1, "only the attribute itself counts");
    PadMap none = PadParse("<keyboard operate_type=\"0\"/>");
    Check(none.l2 < 0 && none.r2 < 0 && none.l3 < 0 && none.r3 < 0, "no <controller> tag: nothing assigned");

    uint8_t b[32] = {};
    b[8] = 0x80;  // (the left stick click, which the game takes for L2)
    int n = PadFill(b, 32, m, false, true, true, false);
    Check(n == 4 && b[8] == 0 && b[9] == 0x80 && b[10] == 0x80 && b[11] == 0, "RT held, left stick clicked: R2 and L3 down, L2 up (%d set)", n);
    Check(b[0] == 0 && b[6] == 0 && b[7] == 0, "other buttons untouched");
    uint8_t c[32] = {};
    c[6] = 0x80;
    PadFill(c, 32, m, true, false, false, false);
    Check(c[8] == 0x80 && c[6] == 0x80, "LT: L2 down, Back still down");
    PadMap past = m;
    past.r3 = 40;
    uint8_t d[32] = {};
    Check(PadFill(d, 32, past, false, false, false, true) == 3, "a button past the state's 32 is left out");
    uint8_t e[128] = {};
    Check(PadFill(e, 128, past, false, false, false, true) == 4 && e[40] == 0x80, "DIJOYSTATE2's 128 buttons take it");
    PadMap same = m;
    same.r2 = same.l2;
    uint8_t f[32] = {};
    f[8] = 0x80;
    Check(PadFill(f, 32, same, false, false, false, false) == 2 && f[8] == 0x80, "two of them on one button: that button left alone");
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail;
}
