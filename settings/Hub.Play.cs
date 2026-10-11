// MHFZ Plus window (see Hub.cs): The Play page: the servers (MHFZ-Plus.ini), a session followed from the launcher's
// log, the table, START and the session panel.
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
    partial class HubForm
    {
        // ---------------------------------------------------------------- data: servers
        void LoadServers()
        {
            servers.Clear();
            int refresh = 60;
            try { var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE)); if (EnumDisplaySettings(null, -1, ref dm) && dm.dmDisplayFrequency > 1) refresh = dm.dmDisplayFrequency; } catch { }
            var gs = Game.Load(baseDir);
            foreach (var def in new[] { new[] { "rain", "Rain" }, new[] { "meze", "MezeLounge" } }) {
                string dir = IniGet(def[0], "game", "");
                var s = new Srv { Id = def[0], Title = def[1], Dir = dir, Ready = dir != "" && Directory.Exists(dir) };
                if (dir == "") continue;
                string eng = IniGet(def[0], "english", "1");
                int files = 0;
                try { files = File.ReadAllLines(Path.Combine(baseDir, "english", def[0], "manifest.txt")).Count(l => l.Trim() != "" && !l.StartsWith("#")); } catch { }
                s.English = files == 0 ? "not included" : eng == "0" ? "off" : eng.ToLowerInvariant() == "safe" ? files + " (safe)" : files + " files";
                var g = gs.FirstOrDefault(x => x.Name == def[0]);
                if (IniGet(def[0], "fps", "1") == "0" || g == null) { s.Fps = "off"; s.Backend = "-"; }
                else {
                    var fi = new IniFile(File.Exists(g.IniPath) ? g.IniPath : g.DefaultIni);
                    string on = fi.Get("enabled") ?? "1", target = fi.Get("target_fps") ?? "0", be = fi.Get("d3d9_backend") ?? "";
                    int t; int.TryParse(target, out t);
                    s.Fps = on == "0" ? "off" : "up to " + (t > 0 ? t : (int)Math.Max(60, Math.Round(refresh * 0.97)));
                    s.Backend = be.Trim() == "" ? "Direct3D 9" : be.EndsWith(@"dxvk\d3d9.dll", StringComparison.OrdinalIgnoreCase) ? "DXVK" : "custom";
                }
                string log = Path.Combine(baseDir, "MHFZ-Plus-" + def[0] + ".log");
                s.Last = File.Exists(log) ? Ago(File.GetLastWriteTime(log)) : "never";
                if (!s.Ready) { s.Desc = "The game folder is gone: " + dir + ". Run the MHFZ Plus setup again to find it."; }
                else if (def[0] == "rain") {
                    s.Desc = "Opens the Rain launcher. Log in and press Play there: the English files and the frame-rate mod go in the moment its file check passes, and come out again when the game closes.";
                    s.Note = "Rain's staff haven't approved mods yet: use it at your own risk.";
                } else s.Desc = "Opens MezeLounge. Log in as usual: MHFZ Plus puts the English files back right after the launcher restores the Japanese ones.";
                servers.Add(s);
            }
        }
        static string Ago(DateTime t)
        {
            var d = DateTime.Now - t;
            if (d.TotalMinutes < 2) return "just now";
            if (d.TotalHours < 1) return (int)d.TotalMinutes + " min ago";
            if (t.Date == DateTime.Today) return (int)d.TotalHours + " h ago";
            if (t.Date == DateTime.Today.AddDays(-1)) return "yesterday";
            if (d.TotalDays < 30) return (int)d.TotalDays + " days ago";
            return t.ToString("yyyy-MM-dd");
        }

        // ---------------------------------------------------------------- sessions
        void Start()
        {
            if (session != null && !session.Done) return;
            if (servers.Count == 0 || !servers[sel].Ready) return;
            var s = servers[sel];
            string exe = Path.Combine(baseDir, "MHFZ-Plus.exe");
            if (!File.Exists(exe)) { ShowToast("Can't start", "MHFZ-Plus.exe is missing from " + baseDir + ". Run the MHFZ Plus setup again.", true); return; }
            if (Process.GetProcessesByName("MHFZ-Plus").Length > 0) { ShowToast("Already running", "MHFZ Plus is already running a game. Close that game first.", true); return; }
            IniPut("general", "server", s.Id);
            string log = Path.Combine(baseDir, "MHFZ-Plus-" + s.Id + ".log");
            try {
                var p = Process.Start(new ProcessStartInfo(exe, s.Id) { WorkingDirectory = baseDir, UseShellExecute = false });
                session = new Session { Server = s.Id, Proc = p, Log = log, Title = "Starting " + s.Title + "...", Text = "Putting the original game files in place for the launcher." };
            } catch (Exception ex) { ShowToast("Can't start", ex.Message, true); return; }
            toast = null;
            if (IniGet("general", "close_on_start", "0") == "1") {
                var t = new System.Windows.Forms.Timer { Interval = 1500 };
                t.Tick += delegate { t.Stop(); Close(); };
                t.Start();
            }
            Invalidate();
        }
        // a session started elsewhere (Steam's Play entry, the command line): follow it too
        void AttachRunningSession()
        {
            try {
                var p = Process.GetProcessesByName("MHFZ-Plus").FirstOrDefault();
                if (p == null) return;
                string newest = null; DateTime t = DateTime.MinValue;
                foreach (var s in new[] { "rain", "meze" }) {
                    string log = Path.Combine(baseDir, "MHFZ-Plus-" + s + ".log");
                    if (File.Exists(log) && File.GetLastWriteTime(log) > t) { t = File.GetLastWriteTime(log); newest = s; }
                }
                if (newest == null) return;
                session = new Session { Server = newest, Proc = p, Log = Path.Combine(baseDir, "MHFZ-Plus-" + newest + ".log"), External = true,
                                        Head = "RUNNING", Title = "A game is running through MHFZ Plus.", Text = "Started outside this window." };
            } catch { }
        }
        void FollowSession()
        {
            var s = session;
            if (s == null || s.Done) return;
            string title = s.Server == "rain" ? "Rain" : "MezeLounge";
            try {
                if (File.Exists(s.Log)) {
                    using (var f = new FileStream(s.Log, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete)) {
                        if (f.Length < s.Pos) s.Pos = 0;
                        f.Seek(s.Pos, SeekOrigin.Begin);
                        var buf = new byte[f.Length - s.Pos];
                        int n = f.Read(buf, 0, buf.Length);
                        s.Pos += n;
                        foreach (string raw in Encoding.UTF8.GetString(buf, 0, n).Split('\n')) {
                            string l = raw.Trim().TrimStart('﻿');
                            if (l.Length > 14 && l[2] == ':' && l[5] == ':') l = l.Substring(13).Trim();   // (the time stamp)
                            if (l.StartsWith("originals in place, started")) {
                                s.Head = "LAUNCHER OPEN";
                                s.Title = s.Server == "rain" ? "Rain's launcher is open: log in and press Play there." : title + " is starting: log in as usual.";
                                s.Text = s.Server == "rain" ? "The English files and the frame-rate mod go in the moment its file check passes." : "MHFZ Plus puts the English files back right after the launcher restores the Japanese ones.";
                            } else if (l.StartsWith("started ")) {
                                s.Head = "LAUNCHER OPEN"; s.Title = title + " is starting: log in as usual.";
                                s.Text = "MHFZ Plus puts the English files back right after the launcher restores the Japanese ones.";
                            } else if (l.StartsWith("launcher check passed")) {
                                s.Head = "CHECK PASSED"; s.Title = "Rain's file check passed.";
                                s.Text = "English and the frame-rate mod are in. The game is starting.";
                            } else if (l == "game started") {
                                s.Head = "IN GAME"; s.Title = "Good hunting!";
                                s.Text = "MHFZ Plus works in the background. In game: Insert shows the FPS counter, Pause turns the mod off and on, Delete saves a clip.";
                            } else if (l.StartsWith("game crashed")) {
                                // (the launcher saw the game end with an exception's exit code)
                                s.Head = "GAME CRASHED"; s.Problem = true; s.Crashed = true; s.Title = "The game crashed.";
                                s.Text = "MHFZ Plus puts the original files back. A bug report (Help) collects what happened, so it can be fixed.";
                                {
                                    var t = ShowToast("The game crashed", "Sorry about that. If it happens again, or right after a change, a bug report helps: it collects the logs (the frame-rate mod notes where a crash happened if it was involved).", true);
                                    t.Buttons.Add(new KeyValuePair<string, Action>("Make a bug report", MakeReport));
                                    t.Buttons.Add(new KeyValuePair<string, Action>("Not now", () => { toast = null; }));
                                }
                            } else if (l.StartsWith("game closed")) {
                                if (s.Crashed) s.Text = "The original files are back. A bug report (Help) collects what happened, so it can be fixed.";
                                else {
                                    s.Head = "GAME CLOSED"; s.Title = "The game closed: the original files are back.";
                                    s.Text = s.Server == "rain" ? "Waiting for Rain's launcher to close." : "Everything is ready for next time.";
                                }
                            } else if (l.StartsWith("LAUNCHER CHECK FAILED")) {
                                s.Head = "CHECK FAILED"; s.Problem = true; s.Title = "Rain's launcher found a file it doesn't accept.";
                                s.Text = l.Substring(l.IndexOf(':') + 1).Trim() + ". Close the launcher, then Help > Put the original files back.";
                            } else if (l.StartsWith("english:") && l.Contains("left out") && !l.Contains(", 0 left out")) {
                                s.Text = "Some English files don't match your game version and stay Japanese.";
                            } else if ((l.StartsWith("fps: could not copy d3d9.dll") || l == @"fps: fps\d3d9.dll missing") && !s.ModMissing) {
                                // (the launcher couldn't put the frame-rate mod in: only its log knew, the game just ran at 30)
                                s.ModMissing = true;
                                ShowToast("No frame-rate mod this time", "MHFZ Plus couldn't put d3d9.dll into the game folder (an antivirus may be " +
                                          "holding or removing it), so this game runs at its own 30 fps. Allow MHFZ Plus's folder in your antivirus, " +
                                          "or run the MHFZ Plus setup again.", true);
                            }
                        }
                    }
                }
            } catch { }
            if (s.Proc != null && s.Proc.HasExited) {
                s.Done = true;
                s.DoneAt = DateTime.Now;
                int code = 0;
                try { code = s.Proc.ExitCode; } catch { }
                if (code != 0 && s.Head == "STARTING") {
                    s.Problem = true; s.Head = "DIDN'T START"; s.Title = "MHFZ Plus couldn't start " + title + ".";
                    s.Text = "See the message it showed, or MHFZ-Plus-" + s.Server + ".log in the MHFZ Plus folder.";
                } else if (!s.Problem) {
                    s.Head = "DONE"; s.Title = "All done: the original files are back.";
                    s.Text = "Everything is ready for next time.";
                }
                LoadServers();
            }
        }

        // ---------------------------------------------------------------- Play
        static readonly float[] cols = { 0, 150, 300, 450, 560 };
        void DrawPlay(Graphics g, RectangleF p)
        {
            float x = p.X + 26, y = p.Y + 16;
            if (servers.Count == 0) {
                using (var b = new SolidBrush(Ui.Ink)) g.DrawString("No game is set up yet.\n\nRun the MHFZ Plus setup (Install.cmd) and choose your MezeLounge and/or Rain folder.", fBody, b, new RectangleF(x, y + 20, p.Width - 52, 160));
                return;
            }
            var heads = new[] { "Server", "English", "Frame rate", "Backend", "Last played" };
            float tx = x + 18;   // (the columns, after the cursor's place)
            using (var b = new SolidBrush(Ui.Navy)) for (int i = 0; i < heads.Length; i++) g.DrawString(heads[i], fHead, b, tx + cols[i], y);
            Dots(g, x, p.Right - 26, y + 24);
            y += 34;
            float y0 = y;
            float? srvHover = null;
            {   // the selected row's highlight and red tag, sliding to it
                if (selPos.Target != sel) selPos.Snap(sel);   // (selected without Select: no slide)
                float sy = y + selPos.Value * 34;
                using (var b = new SolidBrush(Ui.RowOn)) g.FillRectangle(b, x - 8, sy, p.Width - 36, 32);
                var st = g.Save();
                g.ResetClip();   // (the tag sticks out of the dialog)
                g.SetClip(new RectangleF(p.X - 40, p.Y, p.Width + 40, p.Height * unroll.Value));
                var tag = new RectangleF(p.X - 30, sy + 6, 34, 20);
                using (var b = new SolidBrush(Ui.Red)) g.FillRectangle(b, tag);
                using (var pen = new Pen(Ui.RedDark, 2)) g.DrawRectangle(pen, tag.X, tag.Y, tag.Width, tag.Height);
                var house = new[] { new PointF(tag.X + 17, tag.Y + 4), new PointF(tag.X + 23, tag.Y + 9), new PointF(tag.X + 23, tag.Y + 16), new PointF(tag.X + 11, tag.Y + 16), new PointF(tag.X + 11, tag.Y + 9) };
                g.FillPolygon(Brushes.White, house);
                g.Restore(st);
            }
            for (int i = 0; i < servers.Count; i++) {
                var s = servers[i];
                var row = new RectangleF(x - 8, y, p.Width - 36, 32);
                string id = "srv" + i;
                if (i != sel && Hv(id) > 0) using (var b = new SolidBrush(Ease.Fade(Ui.RowHover, Hv(id)))) g.FillRectangle(b, row);
                if (hover == id) srvHover = y - y0 + 10;
                Color val = s.Ready ? Ui.Green : Ui.Muted;
                using (var b = new SolidBrush(val)) {
                    g.DrawString(s.Title, fBold, b, tx + cols[0], y + 7);
                    g.DrawString(s.English, fBody, b, tx + cols[1], y + 7);
                    g.DrawString(s.Fps, fBody, b, tx + cols[2], y + 7);
                    g.DrawString(s.Backend, fBody, b, tx + cols[3], y + 7);
                }
                using (var b = new SolidBrush(Ui.Muted)) g.DrawString(s.Last, fBody, b, tx + cols[4], y + 7);
                int idx = i;
                Hot(row, id, () => ChooseServer(idx));
                y += 34;
            }
            ListCursor(g, "srv", x, y0, srvHover, sel * 34 + 10);
            using (var pen = new Pen(Color.FromArgb(110, Ui.Ink))) g.DrawLine(pen, x, y + 4, p.Right - 26, y + 4);
            y += 14;
            var cur = servers[Math.Min(sel, servers.Count - 1)];
            float h = 0;
            using (var b = new SolidBrush(Ui.Ink)) {
                var sz = g.MeasureString(cur.Desc, fSmall, (int)(p.Width - 52));
                g.DrawString(cur.Desc, fSmall, b, new RectangleF(x, y, p.Width - 52, sz.Height + 4));
                h = sz.Height;
            }
            if (!string.IsNullOrEmpty(cur.Note)) using (var b = new SolidBrush(Ui.Warn)) g.DrawString(cur.Note, fSmallBold, b, x, y + h + 6);
        }

        void DrawStart(Graphics g)
        {
            bool ok = servers.Count > 0 && servers[sel].Ready;
            const string word = "START";
            float tile = 48, size = 78, top = 500;
            float total = tile * word.Length;
            float x0 = (W - total) / 2f + 14;
            var r = new RectangleF(x0 - 34, top - 4, total + 40, size + 8);
            float lit = ok ? Hv("start") : 0;
            bool down = ok && hover == "start" && press == "start";
            // a glint runs over the tiles every few seconds, as on the game's title screen
            float ph = (float)(Tween.Now % 3800 / 1000.0 * 7.5) - 1.5f;
            for (int i = 0; i < word.Length; i++) {
                float pop = startPop[i].Value;
                if (pop <= 0.01f) continue;
                float glint = ok && !Tween.Off ? Math.Max(0, 1 - Math.Abs(ph - i) / 1.3f) : 0;
                DrawTile(g, x0 + i * tile + 10, top + size / 2f - 3 * lit + (down ? 2 : 0) - 14 * (1 - Math.Min(1, pop)), size * pop, word[i], Math.Max(lit, glint * 0.9f), pop * 1.6f);
            }
            if (ok) { Hot(r, "start", Start); glintAt = r; }
            string hint = ok ? "Start " + servers[sel].Title : servers.Count == 0 ? "Run the setup first" : "Game folder not found";
            var hs = g.MeasureString(hint, fSmall);
            var hr = new RectangleF(W / 2f - hs.Width / 2f - 10, top + size + 12, hs.Width + 20, 22);
            using (var b = new SolidBrush(Color.FromArgb(180, 28, 30, 26))) g.FillRectangle(b, hr);
            Outlined(g, hint, fSmall, Ui.HudText, hr.X + 10, hr.Y + 3);
        }

        void DrawSession(Graphics g)
        {
            var s = session;
            if (s != sessSeen) { sessSeen = s; headSeen = s.Head; sessOpen.Run(0, 1, 280, Ease.OutCubic); Kick(); }
            if (s.Head != headSeen) { headSeen = s.Head; sessFlash.Run(1, 0, 900, Ease.OutCubic); Kick(); }   // (news: the header lights up)
            var r = new RectangleF(250, 500, 620, 106);
            var open = Unfold(r, sessOpen.Value);
            HudPanel(g, open);
            var st = g.Save();
            g.SetClip(open, CombineMode.Intersect);
            if (sessFlash.Value > 0) using (var b = new SolidBrush(Color.FromArgb((int)(80 * sessFlash.Value), Ui.HudYellow))) g.FillRectangle(b, r.X + 1, r.Y + 1, r.Width - 1, 25);
            bool live = !s.Done && !s.Problem;
            Gem(g, r.X + 16, r.Y + 13, s.Problem ? 2 : 0, 6, live ? Glow : 0);
            if (live) idle.Add(new RectangleF(r.X + 4, r.Y + 1, 24, 24));
            Outlined(g, s.Head, fHead, Ui.HudYellow, r.X + 28, r.Y + 4);
            string who = s.Server == "rain" ? "Rain" : "MezeLounge";
            var ws = g.MeasureString(who, fSmall);
            Outlined(g, who, fSmall, Ui.HudYellow, r.Right - ws.Width - 10, r.Y + 5);
            using (var p = new Pen(Ui.HudInner)) g.DrawLine(p, r.X + 1, r.Y + 26, r.Right - 1, r.Y + 26);
            Outlined(g, s.Title ?? "", fBig, s.Problem ? Ui.HudRed : Color.White, r.X + 14, r.Y + 36);
            DrawWrappedOutlined(g, s.Text ?? "", fSmall, Ui.HudMuted, new RectangleF(r.X + 14, r.Y + 60, r.Width - 28, 44));
            g.Restore(st);
        }
    }
}
