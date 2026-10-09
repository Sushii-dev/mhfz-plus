// MHFZ Plus Settings: a window for the frame-rate mod's settings (mhfz-fps.ini), for each game MHFZ Plus is set up for.
//
// Every setting the mod reads is described once in Schema() below - its key, kind, default, range and help text - and
// the window is built from that list, so a new setting in the mod needs one line here. The file is edited in place:
// comments and settings this program doesn't know stay as they are.
//
// Which file: MezeLounge keeps the settings next to the game (fps_mode=stay); Rain keeps them in
// fps-state\rain\mhfz-fps.ini and MHFZ Plus copies them into the game folder for a session (fps_mode=swap) - while
// the game runs, the copy in the game folder is the one the mod reads (it re-reads it twice a second), so that one is
// edited then and the change shows in the game at once.
//
// Build: tools\buildsettings.ps1 (the C# compiler that comes with Windows' .NET Framework 4; no SDK needed).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("MHFZ Plus Settings")]
[assembly: System.Reflection.AssemblyProduct("MHFZ Plus")]
[assembly: System.Reflection.AssemblyVersion("1.2.0.0")]

namespace MhfzPlus
{
    enum Kind { Bool, Int, Choice, Key, Text }

    class Setting
    {
        public string Key, Label, Default, Help, Group;
        public Kind Kind;
        public bool Advanced;
        public int Min, Max;
        public string[][] Choices;  // {value, label}
        public bool Editable;       // (a choice list that also takes a number of its own)
    }

    // ------------------------------------------------------------------------------------------------ the settings
    static class Schema
    {
        static List<Setting> list = new List<Setting>();
        static string group = "";
        static Setting Add(string key, Kind kind, string def, string label, string help, bool adv = false)
        {
            var s = new Setting { Key = key, Kind = kind, Default = def, Label = label, Help = help, Advanced = adv, Group = group };
            list.Add(s);
            return s;
        }
        static void Bool(string key, string def, string label, string help, bool adv = false) { Add(key, Kind.Bool, def, label, help, adv); }
        static void Int(string key, string def, int min, int max, string label, string help, bool adv = false)
        {
            var s = Add(key, Kind.Int, def, label, help, adv);
            s.Min = min; s.Max = max;
        }
        static void Choice(string key, string def, string label, string help, bool adv, params string[] pairs)
        {
            var s = Add(key, Kind.Choice, def, label, help, adv);
            s.Choices = new string[pairs.Length / 2][];
            for (int i = 0; i < pairs.Length; i += 2) s.Choices[i / 2] = new[] { pairs[i], pairs[i + 1] };
        }
        static void Key(string key, string def, string label, string help, bool adv = false) { Add(key, Kind.Key, def, label, help, adv); }

