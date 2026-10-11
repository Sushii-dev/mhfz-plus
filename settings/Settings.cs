// MHFZ Plus app (MHFZ-Plus-App.exe): the settings model and the command line; the window is in Hub.cs.
//
// Every setting the mod reads is described once in Schema() below - its key, kind, default, range and help text - and
// the Settings page is built from that list, so a new setting in the mod needs one line here. The file is edited in
// place: comments and settings this program doesn't know stay as they are.
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
using System.Threading;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("MHFZ Plus")]
[assembly: System.Reflection.AssemblyProduct("MHFZ Plus")]
[assembly: System.Reflection.AssemblyVersion("1.4.3.0")]

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
            // ("dxvk" stands for the copy that comes with MHFZ Plus: the ini gets its full path; another path shows as custom)
            Choice("d3d9_backend", "", "Graphics backend", "DXVK runs the game's Direct3D 9 on Vulkan. It needs much less CPU time per frame, so " +
                   "more in-between frames fit: in our tests 50-70% more in towns and crowds. Needs a graphics card with Vulkan 1.3 (most from " +
                   "2016 on); if it can't start there, Windows' Direct3D is used and the game says so. Applies at the next game start.", false,
                   "dxvk", "DXVK (Vulkan): faster", "", "Windows Direct3D 9");
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
            Int("pair_threads", "0", 0, 8, "Matching threads (0 = automatic)", "Automatic scales with your CPU: its threads minus 4 (or cores minus 2, if more), at most 8.", true);
            Bool("pair_async", "1", "Match in the background", "", true);
            Bool("precompute", "1", "Precompute blended constants on all threads", "", true);
            Bool("gpu_queue", "1", "Let the GPU work on one frame while the next is built", "", true);
            Bool("lock_copy", "1", "Copy textures the game rewrites mid-frame", "", true);
            Bool("replay_filter", "1", "Skip constant uploads that change nothing", "", true);
            Bool("flush_endscene", "0", "Run the game frame at every EndScene", "", true);

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
        public string BundledDxvk { get { return System.IO.Path.Combine(Base, "fps", "dxvk", "d3d9.dll"); } }  // (comes with MHFZ Plus 1.3+)
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
            string env = Environment.GetEnvironmentVariable("MHFZPLUS_BASE");   // (testing: another MHFZ Plus folder)
            if (!string.IsNullOrEmpty(env)) return env;
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
                if (k == "d3d9_backend" && v == "dxvk") v = g.BundledDxvk;  // (the copy that comes with MHFZ Plus)
                ini.Set(k, v);
                Console.WriteLine("set " + k + "=" + v);
            }
            if (sets.Count > 0) ini.Save();
            Console.WriteLine(path);
            return 0;
        }

        [DllImport("user32")] static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32")] static extern bool ShowWindow(IntPtr h, int cmd);
        [DllImport("user32")] static extern bool IsIconic(IntPtr h);

        [STAThread]
        static int Main(string[] args)
        {
            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            if (args.Length == 2 && args[0] == "--shots") return HubForm.Shots(args[1]);   // (testing: every page into PNGs)
            if (args.Length > 0) return Cli(args);
            // one MHFZ Plus window: opening it again (a second click on the shortcut, Steam) brings the open one forward
            bool first;
            using (var one = new Mutex(true, @"Local\MHFZ-Plus-App", out first)) {
                if (!first) { ShowOpenWindow(); return 0; }
                // a problem nobody caught: written to MHFZ-Plus-App.log (bug reports include it), said in plain words,
                // and the window closes - not .NET's crash dialog, and not a window that keeps failing to draw
                Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
                Application.ThreadException += (s, e) => Crash(e.Exception);
                AppDomain.CurrentDomain.UnhandledException += (s, e) => Crash(e.ExceptionObject as Exception);
                Application.Run(new HubForm(0));
            }
            return 0;
        }
        static void ShowOpenWindow()
        {
            try {
                var me = Process.GetCurrentProcess();
                foreach (var p in Process.GetProcessesByName(me.ProcessName)) {
                    if (p.Id == me.Id || p.MainWindowHandle == IntPtr.Zero) continue;
                    if (IsIconic(p.MainWindowHandle)) ShowWindow(p.MainWindowHandle, 9);   // SW_RESTORE
                    SetForegroundWindow(p.MainWindowHandle);
                    return;
                }
            } catch { }
        }
        static int crashed;
        public static void LogApp(string what)
        {
            try {
                File.AppendAllText(Path.Combine(Game.FindBase(), "MHFZ-Plus-App.log"),
                                   DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + what + "\r\n", new UTF8Encoding(false));
            } catch { }
        }
        static void Crash(Exception ex)
        {
            if (Interlocked.Increment(ref crashed) > 1) return;   // (one message, however many follow)
            string version = "?";
            try { version = File.ReadAllLines(Path.Combine(Game.FindBase(), "version.txt"))[0].Trim(); } catch { }
            LogApp("MHFZ Plus " + version + " ran into a problem and closed:\r\n" + ex);
            try {
                MessageBox.Show("MHFZ Plus ran into a problem and has to close: " + (ex != null ? ex.Message : "unknown error") +
                                "\n\nA game you started keeps running. Open MHFZ Plus again to carry on.\n\nThe details are in MHFZ-Plus-App.log " +
                                "in the MHFZ Plus folder; Help > Make a bug report includes it.", "MHFZ Plus", MessageBoxButtons.OK, MessageBoxIcon.Error);
            } catch { }
            Environment.Exit(2);
        }
    }
}
