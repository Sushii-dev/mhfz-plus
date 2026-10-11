// MHFZ Plus: the one window players open. Play (start MezeLounge or Rain through MHFZ Plus and follow the session as it
// goes), Settings (the frame-rate mod's mhfz-fps.ini, and MHFZ Plus's own options) and Help (bug report, the original
// files back, updates, uninstall).
//
// The look follows Monster Hunter Frontier's own screens: the World Select screen's parchment dialog with dark tabs,
// navy headers, green values and a red tag on the selected row, its title on gold diamond tiles, over an illustrated
// map (ui\backdrop.jpg); the in-game HUD's charcoal panels with silver frames, yellow headers and a green gem for live
// status and messages. Everything is drawn here with GDI+ (no stock controls but the one text box for typing a
// number), so it looks the same on every Windows version and under Wine. Sizes are logical pixels at 96 dpi, scaled.
//
// It moves like the game's menus (Anim.cs): the title tiles stamp in and the parchment unrolls when the window opens,
// the dialog reshapes between pages, HUD messages unfold, the cursor nudges and selections slide, hovers fade, and a
// glint runs over START. The idle movements (cursor, glint, glowing gems) run only while the window is in front, and
// only the small areas that move are redrawn: nothing is taken from a game running behind it.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace MhfzPlus
{
    // ------------------------------------------------------------------------------------------------ colours and shapes
    static class Ui
    {
        public static readonly Color
            Parchment = Color.FromArgb(232, 225, 199), ParchLight = Color.FromArgb(243, 238, 219), ParchBorder = Color.FromArgb(93, 79, 53),
            ParchInner = Color.FromArgb(160, 140, 98), Navy = Color.FromArgb(36, 48, 122), Green = Color.FromArgb(40, 128, 54),
            Ink = Color.FromArgb(58, 53, 38), Muted = Color.FromArgb(100, 92, 70), Warn = Color.FromArgb(163, 51, 42),
            TabBg = Color.FromArgb(90, 82, 64), TabFg = Color.FromArgb(239, 232, 208), TabOn = Color.FromArgb(217, 208, 174), TabOnFg = Color.FromArgb(43, 42, 32),
            Teal = Color.FromArgb(47, 167, 160), TealDark = Color.FromArgb(20, 92, 88), Red = Color.FromArgb(192, 48, 58), RedDark = Color.FromArgb(94, 20, 22),
            Gold = Color.FromArgb(196, 165, 53), GoldLight = Color.FromArgb(220, 194, 90), GoldDark = Color.FromArgb(111, 93, 21), GoldMid = Color.FromArgb(159, 135, 40),
            Letter = Color.FromArgb(168, 63, 53), LetterDark = Color.FromArgb(94, 30, 24),
            Hud = Color.FromArgb(232, 28, 30, 26), HudLine = Color.FromArgb(195, 200, 189), HudInner = Color.FromArgb(85, 89, 79), HudKey = Color.FromArgb(74, 77, 67),
            HudYellow = Color.FromArgb(229, 214, 74), HudText = Color.FromArgb(240, 240, 232), HudMuted = Color.FromArgb(205, 208, 196), HudRed = Color.FromArgb(255, 140, 110),
            Gem = Color.FromArgb(63, 184, 90), GemDark = Color.FromArgb(29, 95, 43), GemLight = Color.FromArgb(159, 240, 176),
            GemRed = Color.FromArgb(214, 72, 60), GemRedDark = Color.FromArgb(110, 24, 18), GemAmber = Color.FromArgb(230, 170, 50), GemAmberDark = Color.FromArgb(120, 80, 10),
            Corner = Color.FromArgb(184, 154, 74), CornerDark = Color.FromArgb(74, 61, 34), CornerLight = Color.FromArgb(226, 203, 124),
            RowOn = Color.FromArgb(40, 47, 167, 160), RowHover = Color.FromArgb(13, 47, 167, 160),
            LegendGreen = Color.FromArgb(47, 154, 69), LegendOrange = Color.FromArgb(217, 121, 43), LegendBlue = Color.FromArgb(53, 80, 200);

        // the title letters: angular, cut-paper shapes on a 60x70 box (drawn for MHFZ Plus, after the style of the game's titles)
        static readonly Dictionary<char, string> glyphs = new Dictionary<char, string> {
            {'M', "M6 66 L12 4 L30 30 L48 4 L54 66 L43 66 L40 26 L30 44 L20 26 L17 66 Z"},
            {'H', "M6 4 L18 6 L18 28 L42 26 L42 4 L54 6 L54 66 L42 64 L42 40 L18 42 L18 66 L6 64 Z"},
            {'F', "M6 6 L56 2 L52 16 L19 16 L19 29 L46 27 L43 40 L19 40 L17 66 L5 66 Z"},
            {'Z', "M5 4 L55 6 L56 15 L22 54 L56 52 L53 66 L4 66 L4 57 L37 17 L8 17 Z"},
            {'P', "M6 4 L46 4 L56 14 L54 34 L44 43 L19 43 L18 66 L6 66 Z M19 15 L19 32 L41 31 L43 17 Z"},
            {'L', "M6 4 L19 6 L19 53 L56 50 L53 66 L5 66 Z"},
            {'U', "M5 4 L18 4 L18 52 L42 52 L42 4 L55 6 L55 58 L46 66 L14 66 L5 58 Z"},
            {'S', "M55 4 L52 16 L20 16 L20 27 L47 27 L56 36 L56 58 L47 66 L5 66 L8 53 L42 53 L42 40 L13 40 L4 31 L4 12 L13 4 Z"},
            {'T', "M3 4 L57 6 L54 18 L36 18 L35 66 L23 66 L22 18 L5 17 Z"},
            {'A', "M4 66 L24 4 L36 4 L56 66 L44 66 L39 50 L21 50 L16 66 Z M24 39 L36 39 L30 18 Z"},
            {'R', "M6 4 L46 4 L56 14 L55 31 L46 39 L57 66 L44 66 L35 42 L19 42 L18 66 L6 66 Z M19 15 L19 31 L41 30 L43 17 Z"},
        };
        public static GraphicsPath Glyph(char c, float cx, float cy, float scale)
        {
            var p = new GraphicsPath(FillMode.Alternate);
            string d;
            if (!glyphs.TryGetValue(c, out d)) return p;
            var pts = new List<PointF>();
            string[] tok = d.Split(' ');
            for (int i = 0; i < tok.Length; i++) {
                if (tok[i] == "Z") { if (pts.Count > 2) { p.AddPolygon(pts.ToArray()); } pts.Clear(); continue; }
                float x = float.Parse(tok[i].Substring(1), System.Globalization.CultureInfo.InvariantCulture);
                float y = float.Parse(tok[++i], System.Globalization.CultureInfo.InvariantCulture);
                pts.Add(new PointF(cx + (x - 30) * scale, cy + (y - 35) * scale));
            }
            return p;
        }
        public static PointF[] Diamond(float cx, float cy, float r)
        {
            return new[] { new PointF(cx, cy - r), new PointF(cx + r, cy), new PointF(cx, cy + r), new PointF(cx - r, cy) };
        }
        public static Color Lighter(Color c, float f)
        {
            return Color.FromArgb(c.A, Math.Min(255, (int)(c.R + (255 - c.R) * f)), Math.Min(255, (int)(c.G + (255 - c.G) * f)), Math.Min(255, (int)(c.B + (255 - c.B) * f)));
        }
    }

    // ------------------------------------------------------------------------------------------------ the window
    partial class HubForm : Form
    {
        const int W = 1120, H = 700;
        const string Repo = "https://github.com/Sushii-dev/mhfz-plus";
        const string IssueForm = Repo + "/issues/new?template=bug_report.yml";
        const string Kofi = "https://ko-fi.com/sushiisquared";

        enum Page { Play, Settings, Help }
        class HotSpot { public RectangleF R; public string Id; public Action Click; }
        class Srv { public string Id, Title, Dir, English, Fps, Backend, Last, Desc, Note; public bool Ready; }
        class Toast { public string Title, Text; public bool Error; public List<KeyValuePair<string, Action>> Buttons = new List<KeyValuePair<string, Action>>(); public DateTime Until = DateTime.MaxValue; public bool Busy; }
        class Session
        {
            public string Server, Log, Head = "STARTING", Title, Text;
            public Process Proc;
            public long Pos;
            public bool Problem, Done, External, Crashed, ModMissing;
            public DateTime DoneAt;
            public bool ViaSteam; public DateTime Asked, NextLook;   // (asked of Steam: no process of ours yet)
        }
        class AppRow { public string Label, Help; public string[][] Options; public Func<string> Get; public Action<string> Set; }

        // ---------------------------------------------------------------- state
        float k = 1f;                         // dpi scale
        readonly string baseDir, version;
        readonly bool wine;
        Image backdrop;
        Bitmap bg, layer, parch; Page layerFor = (Page)(-1); float layerK, bgK, parchK;   // backdrop; backdrop + title at rest; parchment
        TextureBrush paper;
        FontFamily mono;
        Font fBody, fBold, fSmall, fSmallBold, fHead, fTiny, fBig;
        Page page = Page.Play;
        readonly List<HotSpot> hots = new List<HotSpot>();
        string hover, press;
        readonly System.Windows.Forms.Timer tick = new System.Windows.Forms.Timer { Interval = 120 };
        int frame;
        // animations (Anim.cs): a 60 Hz timer that runs only while something moves
        readonly System.Windows.Forms.Timer anim = new System.Windows.Forms.Timer { Interval = 15 };
        int animFrame;
        bool front, animSetting = true, winAnims = true;
        readonly List<Tween> layout = new List<Tween>();   // the ones that move big parts: the whole window is redrawn
        readonly Fades fades = new Fades();
        readonly List<RectangleF> idle = new List<RectangleF>();   // where idle movement was drawn (redrawn alone)
        RectangleF glintAt;
        Tween homeAmt, panelH, panelOpen, unroll, selPos, toastOpen, sessOpen, sessFlash, capOpen;
        Tween[] tilePop, startPop;
        object toastSeen, sessSeen, capSeen; string headSeen;
        bool introDone;
        // play
        List<Srv> servers = new List<Srv>();
        int sel;
        Session session;
        // settings
        List<Game> games = new List<Game>();
        int gameIdx;
        IniFile ini;
        readonly Dictionary<string, string> vals = new Dictionary<string, string>();
        bool advanced;
        string cat = "General";
        float scroll, scrollMax;
        DateTime saveAt = DateTime.MaxValue, savedAt = DateTime.MinValue;
        string saveError;
        Setting capture;
        TextBox edit; Setting editing;
        // help, toast, updates
        Toast toast;
        string latest, latestUrl;
        bool checking;

        // ---------------------------------------------------------------- native
        [DllImport("user32")] static extern bool ReleaseCapture();
        [DllImport("user32")] static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
        [DllImport("dwmapi")] static extern int DwmSetWindowAttribute(IntPtr h, int attr, ref int v, int size);
        [DllImport("user32")] static extern bool SystemParametersInfo(int action, int param, ref int v, int ini);
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr m, string name);
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern int GetPrivateProfileString(string sec, string key, string def, StringBuilder buf, int size, string file);
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern bool WritePrivateProfileString(string sec, string key, string val, string file);
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct DEVMODE
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
            public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
            public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
            public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
            public short dmLogPixels;
            public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        }
        [DllImport("user32", CharSet = CharSet.Unicode)] static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);

        string AppIni { get { return Path.Combine(baseDir, "MHFZ-Plus.ini"); } }
        string IniGet(string sec, string key, string def)
        {
            var sb = new StringBuilder(2048);
            GetPrivateProfileString(sec, key, def, sb, sb.Capacity, AppIni);
            return sb.ToString();
        }
        void IniPut(string sec, string key, string v) { WritePrivateProfileString(sec, key, v, AppIni); }

        // ---------------------------------------------------------------- setup
        public HubForm(float scale, bool offline = false)
        {
            baseDir = Game.FindBase();
            try { version = File.ReadAllLines(Path.Combine(baseDir, "version.txt"))[0].Trim(); } catch { version = "1.x"; }
            try { wine = GetProcAddress(GetModuleHandle("ntdll.dll"), "wine_get_version") != IntPtr.Zero; } catch { }
            Text = "MHFZ Plus";
            // the icon in every size (taskbar and Alt+Tab at any dpi): the .ico built into the exe as a resource (the exe's
            // own icon comes out of ExtractAssociatedIcon at 32 px only, blurry when bigger)
            try {
                using (var s = typeof(HubForm).Assembly.GetManifestResourceStream("mhfz-plus.ico"))
                    Icon = s != null ? new Icon(s) : Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            } catch { try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { } }
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.None;
            KeyPreview = true;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            k = scale;
            if (k <= 0) {
                using (var g = CreateGraphics()) k = Math.Max(1f, g.DpiX / 96f);
                var wa = Screen.PrimaryScreen.WorkingArea;  // (never bigger than the screen: a small laptop at 150%)
                k = Math.Min(k, Math.Min((wa.Width - 20) / (float)W, (wa.Height - 20) / (float)H));
            }
            ClientSize = new Size((int)Math.Round(W * k), (int)Math.Round(H * k));
            LoadFonts();
            try {
                string bd = Path.Combine(baseDir, "ui", "backdrop.jpg");
                if (File.Exists(bd)) using (var f = Image.FromFile(bd)) backdrop = new Bitmap(f);
            } catch { }
            paper = MakePaper();
            LoadServers();
            LoadGames();
            sel = Math.Max(0, servers.FindIndex(s => s.Id == IniGet("general", "server", "") && s.Ready));   // (the last one chosen)
            GameOfChoice();
            SetupAnimations();
            tick.Tick += delegate { OnTick(); };
            tick.Start();
            AttachRunningSession();
            if (!offline && IniGet("general", "update_check", "1") != "0") CheckUpdate(false);
            if (!offline) SayUpdated();
        }

        void LoadFonts()
        {
            // the game's text is a squared monospace: M PLUS 1 Code when it comes with MHFZ Plus (ui\fonts), else Consolas
            mono = null;
            try {
                string dir = Path.Combine(baseDir, "ui", "fonts");
                if (Directory.Exists(dir)) {
                    var pfc = new PrivateFontCollection();
                    foreach (string f in Directory.GetFiles(dir, "*.ttf")) pfc.AddFontFile(f);
                    if (pfc.Families.Length > 0) mono = pfc.Families[0];
                }
            } catch { }
            if (mono == null) {
                foreach (string name in new[] { "M PLUS 1 Code", "Consolas", "DejaVu Sans Mono", "Liberation Mono" }) {
                    try { var f = new FontFamily(name); if (f.IsStyleAvailable(FontStyle.Regular)) { mono = f; break; } } catch { }
                }
            }
            if (mono == null) mono = FontFamily.GenericMonospace;
            fBody = new Font(mono, 14f, FontStyle.Regular, GraphicsUnit.Pixel);
            fBold = new Font(mono, 14f, mono.IsStyleAvailable(FontStyle.Bold) ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel);
            fHead = new Font(mono, 13.5f, fBold.Style, GraphicsUnit.Pixel);
            fSmall = new Font(mono, 12.5f, FontStyle.Regular, GraphicsUnit.Pixel);
            fSmallBold = new Font(mono, 12.5f, fBold.Style, GraphicsUnit.Pixel);
            fTiny = new Font(mono, 11f, FontStyle.Regular, GraphicsUnit.Pixel);
            fBig = new Font(mono, 15.5f, fBold.Style, GraphicsUnit.Pixel);
        }

        // a faint paper grain for the parchment
        static TextureBrush MakePaper()
        {
            var b = new Bitmap(96, 96, PixelFormat.Format32bppArgb);
            var r = new Random(7);
            for (int y = 0; y < 96; y++)
                for (int x = 0; x < 96; x++) {
                    int n = r.Next(100);
                    b.SetPixel(x, y, n < 7 ? Color.FromArgb(18, 90, 70, 30) : n < 10 ? Color.FromArgb(22, 255, 255, 240) : Color.Transparent);
                }
            return new TextureBrush(b) { WrapMode = WrapMode.Tile };
        }

        protected override CreateParams CreateParams
        {
            get {
                var cp = base.CreateParams;
                cp.Style |= 0x00020000 | 0x00080000;   // WS_MINIMIZEBOX | WS_SYSMENU: minimize from the taskbar
                cp.ClassStyle |= 0x00020000;           // CS_DROPSHADOW
                return cp;
            }
        }
        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            try { int round = 2; DwmSetWindowAttribute(Handle, 33, ref round, 4); } catch { }  // (Windows 11: rounded corners)
        }

        // ---------------------------------------------------------------- animations
        // on by default; off under Wine (Linux, Steam Deck), whose GDI+ draws this window 10-25x slower (64-171 ms a
        // frame, and a small area costs nearly as much): the idle movement alone would keep a CPU core busy
        string AnimDefault { get { return wine ? "0" : "1"; } }
        void SetupAnimations()
        {
            int on = 1;
            try { if (!SystemParametersInfo(0x1042, 0, ref on, 0)) on = 1; } catch { on = 1; }   // SPI_GETCLIENTAREAANIMATION: Windows' "Animation effects"
            winAnims = on != 0;
            animSetting = IniGet("general", "animations", AnimDefault) != "0";
            Tween.Off = !(winAnims && animSetting);
            Func<float, Tween> L = v => { var t = new Tween(v); layout.Add(t); return t; };
            homeAmt = L(1); panelH = L(486); panelOpen = L(1); unroll = L(1);
            selPos = L(sel);
            toastOpen = L(1); sessOpen = L(1); sessFlash = L(0); capOpen = L(1);
            tilePop = new Tween[8];
            for (int i = 0; i < tilePop.Length; i++) tilePop[i] = L(1);
            startPop = new Tween[5];
            for (int i = 0; i < startPop.Length; i++) startPop[i] = L(1);
            anim.Tick += delegate { OnAnim(); };
        }
        // the window opening: the title tiles stamp in one by one, the parchment unrolls, START follows
        void StartIntro()
        {
            for (int i = 0; i < tilePop.Length; i++) tilePop[i].Run(0, 1, 380, Ease.OutBack, 60 + 55 * i);
            panelOpen.Run(0, 1, 340, Ease.OutCubic, 360);
            unroll.Run(0, 1, 420, Ease.OutCubic, 420);
            PopStart(620);
        }
        void PopStart(double delay)
        {
            for (int i = 0; i < startPop.Length; i++) startPop[i].Run(0, 1, 340, Ease.OutBack, delay + 50 * i);
            Kick();
        }
        protected override void OnActivated(EventArgs e) { base.OnActivated(e); front = true; Kick(); }
        protected override void OnDeactivate(EventArgs e) { base.OnDeactivate(e); front = false; }
        void Kick() { if (!Tween.Off && !anim.Enabled) anim.Start(); }
        bool TitleMoving { get { return homeAmt.Moving || tilePop.Any(t => t.Moving); } }
        // idle movement only while the window is in front (a game in front of it gets every bit of the CPU)
        bool IdleOn { get { return !Tween.Off && front && WindowState != FormWindowState.Minimized && capture == null; } }
        // what moved at the last tick: drawn once more after it stops, so the screen shows where it ended, not the last
        // step before (issue #1: fast mouse moves left faded-out highlights and cursors behind)
        List<string> fadingWas = new List<string>();
        bool layoutWas;
        void OnAnim()
        {
            animFrame++;
            bool lay = layout.Any(t => t.Moving);
            var fading = fades.Moving();
            var redraw = fading.Union(fadingWas).ToList();
            fadingWas = fading;
            if (lay || layoutWas) { layoutWas = lay; Invalidate(); return; }
            bool busy = redraw.Count > 0;
            foreach (string id in redraw)
                foreach (var h in hots) if (h.Id == id) InvalidateLogical(h.R, 4);
            if (IdleOn) {
                busy = true;
                if (animFrame % 2 == 0) {   // (30 Hz is plenty for these)
                    foreach (var r in idle) InvalidateLogical(r, 3);
                    float ph = (float)(Tween.Now % 3800 / 1000.0 * 7.5) - 1.5f;   // (START's glint, while it runs: see DrawStart)
                    if (!glintAt.IsEmpty && ph < 7) InvalidateLogical(glintAt, 3);
                }
            }
            if (!busy) anim.Stop();
        }
        void InvalidateLogical(RectangleF r, float pad)
        {
            Invalidate(Rectangle.FromLTRB((int)Math.Floor((r.Left - pad) * k), (int)Math.Floor((r.Top - pad) * k),
                                          (int)Math.Ceiling((r.Right + pad) * k), (int)Math.Ceiling((r.Bottom + pad) * k)));
        }
        float Hv(string id) { return fades.Get(id); }
        // the cursor's nudge and the gems' glow
        float Bob { get { return Tween.Off ? 0 : 2.5f * Ease.Wave(Tween.Now, 900); } }
        float Glow { get { return Tween.Off ? 0.6f : Ease.Wave(Tween.Now, 1200); } }
        // a HUD panel opening out from its middle line
        static RectangleF Unfold(RectangleF r, float o)
        {
            float h = r.Height * Math.Max(0.04f, o);
            return new RectangleF(r.X, r.Y + (r.Height - h) / 2f, r.Width, h);
        }
        // (a page set without GoTo: no glide)
        void SyncLayout()
        {
            float hw = page == Page.Play ? 1 : 0, ph = page == Page.Help ? 430 : 486;
            if (homeAmt.Target != hw) homeAmt.Snap(hw);
            if (page != Page.Play && panelH.Target != ph) panelH.Snap(ph);
        }
        // a server chosen on the Play page: remembered (chosen again when MHFZ Plus opens next time), and its game is the
        // one Settings shows
        void ChooseServer(int i, bool remember = true)
        {
            sel = i;
            if (remember && i < servers.Count && IniGet("general", "server", "") != servers[i].Id) IniPut("general", "server", servers[i].Id);
            GameOfChoice();
            selPos.To(i, 170);
            Kick();
            Invalidate();
        }
        void GameOfChoice()
        {
            if (servers.Count == 0) return;
            int gi = games.FindIndex(x => x.Name == servers[Math.Min(sel, servers.Count - 1)].Id);
            if (gi < 0 || gi == gameIdx) return;
            if (saveAt != DateTime.MaxValue) SaveNow();   // (unsaved changes belong to the other game)
            gameIdx = gi;
            LoadValues();
        }
        void OpenCat(string c)
        {
            cat = c; scroll = 0;
            Invalidate();
        }

        // The ▶ cursor of the lists (Play's servers, Settings' categories, Help's actions), one rule for all, as in the
        // game's menus: it is on the row under the mouse, and slides back to the current choice (the chosen server, the
        // open category: their rows are tinted) when the mouse leaves the list; a list without a choice lets it go.
        // Places are from the list's top (y0), so a list that moves (a page change) takes its cursor along.
        class CursorState { public Tween Y = new Tween(0), Show = new Tween(0); public float At = float.NaN; }
        readonly Dictionary<string, CursorState> cursors = new Dictionary<string, CursorState>();
        void ListCursor(Graphics g, string list, float x, float y0, float? hoverAt, float? choiceAt)
        {
            CursorState c;
            if (!cursors.TryGetValue(list, out c)) { cursors[list] = c = new CursorState(); layout.Add(c.Y); layout.Add(c.Show); }
            float? want = hoverAt ?? choiceAt;
            if (want.HasValue && want.Value != c.At) {
                if (float.IsNaN(c.At) || c.Show.Value < 0.05f) c.Y.Snap(want.Value);   // (appearing: right there)
                else c.Y.To(want.Value, 150);
                c.At = want.Value;
                Kick();
            }
            float show = want.HasValue ? 1 : 0;
            if (c.Show.Target != show) { c.Show.To(show, want.HasValue ? 90 : 160); Kick(); }
            float s = c.Show.Value;
            if (s <= 0.01f) return;
            float ay = y0 + c.Y.Value;
            Arrow(g, x - 8 * (1 - s) + Bob, ay, 11, 12);   // (comes in from the left, nudging like the game's)
            idle.Add(new RectangleF(x - 9, ay - 2, 25, 16));
        }

        // ---------------------------------------------------------------- timer
        void OnTick()
        {
            frame++;
            bool redraw = false;
            if (session != null) {   // (redrawn when it says something new; the gem's glow is idle movement)
                string was = session.Head + session.Title + session.Text + session.Done;
                FollowSession();
                if (session.Done && (DateTime.Now - session.DoneAt).TotalSeconds > (session.Problem ? 30 : 6)) { session = null; redraw = true; }
                else if (session.Head + session.Title + session.Text + session.Done != was) redraw = true;
            } else if (frame % 25 == 0) { AttachRunningSession(); if (session != null) redraw = true; }
            if (DateTime.Now >= saveAt) { SaveNow(); redraw = true; }
            if (toast != null && DateTime.Now > toast.Until && toast.Buttons.Count == 0) { toast = null; redraw = true; }
            if (page == Page.Settings && (DateTime.Now - savedAt).TotalSeconds < 3) redraw = true;
            if (redraw) Invalidate();
        }

        // ---------------------------------------------------------------- drawing
        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            if (!introDone) { introDone = true; StartIntro(); }   // (from the first frame: a slow start can't eat the intro)
            SyncLayout();
            bool live = TitleMoving;
            EnsureLayers(live);
            g.DrawImageUnscaled(live ? bg : layer, 0, 0);
            g.ScaleTransform(k, k);
            if (live) { Quality(g); DrawTitleNow(g); }
            Render(g, false);
        }
        // the backdrop, and the backdrop with the title at rest: drawn once at the screen's scale (the title is drawn
        // anew each frame only while it moves)
        void EnsureLayers(bool live)
        {
            if (bg == null || bgK != k) {
                if (bg != null) bg.Dispose();
                bg = NewLayer();
                using (var g = Graphics.FromImage(bg)) { g.ScaleTransform(k, k); Quality(g); DrawBackdrop(g); }
                bgK = k; layerK = 0;
            }
            var want = page == Page.Play ? Page.Play : Page.Settings;
            if (live || (layer != null && layerFor == want && layerK == k)) return;
            if (layer == null || layer.Width != bg.Width || layer.Height != bg.Height) { if (layer != null) layer.Dispose(); layer = NewLayer(); }
            using (var g = Graphics.FromImage(layer)) {
                g.CompositingMode = CompositingMode.SourceCopy;
                g.DrawImage(bg, new Rectangle(0, 0, bg.Width, bg.Height));
                g.CompositingMode = CompositingMode.SourceOver;
                g.ScaleTransform(k, k); Quality(g); DrawTitleNow(g);
            }
            layerFor = want; layerK = k;
        }
        Bitmap NewLayer() { return new Bitmap(Math.Max(1, (int)Math.Round(W * k)), Math.Max(1, (int)Math.Round(H * k)), PixelFormat.Format32bppPArgb); }
        static void Quality(Graphics g)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
            g.CompositingQuality = CompositingQuality.Default;   // (HighQuality: gamma-correct blending, 5-10x slower; nearly the same look)
        }
        // (the whole picture into a bitmap, for screenshots without a window)
        public Bitmap Shot()
        {
            var b = new Bitmap((int)Math.Round(W * k), (int)Math.Round(H * k), PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(b)) { g.ScaleTransform(k, k); Quality(g); Render(g, true); Render(g, false); }
            return b;
        }

        void Render(Graphics g, bool staticPart)
        {
            Quality(g);
            SyncLayout();
            if (staticPart) { DrawBackdrop(g); DrawTitleNow(g); return; }
            hots.Clear();
            idle.Clear();
            glintAt = RectangleF.Empty;
            DrawTitleBar(g);
            // the layout glides between the Play page's (big title, subtitle, small dialog) and the others'
            float h = homeAmt.Value;
            float reveal = Ease.Clamp((h - 0.55f) / 0.45f);
            if (reveal > 0) {   // (the subtitle and legend open out from the middle)
                var st = g.Save();
                g.SetClip(new RectangleF(W / 2f * (1 - reveal), 150, W * reveal, 72), CombineMode.Intersect);
                HudBox(g, "High frame rate and English for Monster Hunter Frontier Z", W / 2f, 160, true);
                DrawLegend(g, W / 2f, 196);
                g.Restore(st);
            }
            DrawTabs(g, Ease.Lerp(124, 204, h), Ease.Lerp(106, 218, h));
            var panel = Ease.Lerp(new RectangleF(110, 134, 900, panelH.Value), new RectangleF(190, 246, 740, 232), h);
            panel.Height *= panelOpen.Value;
            if (panel.Height > 2) {
                DrawParchment(g, panel);
                var st = g.Save();   // (the page's contents unroll from the top)
                g.SetClip(new RectangleF(panel.X, panel.Y, panel.Width, panel.Height * unroll.Value), CombineMode.Intersect);
                if (page == Page.Play) DrawPlay(g, panel);
                else if (page == Page.Settings) DrawSettings(g, panel);
                else DrawHelp(g, panel);
                g.Restore(st);
            }
            if (page == Page.Play) {
                if (session != null) DrawSession(g);
                else DrawStart(g);
            }
            if (session == null) sessSeen = null;
            DrawFooter(g);
            if (toast != null) DrawToast(g); else toastSeen = null;
            if (capture != null) DrawCapture(g); else capSeen = null;
        }
        void DrawBackdrop(Graphics g)
        {
            if (backdrop != null) g.DrawImage(backdrop, new RectangleF(0, 0, W, H));
            else using (var b = new SolidBrush(Color.FromArgb(227, 199, 160))) g.FillRectangle(b, 0, 0, W, H);
        }
        void DrawTitleNow(Graphics g)
        {
            float h = homeAmt.Value;
            DrawTitle(g, Ease.Lerp(34, 44, h), Ease.Lerp(0.62f, 1f, h));
        }

        void DrawTitleBar(Graphics g)
        {
            using (var b = new SolidBrush(Color.FromArgb(43, 39, 32))) g.FillRectangle(b, 0, 0, W, 30);
            using (var p = new Pen(Ui.ParchBorder)) g.DrawLine(p, 0, 29.5f, W, 29.5f);
            using (var b = new SolidBrush(Ui.Gold)) g.FillPolygon(b, Ui.Diamond(17, 15, 5));
            using (var p = new Pen(Ui.GoldDark)) g.DrawPolygon(p, Ui.Diamond(17, 15, 5));
            using (var b = new SolidBrush(Color.FromArgb(233, 226, 200))) g.DrawString("MHFZ Plus " + version, fSmall, b, 28, 7);
            // minimize, close
            var min = new RectangleF(W - 76, 3, 34, 24); var cls = new RectangleF(W - 40, 3, 34, 24);
            Hot(min, "min", () => { WindowState = FormWindowState.Minimized; });
            Hot(cls, "close", () => Close());
            if (Hv("min") > 0) using (var b = new SolidBrush(Color.FromArgb((int)(40 * Hv("min")), 255, 255, 255))) g.FillRectangle(b, min);
            if (Hv("close") > 0) using (var b = new SolidBrush(Color.FromArgb((int)(190 * Hv("close")), 192, 48, 58))) g.FillRectangle(b, cls);
            using (var p = new Pen(Color.FromArgb(233, 226, 200), 1.6f)) {
                g.DrawLine(p, min.X + 12, 15, min.X + 22, 15);
                g.DrawLine(p, cls.X + 12, 10, cls.X + 22, 20); g.DrawLine(p, cls.X + 22, 10, cls.X + 12, 20);
            }
        }

        void DrawTitle(Graphics g, float top, float scale)
        {
            const string word = "MHFZ PLUS";
            float tile = 62 * scale, gap = 34 * scale, size = 100 * scale;
            float total = 0;
            foreach (char c in word) total += c == ' ' ? gap : tile;
            float x = (W - total) / 2f + 22 * scale;
            int n = 0;
            foreach (char c in word) {
                if (c == ' ') { x += gap; continue; }
                float pop = tilePop[n++].Value;   // (stamped in when the window opens: a little too big, then settling)
                if (pop > 0.01f) DrawTile(g, x + 17 * scale, top + 54 * scale - 16 * scale * (1 - Math.Min(1, pop)), size * pop, c, 0, pop * 1.6f);
                x += tile;
            }
        }
        // lit: 0..1 (hover, the glint); alpha: 0..1 (popping in)
        void DrawTile(Graphics g, float cx, float cy, float size, char c, float lit, float alpha = 1)
        {
            float r = size / 2f, s = size / 100f;
            Func<Color, Color> A = col => Ease.Fade(col, alpha);
            using (var b = new SolidBrush(A(Ui.Lighter(Ui.Gold, 0.2f * lit)))) g.FillPolygon(b, Ui.Diamond(cx, cy, r));
            using (var p = new Pen(A(Ui.GoldDark), 3 * s) { LineJoin = LineJoin.Miter }) g.DrawPolygon(p, Ui.Diamond(cx, cy, r));
            using (var p = new Pen(A(Ui.Lighter(Ui.GoldLight, 0.55f * lit)), 2 * s)) g.DrawPolygon(p, Ui.Diamond(cx, cy, r * 0.82f));
            using (var p = new Pen(A(Ui.GoldMid), 2 * s)) g.DrawPolygon(p, Ui.Diamond(cx, cy, r * 0.66f));
            using (var path = Ui.Glyph(c, cx, cy, s)) {
                using (var b = new SolidBrush(A(Ui.Lighter(Ui.Letter, 0.14f * lit)))) g.FillPath(b, path);
                using (var p = new Pen(A(Ui.LetterDark), 2.4f * s) { LineJoin = LineJoin.Round }) g.DrawPath(p, path);
            }
        }

        // the HUD's little label boxes ("Z Acquainted"), centred on cx or from x
        RectangleF HudBox(Graphics g, string text, float x, float y, bool centred)
        {
            var sz = g.MeasureString(text, fSmall);
            float w = sz.Width + 22, h = 24;
            var r = new RectangleF(centred ? x - w / 2f : x, y, w, h);
            using (var b = new SolidBrush(Color.FromArgb(214, 28, 30, 26))) g.FillRectangle(b, r);
            using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
            Outlined(g, text, fSmall, Ui.HudText, r.X + 11, r.Y + 4);
            return r;
        }
        void KeyBox(Graphics g, string key, string label, float x, float y, string id, Action click)
        {
            var ks = g.MeasureString(key, fTiny); var ls = g.MeasureString(label, fSmall);
            var r = new RectangleF(x, y, ks.Width + ls.Width + 26, 24);
            float hov = click != null ? Hv(id) : 0;
            using (var b = new SolidBrush(Ease.Mix(Color.FromArgb(214, 28, 30, 26), Color.FromArgb(235, 48, 51, 44), hov))) g.FillRectangle(b, r);
            using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
            var kr = new RectangleF(x + 6, y + 4, ks.Width + 6, 16);
            using (var b = new SolidBrush(Ui.HudKey)) g.FillRectangle(b, kr);
            using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, kr.X, kr.Y, kr.Width, kr.Height);
            Outlined(g, key, fTiny, Ui.HudText, kr.X + 3, kr.Y + 1);
            Outlined(g, label, fSmall, Ease.Mix(Ui.HudText, Ui.HudYellow, hov), kr.Right + 6, y + 4);
            if (click != null) Hot(r, id, click);
        }

        void DrawLegend(Graphics g, float cx, float top)
        {
            var items = new[] { new KeyValuePair<string, Color>("English", Ui.LegendGreen), new KeyValuePair<string, Color>("Frame-rate mod", Ui.LegendOrange), new KeyValuePair<string, Color>("DXVK", Ui.LegendBlue) };
            float total = items.Sum(it => g.MeasureString(it.Key, fSmallBold).Width + 16) + 18 * 2;
            float x = cx - total / 2f;
            foreach (var i in items) {
                using (var b = new SolidBrush(i.Value)) g.FillRectangle(b, x, top + 4, 11, 11);
                using (var p = new Pen(Color.FromArgb(Math.Max(0, i.Value.R - 80), Math.Max(0, i.Value.G - 80), Math.Max(0, i.Value.B - 80)))) g.DrawRectangle(p, x, top + 4, 11, 11);
                Outlined(g, i.Key, fSmallBold, Ui.Lighter(i.Value, 0.45f), x + 16, top + 3);
                x += g.MeasureString(i.Key, fSmallBold).Width + 16 + 18;
            }
        }

        void DrawTabs(Graphics g, float x, float y)
        {
            foreach (var t in new[] { new KeyValuePair<Page, string>(Page.Play, "Play"), new KeyValuePair<Page, string>(Page.Settings, "Settings"), new KeyValuePair<Page, string>(Page.Help, "Help") }) {
                bool on = page == t.Key;
                string id = "tab" + t.Value;
                float w = g.MeasureString(t.Value, fHead).Width + (on ? 44 : 28);
                var r = new RectangleF(x, y, w, 28);
                Color bgc = on ? Ui.TabOn : Ease.Mix(Ui.TabBg, Ui.Lighter(Ui.TabBg, 0.15f), Hv(id));
                using (var b = new SolidBrush(bgc)) g.FillRectangle(b, r);
                using (var p = new Pen(Ui.ParchBorder)) { g.DrawLine(p, r.Left, r.Bottom, r.Left, r.Top); g.DrawLine(p, r.Left, r.Top, r.Right, r.Top); g.DrawLine(p, r.Right, r.Top, r.Right, r.Bottom); }
                float tx = r.X + 14;
                if (on) { Arrow(g, tx + Bob, r.Y + 8, 11, 12); idle.Add(new RectangleF(tx - 1, r.Y + 6, 17, 16)); tx += 18; }
                using (var b = new SolidBrush(on ? Ui.TabOnFg : Ui.TabFg)) g.DrawString(t.Value, fHead, b, tx, r.Y + 5);
                var pg = t.Key;
                Hot(r, id, () => GoTo(pg));
                x += w + 3;
            }
        }
        void Arrow(Graphics g, float x, float y, float w, float h)
        {
            var pts = new[] { new PointF(x, y), new PointF(x + w, y + h / 2f), new PointF(x, y + h) };
            using (var b = new SolidBrush(Ui.Teal)) g.FillPolygon(b, pts);
            using (var p = new Pen(Ui.TealDark)) g.DrawPolygon(p, pts);
        }
        void SideArrow(Graphics g, RectangleF r, bool left, float hov)
        {
            float cx = r.X + r.Width / 2f + (left ? -1.5f : 1.5f) * hov, cy = r.Y + r.Height / 2f;   // (hovered: a step outwards)
            var pts = left ? new[] { new PointF(cx + 5, cy - 6), new PointF(cx - 5, cy), new PointF(cx + 5, cy + 6) }
                           : new[] { new PointF(cx - 5, cy - 6), new PointF(cx + 5, cy), new PointF(cx - 5, cy + 6) };
            using (var b = new SolidBrush(Ui.Lighter(Ui.Red, 0.25f * hov))) g.FillPolygon(b, pts);
            using (var p = new Pen(Ui.RedDark)) g.DrawPolygon(p, pts);
        }

        void DrawParchment(Graphics g, RectangleF r)
        {
            using (var sh = new SolidBrush(Color.FromArgb(60, 40, 30, 15))) g.FillRectangle(sh, r.X + 4, r.Y + 7, r.Width, r.Height);
            // the parchment and its grain come from a picture made once at the screen's scale, copied pixel for pixel
            // (filling with the grain's texture brush took most of a frame: page changes ran at 10-20 fps)
            if (parch == null || parchK != k) {
                if (parch != null) parch.Dispose();
                parch = NewLayer();
                using (var pg = Graphics.FromImage(parch)) {
                    pg.Clear(Ui.Parchment);
                    pg.ScaleTransform(k, k);
                    pg.FillRectangle(paper, 0, 0, W, H);
                }
                parchK = k;
            }
            var st = g.Save();
            g.ResetTransform();   // (the clip stays where it is)
            g.CompositingMode = CompositingMode.SourceCopy;
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            var dr = Rectangle.Round(new RectangleF(r.X * k, r.Y * k, r.Width * k, r.Height * k));
            dr.Intersect(new Rectangle(0, 0, parch.Width, parch.Height));
            if (dr.Width > 0 && dr.Height > 0) g.DrawImage(parch, dr, dr.X, dr.Y, dr.Width, dr.Height, GraphicsUnit.Pixel);
            g.Restore(st);
            using (var p = new Pen(Ui.ParchBorder, 3)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
            using (var p = new Pen(Ui.ParchInner, 2)) g.DrawRectangle(p, r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6);
            foreach (var c in new[] { new PointF(r.Left, r.Top), new PointF(r.Right, r.Top), new PointF(r.Left, r.Bottom), new PointF(r.Right, r.Bottom) }) {
                var cr = new RectangleF(c.X - 8, c.Y - 8, 16, 16);
                using (var b = new SolidBrush(Ui.Corner)) g.FillRectangle(b, cr);
                using (var p = new Pen(Ui.CornerDark, 2)) g.DrawRectangle(p, cr.X, cr.Y, cr.Width, cr.Height);
                using (var p = new Pen(Ui.CornerLight, 2)) g.DrawRectangle(p, cr.X + 3, cr.Y + 3, cr.Width - 6, cr.Height - 6);
            }
        }
        void Dots(Graphics g, float x1, float x2, float y)
        {
            using (var b = new SolidBrush(Ui.Ink)) for (float x = x1; x < x2; x += 7) g.FillRectangle(b, x, y, 3, 3);
        }

        // ---------------------------------------------------------------- footer, toast, HUD pieces
        void DrawFooter(Graphics g)
        {
            float x = 18;
            if (page == Page.Play) { KeyBox(g, "Enter", "Start", x, 664, "kstart", servers.Count > 0 ? (Action)Start : null); x += 104; }
            KeyBox(g, "Tab", "Next page", x, 664, "ktab", () => GoTo((Page)(((int)page + 1) % 3)));
            string upd = updating ? "Updating..." : checking ? "Checking for updates..." : Newer(latest) ? "MHFZ Plus " + latest + " is out: update" : "Up to date · " + version;
            var s = g.MeasureString(upd, fSmall);
            var r = new RectangleF(W - 18 - s.Width - 30, 664, s.Width + 30, 24);
            float hov = Hv("upd");
            using (var b = new SolidBrush(Ease.Mix(Color.FromArgb(214, 28, 30, 26), Color.FromArgb(235, 48, 51, 44), hov))) g.FillRectangle(b, r);
            using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
            bool news = Newer(latest);
            Gem(g, r.X + 12, r.Y + 12, 0, 5, news || checking ? Glow : 0);
            if (news || checking) idle.Add(new RectangleF(r.X + 3, r.Y + 3, 18, 18));
            Outlined(g, upd, fSmall, news ? Ui.HudYellow : Ease.Mix(Ui.HudText, Ui.HudYellow, hov), r.X + 22, r.Y + 4);
            Hot(r, "upd", () => { if (updating) return; if (Newer(latest)) OfferUpdate(); else CheckUpdate(true); });
        }
        void DrawToast(Graphics g)
        {
            var t = toast;
            if (t != toastSeen) { toastSeen = t; toastOpen.Run(0, 1, 200, Ease.OutCubic); Kick(); }   // (each message unfolds)
            float w = 560;
            var ts = g.MeasureString(t.Text ?? "", fSmall, (int)(w - 28));
            float h = 32 + (string.IsNullOrEmpty(t.Text) ? 4 : ts.Height + 12) + (t.Buttons.Count > 0 ? 40 : 0);
            var r = new RectangleF(W / 2f - w / 2f, Math.Min(586, H - 44 - h), w, h);
            var open = Unfold(r, toastOpen.Value);
            using (var b = new SolidBrush(Color.FromArgb(70, 0, 0, 0))) g.FillRectangle(b, open.X + 4, open.Y + 6, open.Width, open.Height);
            HudPanel(g, open, true);
            var st = g.Save();
            g.SetClip(open, CombineMode.Intersect);
            Gem(g, r.X + 16, r.Y + 13, t.Error ? 2 : 0, 6, t.Busy ? Glow : 0);
            if (t.Busy) idle.Add(new RectangleF(r.X + 4, r.Y + 1, 24, 24));
            Outlined(g, t.Title, fHead, Ui.HudYellow, r.X + 28, r.Y + 4);
            if (!t.Busy) {
                var cr = new RectangleF(r.Right - 30, r.Y + 2, 26, 22);
                if (Hv("tclose") > 0) using (var b = new SolidBrush(Color.FromArgb((int)(50 * Hv("tclose")), 255, 255, 255))) g.FillRectangle(b, cr);
                using (var p = new Pen(Ui.HudMuted, 1.5f)) { g.DrawLine(p, cr.X + 8, cr.Y + 6, cr.X + 18, cr.Y + 16); g.DrawLine(p, cr.X + 18, cr.Y + 6, cr.X + 8, cr.Y + 16); }
                Hot(cr, "tclose", () => { toast = null; Invalidate(); });
            }
            using (var p = new Pen(Ui.HudInner)) g.DrawLine(p, r.X + 1, r.Y + 26, r.Right - 1, r.Y + 26);
            if (!string.IsNullOrEmpty(t.Text)) DrawWrappedOutlined(g, t.Text, fSmall, Ui.HudText, new RectangleF(r.X + 14, r.Y + 34, w - 28, ts.Height + 4));
            float bx = r.X + 14;
            for (int i = 0; i < t.Buttons.Count; i++) {
                var bt = t.Buttons[i];
                float bw = g.MeasureString(bt.Key, fSmallBold).Width + 28;
                var act = bt.Value;
                TextButton(g, new RectangleF(bx, r.Bottom - 38, bw, 28), bt.Key, "tb" + i, () => { act(); Invalidate(); });
                bx += bw + 8;
            }
            g.Restore(st);
        }
        void HudPanel(Graphics g, RectangleF r, bool solid = false)
        {
            using (var b = new SolidBrush(solid ? Color.FromArgb(250, 30, 32, 28) : Ui.Hud)) g.FillRectangle(b, r);
            using (var p = new Pen(Ui.HudInner)) g.DrawRectangle(p, r.X - 1, r.Y - 1, r.Width + 2, r.Height + 2);
            using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
            using (var p = new Pen(Ui.HudLine, 2)) {   // the corner brackets
                float c = 7;
                foreach (var pt in new[] { new PointF(r.Left, r.Top), new PointF(r.Right, r.Top), new PointF(r.Left, r.Bottom), new PointF(r.Right, r.Bottom) }) {
                    float sx = pt.X == r.Left ? 1 : -1, sy = pt.Y == r.Top ? 1 : -1;
                    g.DrawLine(p, pt.X + 3 * sx, pt.Y + 3 * sy, pt.X + (3 + c) * sx, pt.Y + 3 * sy);
                    g.DrawLine(p, pt.X + 3 * sx, pt.Y + 3 * sy, pt.X + 3 * sx, pt.Y + (3 + c) * sy);
                }
            }
        }
        // the green gem of the HUD headers (state 2: red - a problem); glow 0..1: lit up, with a halo (it breathes while
        // something is going on)
        void Gem(Graphics g, float cx, float cy, int state, float r = 6, float glow = 0)
        {
            Color fill = state == 2 ? Ui.GemRed : Ui.Lighter(Ui.Gem, 0.35f * glow);
            Color dark = state == 2 ? Ui.GemRedDark : Ui.GemDark;
            using (var b = new SolidBrush(fill)) g.FillPolygon(b, Ui.Diamond(cx, cy, r));
            using (var p = new Pen(dark)) g.DrawPolygon(p, Ui.Diamond(cx, cy, r));
            using (var p = new Pen(state == 2 ? Color.FromArgb(255, 190, 180) : Ui.GemLight, 1.3f)) g.DrawLine(p, cx - r * 0.45f, cy - r * 0.05f, cx - r * 0.05f, cy - r * 0.45f);
            if (glow > 0.02f) using (var b = new SolidBrush(Color.FromArgb((int)(80 * glow), Ui.GemLight))) g.FillPolygon(b, Ui.Diamond(cx, cy, r + 3));
        }
        // HUD text: white with a black outline, as in the game
        void Outlined(Graphics g, string text, Font f, Color color, float x, float y)
        {
            if (string.IsNullOrEmpty(text)) return;
            using (var path = new GraphicsPath()) {
                path.AddString(text, f.FontFamily, (int)f.Style, f.Size, new PointF(x, y), StringFormat.GenericTypographic);
                using (var p = new Pen(Color.FromArgb(235, 0, 0, 0), 3f) { LineJoin = LineJoin.Round }) g.DrawPath(p, path);
                using (var b = new SolidBrush(color)) g.FillPath(b, path);
            }
        }
        void DrawWrappedOutlined(Graphics g, string text, Font f, Color color, RectangleF r)
        {
            // line breaks where DrawString would put them, each line outlined
            var words = text.Split(' ');
            var line = new StringBuilder();
            float y = r.Y;
            foreach (string w in words) {
                string tryLine = line.Length == 0 ? w : line + " " + w;
                if (g.MeasureString(tryLine, f).Width > r.Width && line.Length > 0) {
                    Outlined(g, line.ToString(), f, color, r.X, y);
                    y += f.Size * 1.45f;
                    line.Clear().Append(w);
                } else { line.Clear().Append(tryLine); }
            }
            if (line.Length > 0) Outlined(g, line.ToString(), f, color, r.X, y);
        }

        void Hot(RectangleF r, string id, Action click) { hots.Add(new HotSpot { R = r, Id = id, Click = click }); }
        HotSpot HotAt(Point p)
        {
            var q = new PointF(p.X / k, p.Y / k);
            for (int i = hots.Count - 1; i >= 0; i--) if (hots[i].R.Contains(q)) return hots[i];
            return null;
        }
        void GoTo(Page p)
        {
            if (page == p) return;
            EndEdit(true);
            if (saveAt != DateTime.MaxValue) SaveNow();
            page = p;
            scroll = 0;
            if (p == Page.Settings) { LoadGames(); }
            if (p == Page.Play) LoadServers();
            if (toast != null && !toast.Busy && toast.Buttons.Count == 0) toast = null;
            // the dialog reshapes (and the title with it, to and from the Play page), the new page unrolls in it
            homeAmt.To(p == Page.Play ? 1 : 0, 340);
            if (p != Page.Play) panelH.To(p == Page.Settings ? 486 : 430, 260);
            unroll.Run(0, 1, 320, Ease.OutCubic, 60);
            if (p == Page.Play) { PopStart(180); if (session != null) sessOpen.Run(0, 1, 280, Ease.OutCubic, 180); }
            Kick();
            Invalidate();
        }

        // ---------------------------------------------------------------- input
        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            var h = HotAt(e.Location);
            string id = h == null ? null : h.Id;
            if (id != hover) { SetHover(id); Cursor = h != null ? Cursors.Hand : Cursors.Default; }
        }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); if (hover != null) SetHover(null); }
        void SetHover(string id)
        {
            fades.Set(hover, false, 160);   // (highlights fade out a little slower than they come)
            fades.Set(id, true, 90);
            hover = id;
            Invalidate();
            Kick();
        }
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (capture != null) {
                Keys kk = e.Button == MouseButtons.XButton1 ? Keys.XButton1 : e.Button == MouseButtons.XButton2 ? Keys.XButton2 : e.Button == MouseButtons.Middle ? Keys.MButton : Keys.None;
                if (kk != Keys.None) { Change(capture, Keys2.Name(kk, ModifierKeys.HasFlag(Keys.Control), ModifierKeys.HasFlag(Keys.Shift), ModifierKeys.HasFlag(Keys.Alt))); capture = null; Invalidate(); }
                return;
            }
            if (e.Button != MouseButtons.Left) return;
            var h = HotAt(e.Location);
            if (h == null && e.Y < 30 * k) { ReleaseCapture(); SendMessage(Handle, 0xA1, (IntPtr)2, IntPtr.Zero); return; }   // drag by the title bar
            press = h == null ? null : h.Id;
            Invalidate();
        }
        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (e.Button != MouseButtons.Left) return;
            var h = HotAt(e.Location);
            string was = press;
            press = null;
            if (h != null && h.Id == was && h.Click != null) h.Click();
            Invalidate();
        }
        protected override void OnMouseWheel(MouseEventArgs e)
        {
            base.OnMouseWheel(e);
            if (page != Page.Settings || scrollMax <= 0) return;
            scroll = Math.Max(0, Math.Min(scrollMax, scroll - e.Delta / 120f * 48));
            Invalidate();
        }
        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            if (edit != null && edit.Focused) return base.ProcessCmdKey(ref msg, keyData);
            Keys code = keyData & Keys.KeyCode;
            if (capture != null) {
                if (code == Keys.Escape) { capture = null; Invalidate(); return true; }
                if (code == Keys.Back) { Change(capture, "none"); capture = null; Invalidate(); return true; }
                if (code == Keys.ControlKey || code == Keys.ShiftKey || code == Keys.Menu || code == Keys.LWin || code == Keys.RWin) return true;
                string n = Keys2.Name(code, (keyData & Keys.Control) != 0, (keyData & Keys.Shift) != 0, (keyData & Keys.Alt) != 0);
                if (n != null) { Change(capture, n); capture = null; Invalidate(); }
                return true;
            }
            if (code == Keys.Escape && toast != null && !toast.Busy) { toast = null; Invalidate(); return true; }
            if (code == Keys.Tab) { GoTo((Page)(((int)page + ((keyData & Keys.Shift) != 0 ? 2 : 1)) % 3)); return true; }
            if (page == Page.Play) {
                if (code == Keys.Enter) { Start(); return true; }
                if ((code == Keys.Up || code == Keys.Down) && servers.Count > 0) { ChooseServer((sel + servers.Count + (code == Keys.Up ? -1 : 1)) % servers.Count); return true; }
            }
            if (page == Page.Settings && (code == Keys.Up || code == Keys.Down)) {
                var cats = Categories();
                int i = Math.Max(0, cats.IndexOf(cat));
                OpenCat(cats[(i + cats.Count + (code == Keys.Up ? -1 : 1)) % cats.Count]); return true;
            }
            return base.ProcessCmdKey(ref msg, keyData);
        }
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            EndEdit(true);
            if (saveAt != DateTime.MaxValue) SaveNow();
            base.OnFormClosing(e);
        }

    }
}