        public static List<Setting> All()
        {
            if (list.Count > 0) return list;
            group = "General";
            Bool("enabled", "1", "Frame-rate mod on", "Off: the game runs exactly as without the mod (Pause switches it while playing).");
            Choice("target_fps", "0", "Frame rate", "How many frames per second to aim for. Automatic: just below your monitor's refresh rate. " +
                   "The game itself always runs its 30 ticks per second.", false,
                   "0", "Automatic (your monitor's refresh rate)", "60", "60", "90", "90", "120", "120", "144", "144", "165", "165", "175", "175", "240", "240");
            list[list.Count - 1].Editable = true;
            Choice("present_interval", "0", "VSync for the extra frames", "Off is best in windowed mode and with G-Sync/FreeSync. " +
                   "Turn it on only if you play in exclusive fullscreen without G-Sync/FreeSync and see tearing.", false,
                   "0", "Off", "1", "On", "-1", "Whatever the game asks for");
            Bool("latency_trim", "0", "Lower input latency (testing)", "After each of the game's own frames the game waits the little time " +
                 "its next frames would have waited anyway, so it reads your input a few milliseconds later. No frames are lost and the game " +
                 "keeps its speed.");
            Bool("input_latch", "0", "Catch quick button taps (testing)", "The game reads keyboard, mouse and controller once per tick (30 times a " +
                 "second), so a very quick tap can fall between two reads and get lost - in menus it feels like a press that did nothing. " +
                 "With this on, such a tap is caught and handed to the game at its next read. Nothing is delayed.");
            Choice("mode", "2", "Mode", "Interpolate is the normal mode. The others are for testing.", true,
                   "2", "Interpolate (normal)", "0", "Vanilla 30 fps (mod loaded, idle)", "1", "Repeat frames without interpolation (testing)");
            Bool("precise_tick", "0", "Exact 33.3 ms ticks", "Off keeps the game's own tick timing exactly (recommended).", true);
            Bool("hotkeys", "1", "Keys", "Off: none of the keys below do anything.");

            group = "FPS counter";
            Choice("overlay", "0", "Show at start", "The Insert key cycles: hidden, counter, detailed.", false,
                   "0", "Hidden", "1", "Counter (fps, 1% low, game rate)", "2", "Detailed (frame times and a graph)");
            Choice("overlay_pos", "0", "Position", "", false, "0", "Top left", "1", "Top right", "2", "Bottom left", "3", "Bottom right", "4", "Top centre");
            Choice("overlay_scale", "0", "Size", "", false, "0", "Automatic", "1", "1x", "2", "2x", "3", "3x", "4", "4x");

            group = "Keys";
            Key("key_toggle", "pause", "Mod on/off", "Compare with and without the mod while playing.");
            Key("key_overlay", "insert", "FPS counter", "Cycles hidden, counter, detailed.");
            Key("key_capture", "delete", "Save a clip", "Saves the last second of frames when something looks wrong (the clip recorder must be on).");
            Key("key_latency", "ctrl+shift+5", "Lower input latency on/off", "");
            Bool("debug_keys", "0", "Developer keys", "Turns on the keys below and the Ctrl+Shift+F7..F12 test switches.", true);
            Key("key_campaths", "ctrl+shift+1", "Camera paths on/off", "Developer key (needs developer keys on).", true);
            Key("key_carry", "ctrl+shift+2", "Carried render targets (cycle)", "Developer key.", true);
            Key("key_camprobe", "ctrl+shift+f6", "Camera probe (MezeLounge)", "Developer key: for a few seconds, logs which game code moves the camera and how far it tilts (for the camera-limit research).", true);
            Key("key_nonpos", "ctrl+shift+3", "Fog/light blending rule", "Developer key.", true);
            Key("key_field", "ctrl+shift+4", "Repeated objects by the motion around them", "Developer key.", true);
            Key("key_shadow", "none", "Shadow map reuse on/off", "Developer key.", true);
            Key("key_debug", "none", "Debug view", "Developer key.", true);
            Key("key_mode", "none", "Mode (cycle)", "Developer key.", true);

            group = "Clips";
            Bool("retro", "1", "Clip recorder", "Keeps the last frames in video memory so the clip key can save them. Costs video memory " +
                 "(see below); turn it off on graphics cards with little memory.");
            Int("retro_frames", "192", 48, 600, "Frames kept", "192 frames is a bit more than one second at 150 fps.");
            Int("retro_ticks", "16", 0, 48, "Game ticks kept for analysis", "What the mod saw on the last ticks, saved with the clip " +
                "(4-7 MB each in a crowd, from the game's 2 GB).", true);

            group = "Quality";
            Choice("lerp_nonpos", "2", "Fog, light and colour blending", "", true, "2", "Everything that moves smoothly (new)", "1", "Small changes only (old)", "0", "Off");
            Bool("cam_paths", "1", "Camera paths", "A camera turn stays a turn in the in-between frames instead of a straight blend.", true);
            Bool("cam_fit", "1", "Camera estimate by objects explained", "", true);
            Bool("group_cam", "1", "Repeated objects: other camera predictions when paired poorly", "Keeps distant trees and leaves steady in fast camera flicks.", true);
            Bool("slot_rows", "1", "Light slot coupling", "A lamp that changes light slots switches its range and colour with it (no flash on characters).", true);
            Bool("text_runs", "1", "Screen text as whole strings", "", true);
            Bool("text_ckeys", "1", "Glyphs by their pixels", "", true);
            Bool("slerp_bones", "1", "Skeletons: rotation blending", "", true);
            Choice("carry_restore", "1", "Carried render targets", "", true, "1", "Put back after the in-between frames", "2", "Also each in-between frame", "0", "Off");
            Bool("field_match", "0", "Repeated objects by the motion around them", "", true);
            Choice("pacing", "2", "Frame pacing", "", true, "2", "Slots before the deadline, content for when shown", "1", "Evenly spaced (fewer frames)", "0", "Slots, content for when built");
            Bool("late_slot", "0", "Use slots missed by up to half an interval", "More frames in a crowd, but two can land closer than one refresh.", true);
            Int("max_extra", "10", 1, 16, "Most in-between frames per tick", "", true);
            Int("margin_us", "1500", 0, 10000, "Slack before the game's deadline (microseconds)", "", true);
            Bool("interp_vs", "1", "Interpolate vertex shader constants", "", true);
            Bool("interp_xform", "1", "Interpolate fixed-function transforms", "", true);
            Bool("interp_up", "1", "Interpolate CPU-built geometry", "", true);
            Bool("interp_ps", "1", "Interpolate pixel shader constants", "", true);
            Int("snap_ratio_pct", "100", 1, 1000, "Jump threshold (%)", "", true);
            Int("smooth_ratio_pct", "20", 1, 100, "Smooth change threshold (%)", "", true);

            group = "Performance";
            Choice("regroup", "2", "Regroup shadow passes", "Draws passes that switch target on every draw one target after the other (crowds).", true,
                   "2", "Game frames and in-between frames", "1", "In-between frames only", "0", "Off");
            Bool("shadow_reuse", "0", "Reuse the game frame's shadow maps", "", true);
            Int("pair_threads", "0", 0, 16, "Matching threads (0 = automatic)", "", true);
            Bool("pair_async", "1", "Match in the background", "", true);
            Bool("precompute", "1", "Precompute blended constants on all threads", "", true);
            Bool("gpu_queue", "1", "Let the GPU work on one frame while the next is built", "", true);
            Bool("lock_copy", "1", "Copy textures the game rewrites mid-frame", "", true);
            Bool("replay_filter", "1", "Skip constant uploads that change nothing", "", true);
            Bool("flush_endscene", "0", "Run the game frame at every EndScene", "", true);
            Add("d3d9_backend", Kind.Text, "", "Direct3D 9 implementation", "Empty: Windows' own. A path such as dxvk\\d3d9.dll uses that one (DXVK).", true);

            group = "Diagnostics";
            Int("stats_interval", "60", 0, 3600, "Stats line in the log every (seconds, 0 = off)", "mhfz-fps.log next to the game.");
            Bool("profile", "0", "Sampling profiler (stats line)", "", true);
            Bool("selfcheck", "0", "Self-check (predict real frames, measure the error)", "", true);
            Bool("exc_log", "0", "Log every exception the game raises", "", true);
            Bool("debug_marker", "0", "Marker on in-between frames", "", true);
            Bool("debug_unlerped", "0", "Draw non-interpolated draws magenta", "", true);
            Int("regroup_check", "0", 0, 600, "Check the regrouping every N seconds", "", true);
            return list;
        }
    }

