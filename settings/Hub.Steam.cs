// MHFZ Plus window (see Hub.cs): START through Steam. A game started outside Steam gets no Steam Input, and a
// controller can act up in it (a player's menus scrolled by themselves until they started from Steam). So START asks
// Steam to run MHFZ Plus's own Steam entry for that server - the one setup's "Add to Steam" made for this install -
// and follows the session as usual once Steam has started it. Settings > MHFZ Plus > Start through Steam: with a
// controller plugged in (default), always or never. Windows only: on Linux the Steam entries start from Steam itself.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace MhfzPlus
{
    partial class HubForm
    {
        public class SteamEntry
        {
            public string Steam; public uint AppId;
            public ulong GameId { get { return ((ulong)AppId << 32) | 0x02000000; } }   // (a non-Steam game's id in steam:// links)
        }

        static string SteamRoot()
        {
            string env = Environment.GetEnvironmentVariable("MHFZPLUS_STEAM");   // (testing: another Steam folder)
            if (!string.IsNullOrEmpty(env)) return env;
            try {
                using (var k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")) {
                    string p = k == null ? null : k.GetValue("SteamPath") as string;
                    if (!string.IsNullOrEmpty(p) && Directory.Exists(p)) return Path.GetFullPath(p);
                }
            } catch { }
            return null;
        }
        static string ActiveSteamUser()   // (the account id Steam is logged in with: its userdata folder, first)
        {
            try {
                using (var k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam\ActiveProcess")) {
                    object v = k == null ? null : k.GetValue("ActiveUser");
                    if (v is int && (int)v != 0) return ((uint)(int)v).ToString();
                }
            } catch { }
            return null;
        }

        // MHFZ Plus's Steam entry that starts this install's MHFZ-Plus.exe for the server, or null (no Steam, no
        // such entry, or a shortcuts.vdf this can't read)
        public static SteamEntry FindSteamEntry(string baseDir, string server)
        {
            try {
                string root = SteamRoot();
                if (root == null) return null;
                string steam = Path.Combine(root, "steam.exe");
                if (!File.Exists(steam) || !Directory.Exists(Path.Combine(root, "userdata"))) return null;
                string want = Path.GetFullPath(Path.Combine(baseDir, "MHFZ-Plus.exe"));
                string active = ActiveSteamUser();
                var files = Directory.GetDirectories(Path.Combine(root, "userdata"))
                    .Select(d => Path.Combine(d, @"config\shortcuts.vdf")).Where(File.Exists)
                    .OrderByDescending(f => Path.GetFileName(Path.GetDirectoryName(Path.GetDirectoryName(f))) == active)
                    .ThenByDescending(f => File.GetLastWriteTimeUtc(f));
                foreach (string f in files) {
                    var vdf = ReadVdf(File.ReadAllBytes(f));
                    object sc;
                    if (vdf == null || !vdf.TryGetValue("shortcuts", out sc) || !(sc is Dictionary<string, object>)) continue;
                    foreach (var e in ((Dictionary<string, object>)sc).Values.OfType<Dictionary<string, object>>()) {
                        object exe, opts, id;
                        if (!e.TryGetValue("exe", out exe) || !e.TryGetValue("LaunchOptions", out opts) || !e.TryGetValue("appid", out id) || !(id is int)) continue;
                        if (!string.Equals(((opts as string) ?? "").Trim(), server, StringComparison.OrdinalIgnoreCase)) continue;
                        string path = ((exe as string) ?? "").Trim().Trim('"');
                        try { if (!string.Equals(Path.GetFullPath(path), want, StringComparison.OrdinalIgnoreCase)) continue; } catch { continue; }
                        return new SteamEntry { Steam = steam, AppId = (uint)(int)id };
                    }
                }
            } catch { }
            return null;
        }

        // binary VDF (Steam's shortcuts.vdf): maps, strings, 32/64-bit numbers; null for anything else
        public static Dictionary<string, object> ReadVdf(byte[] b)
        {
            int p = 0;
            try { return VdfMap(b, ref p, 0); } catch { return null; }
        }
        static Dictionary<string, object> VdfMap(byte[] b, ref int p, int depth)
        {
            if (depth > 16) throw new InvalidDataException();
            var m = new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
            while (true) {
                byte t = b[p++];
                if (t == 8) return m;
                string k = VdfString(b, ref p);
                switch (t) {
                    case 0: m[k] = VdfMap(b, ref p, depth + 1); break;
                    case 1: m[k] = VdfString(b, ref p); break;
                    case 2: m[k] = BitConverter.ToInt32(b, p); p += 4; break;
                    case 7: m[k] = BitConverter.ToUInt64(b, p); p += 8; break;
                    default: throw new InvalidDataException("VDF type " + t);
                }
            }
        }
        static string VdfString(byte[] b, ref int p)
        {
            int e = Array.IndexOf(b, (byte)0, p);
            if (e < 0) throw new InvalidDataException();
            string s = Encoding.UTF8.GetString(b, p, e - p);
            p = e + 1;
            return s;
        }

        // a game controller plugged in (any the old joystick API sees, or an Xbox-style one)
        [StructLayout(LayoutKind.Sequential)]
        struct JOYINFOEX { public int dwSize, dwFlags, dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos, dwButtons, dwButtonNumber, dwPOV, dwReserved1, dwReserved2; }
        [StructLayout(LayoutKind.Sequential)]
        struct XINPUT_STATE { public uint dwPacketNumber; public ushort wButtons; public byte bLeftTrigger, bRightTrigger; public short sThumbLX, sThumbLY, sThumbRX, sThumbRY; }
        [DllImport("winmm")] static extern int joyGetNumDevs();
        [DllImport("winmm")] static extern int joyGetPosEx(int id, ref JOYINFOEX ji);
        [DllImport("xinput1_4")] static extern int XInputGetState(int user, ref XINPUT_STATE st);
        public static bool ControllerConnected()
        {
            try {
                for (int i = 0, n = Math.Min(joyGetNumDevs(), 16); i < n; i++) {
                    var ji = new JOYINFOEX { dwSize = Marshal.SizeOf(typeof(JOYINFOEX)), dwFlags = 0xFF };   // (JOY_RETURNALL)
                    if (joyGetPosEx(i, ref ji) == 0) return true;
                }
            } catch { }
            try {
                for (int i = 0; i < 4; i++) { var st = new XINPUT_STATE(); if (XInputGetState(i, ref st) == 0) return true; }
            } catch { }
            return false;
        }

        // START for this server: through Steam? (not when this window was itself started by Steam: its games then
        // run under Steam already)
        SteamEntry SteamStartFor(string server)
        {
            if (wine || !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("SteamGameId"))) return null;
            string mode = IniGet("general", "start_steam", "auto");
            if (mode == "0" || (mode != "1" && !ControllerConnected())) return null;
            return FindSteamEntry(baseDir, server);
        }

        // a session asked of Steam: it's ours once MHFZ-Plus.exe runs and has started a new log
        bool SteamStarted(Session s, string title)
        {
            if (DateTime.Now < s.NextLook) return false;
            s.NextLook = DateTime.Now.AddMilliseconds(500);
            try {
                var p = Process.GetProcessesByName("MHFZ-Plus").FirstOrDefault();
                if (p != null && File.Exists(s.Log) && File.GetLastWriteTime(s.Log) >= s.Asked.AddSeconds(-2)) {
                    s.Proc = p; s.Pos = 0;
                    s.Text = "Started through Steam, so Steam's controller support works.";
                    return true;
                }
            } catch { }
            if ((DateTime.Now - s.Asked).TotalSeconds > 120) {
                s.Done = true; s.DoneAt = DateTime.Now; s.Problem = true;
                s.Head = "DIDN'T START"; s.Title = "Steam didn't start " + title + ".";
                s.Text = "Steam may be waiting for something (a login, an update). Start it from Steam, or without Steam.";
                string server = s.Server;
                var t = ShowToast("Steam didn't start the game", "MHFZ Plus asked Steam to start " + title + " through its Steam entry (for Steam's controller support), but nothing started within two minutes.", true);
                t.Buttons.Add(new KeyValuePair<string, Action>("Start without Steam", () => { session = null; StartDirect(server); }));
                t.Buttons.Add(new KeyValuePair<string, Action>("Not now", () => { toast = null; }));
            }
            return false;
        }
    }
}
