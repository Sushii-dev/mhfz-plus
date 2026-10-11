// MHFZ Plus window (see Hub.cs): The Help page: bug report, the original files back, the folder, updates, read me,
// uninstall.
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
        // ---------------------------------------------------------------- help actions
        void RunHidden(string file, string args, Action<int, string> done)
        {
            var psi = new ProcessStartInfo(file, args) { WorkingDirectory = baseDir, UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
            new Thread(() => {
                int code = -1; string output = "";
                try {
                    using (var p = Process.Start(psi)) {
                        output = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
                        p.WaitForExit();
                        code = p.ExitCode;
                    }
                } catch (Exception ex) { output = ex.Message; }
                try { BeginInvoke((Action)(() => done(code, output))); } catch { }
            }) { IsBackground = true }.Start();
        }
        string PowerShell { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe"); } }
        void MakeReport()
        {
            if (wine) {
                if (!RunLinuxTool("report")) { ShowToast("Bug report on Linux", "Run  mhfz-plus report  in a terminal (~/.local/share/mhfz-plus/mhfz-plus).", false); return; }
                ShowToast("Making the bug report", "A window opens when it's ready, with the zip files on your desktop and a button for the GitHub form.", false);
                return;
            }
            string script = Path.Combine(baseDir, "Report.ps1");
            if (!File.Exists(script)) { ShowToast("Bug report", "Report.ps1 is missing: run the MHFZ Plus setup again.", true); return; }
            toast = new Toast { Title = "Making the bug report...", Text = "Collecting the logs, your settings and your PC's specs.", Busy = true };
            Invalidate();
            RunHidden(PowerShell, "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\" -NoUI", (code, output) => {
                var outLines = output.Split('\n').Select(l => l.Trim()).ToList();
                var zips = outLines.Where(l => l.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) && File.Exists(l)).ToList();
                if (code != 0 || zips.Count == 0) { ShowToast("Bug report failed", output.Trim().Split('\n').LastOrDefault() ?? "", true); return; }
                // the form's link with "Your setup" filled in (Report.ps1's last line)
                string form = outLines.LastOrDefault(l => l.StartsWith(Repo + "/issues/new")) ?? IssueForm;
                // the newest clip comes in parts GitHub takes, named with when it was saved (it may be about something else)
                var clips = zips.Where(z => Path.GetFileName(z).StartsWith("MHFZ-Plus-clip")).ToList();
                string text = "Saved on your desktop (your user name is left out). Next: GitHub opens with your setup filled in, and the report " +
                              "zip shows next to it: write what happened and drag the zip into Files.";
                if (clips.Count > 0) {
                    var m = Regex.Match(Path.GetFileName(clips[0]), @"from-(\d{4}-\d\d-\d\d)-(\d\d)(\d\d)");
                    text += " The newest clip" + (m.Success ? " (saved " + m.Groups[1].Value + " " + m.Groups[2].Value + ":" + m.Groups[3].Value + ")" : "") +
                            (clips.Count > 1 ? " is in " + clips.Count + " zips beside it: drag them in too" : " is in a zip beside it: drag it in too") + ", if it shows this problem.";
                }
                text += " No GitHub account? Send the zips to Sushii on Discord.";
                Action showFiles = () => { try { Process.Start("explorer.exe", "/select,\"" + zips[0] + "\""); } catch { } };
                var t = ShowToast("Bug report ready", text, false);
                t.Buttons.Add(new KeyValuePair<string, Action>("Report it on GitHub", () => { showFiles(); OpenUrl(form); }));
                t.Buttons.Add(new KeyValuePair<string, Action>("Show the files", showFiles));
            });
        }
        void RestoreOriginals()
        {
            if (session != null && !session.Done) { ShowToast("A game is running", "Close the game and its launcher first.", true); return; }
            string exe = Path.Combine(baseDir, "MHFZ-Plus.exe");
            toast = new Toast { Title = "Putting the original files back...", Text = "", Busy = true };
            Invalidate();
            RunHidden(exe, "all --restore", (code, output) => {
                string text;
                try {
                    text = string.Join(" ", File.ReadAllLines(Path.Combine(baseDir, "MHFZ-Plus-restore.log")).Skip(1)
                        .Select(l => l.Length > 14 && l[2] == ':' ? l.Substring(13).Trim() : l.Trim()).Where(l => l != ""));
                } catch { text = ""; }
                if (code == 0) ShowToast("The original files are back", (text != "" ? text + ". " : "") + "Starting a game through MHFZ Plus puts English and the mod back in.", false);
                else ShowToast("Not everything could be put back", text != "" ? text : "Close the game and its launcher, then try again.", true);
                LoadServers();
            });
        }
        void OpenFolder()
        {
            try {
                if (wine) Process.Start("winebrowser", "\"" + baseDir + "\"");
                else Process.Start("explorer.exe", "\"" + baseDir + "\"");
            } catch (Exception ex) { ShowToast("MHFZ Plus folder", baseDir + " (" + ex.Message + ")", true); }
        }
        // only web pages: Process.Start would also run a program or open a file given as the "link"
        void OpenUrl(string url)
        {
            if (url == null || !url.StartsWith("https://", StringComparison.OrdinalIgnoreCase)) { ShowToast("Couldn't open the link", url ?? "", true); return; }
            try { Process.Start(url); } catch { ShowToast("Couldn't open the browser", url, true); }
        }
        // the download page from the update check (GitHub's answer, or the copy kept in MHFZ-Plus.ini): this project's
        // release pages only, never some other address
        static string ReleasePage(string url)
        {
            return url != null && url.StartsWith(Repo + "/releases/", StringComparison.Ordinal) && url.IndexOfAny(new[] { ' ', '"', '\'', '<', '>', '\r', '\n' }) < 0
                ? url : Repo + "/releases/latest";
        }
        void CheckUpdate(bool manual)
        {
            if (checking) return;
            if (!manual) {
                DateTime last;
                if (DateTime.TryParse(IniGet("general", "last_update_check", ""), System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.RoundtripKind, out last) &&
                    (DateTime.UtcNow - last).TotalHours < 24) { latest = IniGet("general", "latest_version", ""); latestUrl = ReleasePage(IniGet("general", "latest_url", "")); return; }
            }
            checking = true;
            if (manual) { toast = new Toast { Title = "Checking for updates...", Text = "", Busy = true }; Invalidate(); }
            new Thread(() => {
                string tag = null, url = null, err = null;
                try {
                    ServicePointManager.SecurityProtocol |= (SecurityProtocolType)3072;   // TLS 1.2
                    var req = (HttpWebRequest)WebRequest.Create("https://api.github.com/repos/Sushii-dev/mhfz-plus/releases/latest");
                    req.UserAgent = "MHFZ-Plus/" + version;
                    req.Timeout = 8000;
                    using (var resp = req.GetResponse())
                    using (var rd = new StreamReader(resp.GetResponseStream())) {
                        string json = rd.ReadToEnd();
                        var m = Regex.Match(json, "\"tag_name\"\\s*:\\s*\"v?([^\"]+)\"");
                        var u = Regex.Match(json, "\"html_url\"\\s*:\\s*\"([^\"]+/releases/tag/[^\"]+)\"");
                        if (m.Success) tag = m.Groups[1].Value;
                        if (u.Success) url = u.Groups[1].Value;
                    }
                } catch (Exception ex) { err = ex.Message; }
                try {
                    BeginInvoke((Action)(() => {
                        checking = false;
                        if (tag != null) {
                            latest = tag; latestUrl = ReleasePage(url);
                            IniPut("general", "last_update_check", DateTime.UtcNow.ToString("o"));
                            IniPut("general", "latest_version", latest);
                            IniPut("general", "latest_url", latestUrl);
                        }
                        if (manual) {
                            if (tag == null) ShowToast("Couldn't check for updates", err ?? "No answer from GitHub.", true);
                            else if (Newer(latest)) { var t = ShowToast("MHFZ Plus " + latest + " is out", "You have " + version + ". Download the new version and run Install.cmd: your settings stay.", false); t.Buttons.Add(new KeyValuePair<string, Action>("Download", () => OpenUrl(latestUrl))); }
                            else ShowToast("Up to date", "MHFZ Plus " + version + " is the newest version.", false);
                        }
                        Invalidate();
                    }));
                } catch { }
            }) { IsBackground = true }.Start();
        }
        bool Newer(string v)
        {
            Version a, b;
            return v != null && Version.TryParse(v, out a) && Version.TryParse(version, out b) && a > b;
        }
        void Uninstall()
        {
            var t = ShowToast("Uninstall MHFZ Plus?", "The original game files are put back first. Then MHFZ Plus, its shortcut and its Steam entries are removed.", false);
            t.Buttons.Add(new KeyValuePair<string, Action>("Uninstall", () => {
                if (wine) {   // (Linux: the Linux tool, once this window and its Wine are gone)
                    if (RunLinuxTool("uninstall --wait")) Close();
                    else ShowToast("Uninstall on Linux", "Run  mhfz-plus uninstall  in a terminal (~/.local/share/mhfz-plus/mhfz-plus).", false);
                    return;
                }
                try {
                    Process.Start(new ProcessStartInfo(PowerShell, "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(baseDir, "Uninstall.ps1") + "\"") { UseShellExecute = false, CreateNoWindow = true });
                    Close();
                } catch (Exception ex) { ShowToast("Couldn't start the uninstaller", ex.Message, true); }
            }));
            t.Buttons.Add(new KeyValuePair<string, Action>("Cancel", () => { toast = null; }));
        }
        // Linux: the MHFZ Plus script outside Wine (Wine runs a Linux program by its Z: path)
        bool RunLinuxTool(string args)
        {
            string tool = IniGet("linux", "tool", "");
            if (tool == "" || !File.Exists(tool)) return false;
            try {
                Process.Start(new ProcessStartInfo("cmd.exe", "/c \"\"" + tool + "\" " + args + "\"") { UseShellExecute = false, CreateNoWindow = true });
                return true;
            } catch { return false; }
        }
        Toast ShowToast(string title, string text, bool error)
        {
            toast = new Toast { Title = title, Text = text, Error = error };
            if (!error) toast.Until = DateTime.Now.AddSeconds(12);
            Invalidate();
            return toast;
        }

        // ---------------------------------------------------------------- Help
        void DrawHelp(Graphics g, RectangleF p)
        {
            float x = p.X + 26, y = p.Y + 16;
            using (var b = new SolidBrush(Ui.Navy)) { g.DrawString("Action", fHead, b, x, y); g.DrawString("What it does", fHead, b, x + 330, y); }
            Dots(g, x, p.Right - 26, y + 24);
            y += 34;
            var actions = new List<Tuple<string, string, Action, bool>> {
                Tuple.Create("Make a bug report", "Logs, settings and your PC's specs in a zip on your desktop, ready for a bug report on GitHub. Your user name is left out.", (Action)MakeReport, false),
                Tuple.Create("Put the original files back", "Takes the English files and the frame-rate mod out of both game folders. Starting a game puts them in again.", (Action)RestoreOriginals, false),
                Tuple.Create("Open the MHFZ Plus folder", "Settings, logs and saved clips.", (Action)OpenFolder, false),
                Tuple.Create("Check for updates", "Asks GitHub for the newest version.", (Action)(() => CheckUpdate(true)), false),
                Tuple.Create("Read me", "Everything about installing, playing, settings and problems.", (Action)(() => { try { Process.Start(Path.Combine(baseDir, "README.txt")); } catch { } }), false),
                Tuple.Create("Uninstall MHFZ Plus", "The original files back first, then MHFZ Plus, its shortcut and its Steam entries are removed.", (Action)Uninstall, true),
            };
            float descW = p.Right - 26 - (x + 330);
            float y0 = y;
            float? actHover = null;
            for (int i = 0; i < actions.Count; i++) {
                var a = actions[i];
                var ds = g.MeasureString(a.Item2, fSmall, (int)descW);
                float rh = Math.Max(30, ds.Height + 10);
                var row = new RectangleF(x - 8, y, p.Width - 36, rh);
                string id = "act" + i;
                if (Hv(id) > 0) using (var b = new SolidBrush(Ease.Fade(Ui.RowHover, Hv(id)))) g.FillRectangle(b, row);
                if (hover == id) actHover = y - y0 + 9;
                using (var b = new SolidBrush(a.Item4 ? Ui.Warn : Ui.Green)) g.DrawString(a.Item1, fBold, b, x + 18, y + 6);
                using (var b = new SolidBrush(Ui.Muted)) g.DrawString(a.Item2, fSmall, b, new RectangleF(x + 330, y + 6, descW, ds.Height + 2));
                Hot(row, id, a.Item3);
                y += rh + 4;
            }
            ListCursor(g, "act", x, y0, actHover, null);   // (nothing is chosen here: the cursor only follows the mouse)
            using (var pen = new Pen(Color.FromArgb(110, Ui.Ink))) g.DrawLine(pen, x, p.Bottom - 40, p.Right - 26, p.Bottom - 40);
            float lx = x;
            using (var b = new SolidBrush(Ui.Muted)) {
                string about = "MHFZ Plus " + version + " · MIT license · Fan project, not affiliated with Capcom or any server.";
                g.DrawString(about, fSmall, b, lx, p.Bottom - 30);
                lx += g.MeasureString(about, fSmall).Width + 10;
            }
            lx = Link(g, "GitHub", lx, p.Bottom - 30, "lgh", () => OpenUrl(Repo)) + 12;
            Link(g, "Tips (Ko-fi)", lx, p.Bottom - 30, "lkofi", () => OpenUrl(Kofi));
        }
        float Link(Graphics g, string text, float x, float y, string id, Action click)
        {
            var s = g.MeasureString(text, fSmallBold);
            var col = Ease.Mix(Ui.Navy, Ui.Green, Hv(id));
            using (var b = new SolidBrush(col)) g.DrawString(text, fSmallBold, b, x, y);
            using (var p = new Pen(col)) g.DrawLine(p, x + 2, y + s.Height - 2, x + s.Width - 3, y + s.Height - 2);
            Hot(new RectangleF(x, y, s.Width, s.Height), id, click);
            return x + s.Width;
        }
    }
}