    // ------------------------------------------------------------------------------------------------ keys
    static class Keys2
    {
        // names the mod's ini reader knows (src\fps.cpp, LoadConfig: K)
        static readonly Dictionary<Keys, string> names = new Dictionary<Keys, string> {
            {Keys.Delete, "delete"}, {Keys.Insert, "insert"}, {Keys.Home, "home"}, {Keys.End, "end"}, {Keys.PageUp, "pageup"},
            {Keys.PageDown, "pagedown"}, {Keys.Pause, "pause"}, {Keys.Scroll, "scrolllock"}, {Keys.PrintScreen, "printscreen"},
            {Keys.Back, "backspace"}, {Keys.Tab, "tab"}, {Keys.Up, "up"}, {Keys.Down, "down"}, {Keys.Left, "left"}, {Keys.Right, "right"},
            {Keys.Space, "space"}, {Keys.Return, "enter"}, {Keys.CapsLock, "capslock"}, {Keys.NumLock, "numlock"},
            {Keys.XButton1, "mouse4"}, {Keys.XButton2, "mouse5"}, {Keys.MButton, "mmb"},
            {Keys.Multiply, "multiply"}, {Keys.Add, "add"}, {Keys.Subtract, "subtract"}, {Keys.Decimal, "decimal"}, {Keys.Divide, "divide"},
            {Keys.Oem1, "semicolon"}, {Keys.Oemplus, "plus"}, {Keys.Oemcomma, "comma"}, {Keys.OemMinus, "minus"}, {Keys.OemPeriod, "period"},
            {Keys.Oem2, "slash"}, {Keys.Oem3, "backquote"}, {Keys.Oem4, "lbracket"}, {Keys.Oem5, "backslash"}, {Keys.Oem6, "rbracket"},
            {Keys.Oem7, "quote"},
        };
        static readonly Dictionary<string, string> pretty = new Dictionary<string, string> {
            {"pageup", "Page Up"}, {"pagedown", "Page Down"}, {"scrolllock", "Scroll Lock"}, {"printscreen", "Print Screen"},
            {"capslock", "Caps Lock"}, {"numlock", "Num Lock"}, {"mouse4", "Mouse 4"}, {"mouse5", "Mouse 5"}, {"mmb", "Middle mouse"},
            {"lbracket", "["}, {"rbracket", "]"}, {"backslash", "\\"}, {"semicolon", ";"}, {"quote", "'"}, {"comma", ","}, {"period", "."},
            {"slash", "/"}, {"backquote", "`"}, {"minus", "-"}, {"plus", "="}, {"multiply", "Num *"}, {"add", "Num +"},
            {"subtract", "Num -"}, {"decimal", "Num ."}, {"divide", "Num /"},
        };
        // virtual-key codes an older ini may hold ("19", "45", "46")
        static readonly Dictionary<int, string> codes = new Dictionary<int, string> {
            {19, "pause"}, {45, "insert"}, {46, "delete"}, {36, "home"}, {35, "end"}, {33, "pageup"}, {34, "pagedown"}, {145, "scrolllock"},
        };

        public static string Name(Keys code, bool ctrl, bool shift, bool alt)
        {
            string k = null;
            if (code >= Keys.A && code <= Keys.Z) k = ((char)('a' + (code - Keys.A))).ToString();
            else if (code >= Keys.D0 && code <= Keys.D9) k = ((char)('0' + (code - Keys.D0))).ToString();
            else if (code >= Keys.F1 && code <= Keys.F24) k = "f" + (1 + (code - Keys.F1));
            else if (code >= Keys.NumPad0 && code <= Keys.NumPad9) k = "num" + (code - Keys.NumPad0);
            else if (!names.TryGetValue(code, out k)) return null;
            var sb = new StringBuilder();
            if (ctrl) sb.Append("ctrl+");
            if (shift) sb.Append("shift+");
            if (alt) sb.Append("alt+");
            return sb.Append(k).ToString();
        }

        // the ini's value as the mod reads it: a name, "none", or a virtual-key code
        public static string Normalize(string v)
        {
            v = (v ?? "").Trim().ToLowerInvariant();
            if (v == "" || v == "0") return "none";
            int c;
            if (v.Length >= 2 && int.TryParse(v, out c)) return codes.ContainsKey(c) ? codes[c] : v;
            return v;
        }

        public static string Display(string v)
        {
            v = Normalize(v);
            if (v == "none") return "(none)";
            var parts = v.Split('+');
            for (int i = 0; i < parts.Length; i++) {
                string p = parts[i], q;
                if (pretty.TryGetValue(p, out q)) parts[i] = q;
                else if (p == "ctrl") parts[i] = "Ctrl";
                else if (p == "shift") parts[i] = "Shift";
                else if (p == "alt") parts[i] = "Alt";
                else if (p.Length == 1) parts[i] = p.ToUpperInvariant();
                else if (p.StartsWith("num") && p.Length == 4) parts[i] = "Num " + p.Substring(3);
                else parts[i] = char.ToUpperInvariant(p[0]) + p.Substring(1);
            }
            return string.Join("+", parts);
        }

        // a key the game itself uses while playing (moving, menus, chat), bound without a modifier
        public static bool GameKey(string v)
        {
            v = Normalize(v);
            if (v.Contains("+")) return false;
            if (v.Length == 1) return true;
            return new[] { "space", "enter", "tab", "up", "down", "left", "right", "backspace" }.Contains(v);
        }
    }

    // ------------------------------------------------------------------------------------------------ ini file
    // The [fps] section of mhfz-fps.ini, edited in place: existing lines keep their place and comments, new keys go at
    // the end of the section.
    class IniFile
    {
        public string Path;
        List<string> lines = new List<string>();
        public IniFile(string path) { Path = path; if (File.Exists(path)) lines = File.ReadAllLines(path).ToList(); }

        int SectionStart()
        {
            for (int i = 0; i < lines.Count; i++) if (lines[i].Trim().Equals("[fps]", StringComparison.OrdinalIgnoreCase)) return i;
            return -1;
        }
        int SectionEnd(int start)
        {
            for (int i = start + 1; i < lines.Count; i++) if (lines[i].TrimStart().StartsWith("[")) return i;
            return lines.Count;
        }
        int Find(string key)
        {
            int s = SectionStart();
            if (s < 0) return -1;
            int e = SectionEnd(s);
            for (int i = s + 1; i < e; i++) {
                string t = lines[i].TrimStart();
                if (t.StartsWith(";") || t.StartsWith("#")) continue;
                int eq = t.IndexOf('=');
                if (eq > 0 && t.Substring(0, eq).Trim().Equals(key, StringComparison.OrdinalIgnoreCase)) return i;
            }
            return -1;
        }
        public string Get(string key)
        {
            int i = Find(key);
            if (i < 0) return null;
            string t = lines[i];
            return t.Substring(t.IndexOf('=') + 1).Trim();
        }
        public void Set(string key, string value)
        {
            int i = Find(key);
            if (i >= 0) { lines[i] = key + "=" + value; return; }
            int s = SectionStart();
            if (s < 0) { if (lines.Count > 0 && lines[lines.Count - 1].Trim() != "") lines.Add(""); lines.Add("[fps]"); s = lines.Count - 1; }
            int e = SectionEnd(s);
            while (e > s + 1 && lines[e - 1].Trim() == "") e--;  // (before the blank lines that end the section)
            lines.Insert(e, key + "=" + value);
        }
        public void Save()
        {
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path));
            string tmp = Path + ".tmp";
            File.WriteAllText(tmp, string.Join("\r\n", lines) + "\r\n", new UTF8Encoding(false));
            if (File.Exists(Path)) File.Replace(tmp, Path, null); else File.Move(tmp, Path);
        }
    }

    // ------------------------------------------------------------------------------------------------ games
    class Game
    {
        public string Name, Title, Dir, Mode;  // Mode: "swap" (Rain: the settings live in fps-state between sessions) or "stay"
        public string Base;
        public string LiveIni { get { return System.IO.Path.Combine(Dir, "mhfz-fps.ini"); } }
        public string KeptIni { get { return System.IO.Path.Combine(Base, "fps-state", Name, "mhfz-fps.ini"); } }
        public string DefaultIni { get { return System.IO.Path.Combine(Base, "fps", "mhfz-fps.ini"); } }
        // the file the mod reads (or will read at the next start)
        public string IniPath { get { return Mode == "swap" && !File.Exists(LiveIni) ? KeptIni : LiveIni; } }
        public bool Live { get { return Mode == "swap" && File.Exists(LiveIni); } }
        public string ClipsDir { get { return Mode == "swap" ? System.IO.Path.Combine(Base, "fps-state", Name) : System.IO.Path.Combine(Dir, "retro"); } }

        [DllImport("kernel32", CharSet = CharSet.Unicode)]
        static extern int GetPrivateProfileString(string sec, string key, string def, StringBuilder buf, int size, string file);
        static string Ini(string file, string sec, string key, string def)
        {
            var sb = new StringBuilder(1024);
            GetPrivateProfileString(sec, key, def, sb, sb.Capacity, file);
            return sb.ToString();
        }

        public static string FindBase()
        {
            string here = AppDomain.CurrentDomain.BaseDirectory;
            if (File.Exists(System.IO.Path.Combine(here, "MHFZ-Plus.ini"))) return here;
            return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MHFZ-Plus");
        }
        public static List<Game> Load(string b)
        {
            var list = new List<Game>();
            string ini = System.IO.Path.Combine(b, "MHFZ-Plus.ini");
            foreach (var g in new[] { new[] { "meze", "MezeLounge" }, new[] { "rain", "Rain" } }) {
                string dir = Ini(ini, g[0], "game", "");
                if (dir == "" || !Directory.Exists(dir)) continue;
                if (Ini(ini, g[0], "fps", "1") == "0") continue;
                list.Add(new Game { Name = g[0], Title = g[1], Dir = dir, Base = b,
                                    Mode = Ini(ini, g[0], "fps_mode", g[0] == "rain" ? "swap" : "stay") });
            }
            return list;
        }
    }

    // ------------------------------------------------------------------------------------------------ key capture
    class KeyDialog : Form
    {
        public string Result;
        Label shown;
        public KeyDialog(string action)
        {
            Text = "Set key";
            Font = new Font("Segoe UI", 9f);
            AutoScaleMode = AutoScaleMode.None;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            Func<int, int> S = MainForm.S;
            ClientSize = new Size(S(440), S(160));
            KeyPreview = true;
            var l = new Label { Text = "Press the key or combination for \"" + action + "\".\nEsc cancels.", Location = new Point(S(16), S(14)), Size = new Size(S(410), S(40)) };
            shown = new Label { Text = "", Location = new Point(S(16), S(60)), Size = new Size(S(410), S(30)), Font = new Font("Segoe UI", 13f, FontStyle.Bold) };
            var none = new Button { Text = "No key", Location = new Point(S(16), S(112)), Size = new Size(S(100), S(32)) };
            none.Click += delegate { Result = "none"; DialogResult = DialogResult.OK; };
            var cancel = new Button { Text = "Cancel", Location = new Point(S(324), S(112)), Size = new Size(S(100), S(32)), DialogResult = DialogResult.Cancel };
            Controls.AddRange(new Control[] { l, shown, none, cancel });
            CancelButton = cancel;
            MouseDown += OnMouse;
            foreach (Control c in Controls) c.MouseDown += OnMouse;
        }
        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            Keys code = keyData & Keys.KeyCode;
            if (code == Keys.Escape) { DialogResult = DialogResult.Cancel; return true; }
            if (code == Keys.ControlKey || code == Keys.ShiftKey || code == Keys.Menu || code == Keys.LWin || code == Keys.RWin) {
                shown.Text = Keys2.Display(Keys2.Name(Keys.A, (keyData & Keys.Control) != 0, (keyData & Keys.Shift) != 0, (keyData & Keys.Alt) != 0)).Replace("A", "...");
                return true;
            }
            string n = Keys2.Name(code, (keyData & Keys.Control) != 0, (keyData & Keys.Shift) != 0, (keyData & Keys.Alt) != 0);
            if (n == null) { shown.Text = "That key can't be used"; return true; }
            Result = n;
            DialogResult = DialogResult.OK;
            return true;
        }
        void OnMouse(object s, MouseEventArgs e)
        {
            Keys k = e.Button == MouseButtons.XButton1 ? Keys.XButton1 : e.Button == MouseButtons.XButton2 ? Keys.XButton2 :
                     e.Button == MouseButtons.Middle ? Keys.MButton : Keys.None;
            if (k == Keys.None) return;
            Result = Keys2.Name(k, ModifierKeys.HasFlag(Keys.Control), ModifierKeys.HasFlag(Keys.Shift), ModifierKeys.HasFlag(Keys.Alt));
            DialogResult = DialogResult.OK;
        }
    }

    // ------------------------------------------------------------------------------------------------ the window
    class MainForm : Form
    {
        List<Game> games;
        Game game;
        IniFile ini;
        Dictionary<string, string> values = new Dictionary<string, string>();  // (what the controls show, as ini text)
        ComboBox gameBox;
        TabControl tabs;
        CheckBox advanced;
        Label status, warn;
        Button save, revert;
        ToolTip tips = new ToolTip { AutoPopDelay = 20000, InitialDelay = 300 };
        bool loading, dirty, wasLive;
        int shownGame = -1;
        // (pixel sizes are for 96 dpi and scaled to the screen's: the process is dpi-aware, so text stays sharp on 4K screens)
        public static float UiScale = 1f;
        public static int S(int v) { return (int)Math.Round(v * UiScale); }
        static Size Sz(int w, int h) { return new Size(S(w), S(h)); }
        static Point Pt(int x, int y) { return new Point(S(x), S(y)); }

        public MainForm()
        {
            Text = "MHFZ Plus Settings";
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            AutoScaleMode = AutoScaleMode.None;
            using (var g = CreateGraphics()) UiScale = Math.Max(1f, g.DpiX / 96f);
            Font = new Font("Segoe UI", 9f);
            ClientSize = Sz(780, 620);
            MinimumSize = Sz(660, 480);
            StartPosition = FormStartPosition.CenterScreen;

            var top = new Panel { Dock = DockStyle.Top, Height = S(44) };
            top.Controls.Add(new Label { Text = "Game:", Location = Pt(12, 13), AutoSize = true });
            gameBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = Pt(60, 10), Width = S(200) };
            gameBox.SelectedIndexChanged += delegate {
                if (gameBox.SelectedIndex == shownGame) return;
                if (!ConfirmDiscard()) { gameBox.SelectedIndex = shownGame; return; }
                shownGame = gameBox.SelectedIndex;
                game = games[shownGame];
                LoadGame();
            };
            top.Controls.Add(gameBox);
            advanced = new CheckBox { Text = "Show advanced settings", Location = Pt(290, 12), AutoSize = true };
            advanced.CheckedChanged += delegate { Build(); };
            top.Controls.Add(advanced);

            var bottom = new Panel { Dock = DockStyle.Bottom, Height = S(84) };
            status = new Label { Location = Pt(12, 6), Size = Sz(750, 18), ForeColor = Color.DimGray, AutoEllipsis = true, Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top };
            warn = new Label { Location = Pt(12, 25), Size = Sz(750, 18), ForeColor = Color.Firebrick, AutoEllipsis = true, Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top };
            save = new Button { Text = "Save", Size = Sz(96, 30), Anchor = AnchorStyles.Right | AnchorStyles.Bottom };
            revert = new Button { Text = "Undo changes", Size = Sz(110, 30), Anchor = AnchorStyles.Right | AnchorStyles.Bottom };
            var defaults = new Button { Text = "Recommended settings", Size = Sz(170, 30), Anchor = AnchorStyles.Left | AnchorStyles.Bottom };
            var openFile = new Button { Text = "Open the file", Size = Sz(110, 30), Anchor = AnchorStyles.Left | AnchorStyles.Bottom };
            save.Location = new Point(bottom.Width - S(108), S(46)); revert.Location = new Point(bottom.Width - S(226), S(46));
            defaults.Location = Pt(12, 46); openFile.Location = Pt(190, 46);
            save.Click += delegate { Save(); };
            revert.Click += delegate { LoadGame(); };
            defaults.Click += delegate { Recommended(); };
            openFile.Click += delegate { if (game != null && File.Exists(game.IniPath)) Process.Start("notepad.exe", "\"" + game.IniPath + "\""); };
            // (the game starting or closing changes where its settings are on Rain: keep the status line current)
            var poll = new Timer { Interval = 2000 };
            poll.Tick += delegate { if (game != null && game.Live != wasLive) UpdateStatus(); };
            poll.Start();
            bottom.Controls.AddRange(new Control[] { status, warn, save, revert, defaults, openFile });
            bottom.Resize += delegate { save.Left = bottom.Width - S(108); revert.Left = bottom.Width - S(226); };

            tabs = new TabControl { Dock = DockStyle.Fill };
            Controls.Add(tabs);
            Controls.Add(top);
            Controls.Add(bottom);

            games = Game.Load(Game.FindBase());
            if (games.Count == 0) {
                status.Text = "No game set up yet: run the MHFZ Plus setup first.";
                save.Enabled = revert.Enabled = defaults.Enabled = openFile.Enabled = false;
                return;
            }
            foreach (var g in games) gameBox.Items.Add(g.Title);
            gameBox.SelectedIndex = 0;  // (loads it)
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            // (testing: MHFZPLUS_SETTINGS_SHOTS=<folder> draws every page into PNGs - plain, then with the advanced settings - and exits)
            string shots = Environment.GetEnvironmentVariable("MHFZPLUS_SETTINGS_SHOTS");
            if (!string.IsNullOrEmpty(shots)) {
                Directory.CreateDirectory(shots);
                for (int pass = 0; pass < 2; pass++) {
                    advanced.Checked = pass == 1;
                    for (int i = 0; i < tabs.TabCount; i++) {
                        tabs.SelectedIndex = i;
                        Application.DoEvents();
                        using (var bmp = new Bitmap(Width, Height)) {
                            DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
                            bmp.Save(Path.Combine(shots, string.Format("{0}{1}-{2}.png", pass, i, tabs.TabPages[i].Text.Replace(" ", ""))));
                        }
                    }
                }
                dirty = false;
                Close();
            }
        }
        bool ConfirmDiscard()
        {
            if (!dirty) return true;
            var r = MessageBox.Show(this, "Save your changes first?", Text, MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
            if (r == DialogResult.Cancel) return false;
            if (r == DialogResult.Yes) Save();
            return true;
        }
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (!ConfirmDiscard()) e.Cancel = true;
            base.OnFormClosing(e);
        }

        void LoadGame()
        {
            string path = game.IniPath;
            if (!File.Exists(path) && File.Exists(game.DefaultIni)) {  // (no settings of the player's own yet: start from the defaults)
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.Copy(game.DefaultIni, path);
            }
            ini = new IniFile(path);
            values.Clear();
            foreach (var s in Schema.All()) {
                string v = ini.Get(s.Key);
                values[s.Key] = v == null ? s.Default : s.Kind == Kind.Key ? Keys2.Normalize(v) : v;
            }
            dirty = false;
            Build();
            UpdateStatus();
        }

        void UpdateStatus()
        {
            if (game == null) return;
            wasLive = game.Live;
            status.Text = (wasLive ? "The game is running: changes apply in the game as soon as you save. " :
                           game.Mode == "swap" ? "Changes apply the next time you start the game. " : "Changes apply at once if the game runs. ") + game.IniPath;
        }

        void Recommended()
        {
            if (MessageBox.Show(this, "Set every setting on this page and the advanced ones back to the recommended values?\n(Keys too.)", Text,
                                MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
            foreach (var s in Schema.All()) values[s.Key] = s.Default;
            dirty = true;
            Build();
        }

        void Save()
        {
            if (ini == null) return;
            // the file as it is now, where the game uses it now (Rain: it moves into the game folder while the game runs and
            // back when it closes - the window may have been open across that)
            string path = game.IniPath;
            try {
                if (!File.Exists(path) && File.Exists(game.DefaultIni)) {
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    File.Copy(game.DefaultIni, path);
                }
                var cur = new IniFile(path);
                foreach (var s in Schema.All()) {
                    string v = values[s.Key];
                    string old = cur.Get(s.Key);
                    // (only what differs from what is there, or from the default when the file doesn't name it)
                    if (old == null ? v == s.Default : (s.Kind == Kind.Key ? Keys2.Normalize(old) == v : old == v)) continue;
                    cur.Set(s.Key, v);
                }
                cur.Save();
                ini = cur;
                dirty = false;
                UpdateWarnings();
                UpdateStatus();
            }
            catch (Exception ex) { MessageBox.Show(this, "Could not save " + path + ":\n" + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        void Changed(string key, string v)
        {
            if (loading) return;
            values[key] = v;
            dirty = true;
            UpdateWarnings();
        }

        void UpdateWarnings()
        {
            var keys = Schema.All().Where(s => s.Kind == Kind.Key && values[s.Key] != "none").ToList();
            var dup = keys.GroupBy(s => values[s.Key]).Where(g => g.Count() > 1).Select(g => Keys2.Display(g.Key)).ToList();
            var game = keys.Where(s => Keys2.GameKey(values[s.Key])).Select(s => Keys2.Display(values[s.Key])).ToList();
            var msg = new List<string>();
            if (dup.Count > 0) msg.Add("The same key is set twice: " + string.Join(", ", dup) + ".");
            if (game.Count > 0) msg.Add("The game uses " + string.Join(", ", game) + " itself - pressing it will do both.");
            if (values["retro"] == "0" && values["key_capture"] != "none") msg.Add("The clip recorder is off: the clip key does nothing.");
            warn.Text = string.Join("  ", msg);
            save.Text = dirty ? "Save *" : "Save";
        }

        // the pages, built from the schema
        void Build()
        {
            loading = true;
            int sel = tabs.SelectedIndex;
            tabs.SuspendLayout();
            tabs.TabPages.Clear();
            var groups = new List<string>();
            foreach (var s in Schema.All()) if (!groups.Contains(s.Group)) groups.Add(s.Group);
            foreach (string g in groups) {
                var items = Schema.All().Where(s => s.Group == g && (advanced.Checked || !s.Advanced)).ToList();
                if (items.Count == 0) continue;
                var page = new TabPage(g) { AutoScroll = true, Padding = new Padding(S(8)) };
                var table = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(S(4)) };
                table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, S(320)));
                table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, S(390)));
                foreach (var s in items) AddRow(table, s);
                if (g == "Clips") AddClipInfo(table);
                page.Controls.Add(table);
                tabs.TabPages.Add(page);
            }
            if (sel >= 0 && sel < tabs.TabCount) tabs.SelectedIndex = sel;
            tabs.ResumeLayout();
            loading = false;
            UpdateWarnings();
        }

        void AddRow(TableLayoutPanel t, Setting s)
        {
            var label = new Label { Text = s.Label, AutoSize = true, Margin = new Padding(S(3), S(7), S(3), S(3)), MaximumSize = new Size(S(310), 0) };
            Control c;
            string v = values[s.Key];
            switch (s.Kind) {
                case Kind.Bool: {
                    var cb = new CheckBox { Checked = v.Trim() != "0" && v.Trim() != "", AutoSize = true, Margin = new Padding(S(3), S(7), S(3), S(3)) };
                    cb.CheckedChanged += delegate { Changed(s.Key, cb.Checked ? "1" : "0"); };
                    c = cb;
                    break;
                }
                case Kind.Int: {
                    int n; int.TryParse(v, out n);
                    var nu = new NumericUpDown { Minimum = s.Min, Maximum = s.Max, Value = Math.Max(s.Min, Math.Min(s.Max, n)), Width = S(100) };
                    nu.ValueChanged += delegate { Changed(s.Key, ((int)nu.Value).ToString()); };
                    c = nu;
                    break;
                }
                case Kind.Choice: {
                    var cbx = new ComboBox { Width = S(370), DropDownStyle = s.Editable ? ComboBoxStyle.DropDown : ComboBoxStyle.DropDownList };
                    foreach (var ch in s.Choices) cbx.Items.Add(ch[1]);
                    int idx = Array.FindIndex(s.Choices, ch => ch[0] == v);
                    if (idx >= 0) cbx.SelectedIndex = idx; else if (s.Editable) cbx.Text = v;
                    else { cbx.Items.Add(v + " (custom)"); cbx.SelectedIndex = cbx.Items.Count - 1; }
                    EventHandler upd = delegate {
                        int i = cbx.SelectedIndex;
                        if (i >= 0 && i < s.Choices.Length) Changed(s.Key, s.Choices[i][0]);
                        else if (s.Editable) { int x; if (int.TryParse(cbx.Text.Trim(), out x) && x >= 0) Changed(s.Key, x.ToString()); }
                    };
                    cbx.SelectedIndexChanged += upd;
                    if (s.Editable) cbx.TextChanged += upd;
                    c = cbx;
                    break;
                }
                case Kind.Key: {
                    var b = new Button { Text = Keys2.Display(v), Width = S(200), Height = S(28) };
                    b.Click += delegate {
                        using (var d = new KeyDialog(s.Label)) {
                            if (d.ShowDialog(this) == DialogResult.OK && d.Result != null) { Changed(s.Key, d.Result); b.Text = Keys2.Display(d.Result); }
                        }
                    };
                    c = b;
                    break;
                }
                default: {
                    var tb = new TextBox { Text = v, Width = S(370) };
                    tb.TextChanged += delegate { Changed(s.Key, tb.Text.Trim()); };
                    c = tb;
                    break;
                }
            }
            if (s.Help != "") { tips.SetToolTip(label, s.Help); tips.SetToolTip(c, s.Help); }
            t.Controls.Add(label);
            t.Controls.Add(c);
            if (s.Help != "") {
                var h = new Label { Text = s.Help, AutoSize = true, ForeColor = Color.DimGray, MaximumSize = new Size(S(690), 0), Margin = new Padding(S(3), 0, S(3), S(8)) };
                t.Controls.Add(h);
                t.SetColumnSpan(h, 2);
            }
        }

        void AddClipInfo(TableLayoutPanel t)
        {
            var sz = Screen.PrimaryScreen.Bounds.Size;
            int frames; int.TryParse(values["retro_frames"], out frames);
            double mb = frames * (sz.Width / 2.0) * (sz.Height / 2.0) * 4 / (1024 * 1024);
            var info = new Label { Text = string.Format("At {0}x{1}: about {2:0} MB of video memory for {3} frames.", sz.Width, sz.Height, mb, frames),
                                   AutoSize = true, Margin = new Padding(S(3), S(10), S(3), S(3)) };
            var open = new Button { Text = "Open the clips folder", Width = S(200), Height = S(30) };
            open.Click += delegate {
                string d = game.ClipsDir;
                if (Directory.Exists(d)) Process.Start("explorer.exe", "\"" + d + "\"");
                else MessageBox.Show(this, "No clips yet. They are saved in:\n" + d, Text);
            };
            t.Controls.Add(info);
            t.SetColumnSpan(info, 2);
            t.Controls.Add(open);
        }
    }

    static class Program
    {
        [DllImport("user32")] static extern bool SetProcessDPIAware();
        [DllImport("kernel32")] static extern bool AttachConsole(int pid);

        // Command line (for support and scripts):  --game meze|rain  --set key=value ...  --get key
        // Changes the same file the window would; prints what it did. Exit code 0 = done, 1 = error.
        static int Cli(string[] args)
        {
            AttachConsole(-1);
            string name = null;
            var sets = new List<string>();
            var gets = new List<string>();
            for (int i = 0; i < args.Length; i++) {
                if (args[i] == "--game" && i + 1 < args.Length) name = args[++i].ToLowerInvariant();
                else if (args[i] == "--set" && i + 1 < args.Length) sets.Add(args[++i]);
                else if (args[i] == "--get" && i + 1 < args.Length) gets.Add(args[++i]);
                else { Console.WriteLine("unknown argument: " + args[i]); return 1; }
            }
            var games = Game.Load(Game.FindBase());
            var g = name == null ? games.FirstOrDefault() : games.FirstOrDefault(x => x.Name == name);
            if (g == null) { Console.WriteLine("no such game set up: " + (name ?? "(any)")); return 1; }
            string path = g.IniPath;
            if (!File.Exists(path) && File.Exists(g.DefaultIni)) { Directory.CreateDirectory(Path.GetDirectoryName(path)); File.Copy(g.DefaultIni, path); }
            var ini = new IniFile(path);
            foreach (string k in gets) {
                var s = Schema.All().FirstOrDefault(x => x.Key == k);
                string v = ini.Get(k);
                Console.WriteLine(k + "=" + (v ?? (s != null ? s.Default + " (default)" : "(not set)")));
            }
            foreach (string kv in sets) {
                int eq = kv.IndexOf('=');
                if (eq <= 0) { Console.WriteLine("expected key=value: " + kv); return 1; }
                string k = kv.Substring(0, eq).Trim(), v = kv.Substring(eq + 1).Trim();
                ini.Set(k, v);
                Console.WriteLine("set " + k + "=" + v);
            }
            if (sets.Count > 0) ini.Save();
            Console.WriteLine(path);
            return 0;
        }

        [STAThread]
        static int Main(string[] args)
        {
            if (args.Length > 0) return Cli(args);
            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
            return 0;
        }
    }
}
