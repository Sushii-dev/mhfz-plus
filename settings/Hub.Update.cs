// MHFZ Plus window (see Hub.cs): the update in one press. The newest release's zip comes from this project's release
// page only, its SHA-256 has to match the checksum GitHub lists for it, and the setup in it updates this install
// keeping every choice (Install.ps1 -Update), then opens MHFZ Plus again. Nothing runs in the background by itself:
// the player presses "Update now". On Linux the Linux tool does the same outside Wine (mhfz-plus update).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Threading;
using System.Web.Script.Serialization;

namespace MhfzPlus
{
    partial class HubForm
    {
        bool updating;

        // GitHub's answer for the newest release -> its tag, and the zip to download with its checksum. Throws with a
        // plain reason when there is nothing (safe) to download.
        public class Release { public string Tag, Url, Sha256; public long Size; }
        public static Release ReadRelease(string json, string zipPrefix)
        {
            var rel = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
            object v;
            string tag = rel.TryGetValue("tag_name", out v) && v is string ? ((string)v).TrimStart('v') : null;
            if (string.IsNullOrEmpty(tag)) throw new Exception("GitHub's answer has no version in it.");
            string want = zipPrefix + tag + ".zip";
            if (!rel.TryGetValue("assets", out v) || !(v is System.Collections.IEnumerable)) throw new Exception("The release has no files.");
            foreach (var a in (System.Collections.IEnumerable)v) {
                var d = a as Dictionary<string, object>;
                if (d == null || !d.TryGetValue("name", out v) || (v as string) != want) continue;
                string url = d.TryGetValue("browser_download_url", out v) ? v as string : null;
                string digest = d.TryGetValue("digest", out v) ? v as string : null;
                if (url == null || !url.StartsWith(Repo + "/releases/download/", StringComparison.Ordinal) || url.IndexOfAny(new[] { ' ', '"', '\r', '\n' }) >= 0)
                    throw new Exception("The download isn't on this project's release page, so it isn't used.");
                if (digest == null || !digest.StartsWith("sha256:", StringComparison.Ordinal) || digest.Length != 7 + 64)
                    throw new Exception("GitHub lists no checksum for the download, so it couldn't be checked: update by hand.");
                long size = d.TryGetValue("size", out v) && v != null ? Convert.ToInt64(v) : 0;
                return new Release { Tag = tag, Url = url, Sha256 = digest.Substring(7).ToLowerInvariant(), Size = size };
            }
            throw new Exception("The release has no " + want + ".");
        }
        public static bool SameSha256(string file, string sha256)
        {
            using (var sha = SHA256.Create())
            using (var f = File.OpenRead(file))
                return BitConverter.ToString(sha.ComputeHash(f)).Replace("-", "").ToLowerInvariant() == sha256;
        }

        // (the folder without its last \ : before the closing quote it would escape it)
        public static string UpdateArgs(string setup, string target)
        {
            return "-NoProfile -ExecutionPolicy Bypass -File \"" + setup.TrimEnd('\\') + "\" -Unattended -Update -Relaunch -Target \"" + target.TrimEnd('\\') + "\"";
        }
        void OfferUpdate()
        {
            var t = ShowToast("MHFZ Plus " + latest + " is out", "You have " + version + ". Update now: it downloads, installs and opens again in about a minute, keeping your settings.", false);
            t.Buttons.Add(new KeyValuePair<string, Action>("Update now", UpdateNow));
            t.Buttons.Add(new KeyValuePair<string, Action>("What's new", () => OpenUrl(latestUrl)));
        }
        void UpdateNow()
        {
            if (updating) return;
            if ((session != null && !session.Done) || Process.GetProcessesByName("MHFZ-Plus").Length > 0) {
                ShowToast("Close the game first", "MHFZ Plus updates while no game runs through it. Close the game and its launcher, then press Update again.", true);
                return;
            }
            if (wine) {   // (Linux: the Linux tool downloads, checks and installs outside Wine, then opens MHFZ Plus again)
                if (RunLinuxTool("update --wait --relaunch")) {
                    IniPut("general", "updated_from", version);
                    ShowToast("Updating MHFZ Plus...", "MHFZ Plus closes for the update and opens again when it's done.", false);
                    var t = new System.Windows.Forms.Timer { Interval = 1500 };
                    t.Tick += delegate { t.Stop(); Close(); };
                    t.Start();
                } else ShowToast("Update on Linux", "Run  mhfz-plus update  in a terminal (~/.local/share/mhfz-plus/mhfz-plus), or download the new version and run sh install.sh.", false);
                return;
            }
            updating = true;
            var busy = new Toast { Title = "Updating MHFZ Plus...", Text = "Asking GitHub for the newest version.", Busy = true };
            toast = busy;
            Invalidate();
            new Thread(() => {
                string setup = null, err = null;
                Action<string> say = s => { try { BeginInvoke((Action)(() => { busy.Text = s; Invalidate(); })); } catch { } };
                try { setup = DownloadUpdate(say); } catch (Exception ex) { err = ex.Message; }
                try {
                    BeginInvoke((Action)(() => {
                        updating = false;
                        if (err != null) {
                            var t = ShowToast("Couldn't update", err + " MHFZ Plus is as it was.", true);
                            t.Buttons.Add(new KeyValuePair<string, Action>("Download page", () => OpenUrl(Repo + "/releases/latest")));
                            return;
                        }
                        // setup takes it from here: it waits for this window to close, updates, and opens MHFZ Plus again
                        try {
                            Process.Start(new ProcessStartInfo(PowerShell, UpdateArgs(setup, baseDir))
                                          { UseShellExecute = false, CreateNoWindow = true });
                            IniPut("general", "updated_from", version);
                            Close();
                        } catch (Exception ex) { ShowToast("Couldn't start the update", ex.Message, true); }
                    }));
                } catch { }
            }) { IsBackground = true }.Start();
        }

        // the newest release into %TEMP%\MHFZ-Plus-update, checked and unpacked: its setup\Install.ps1
        string DownloadUpdate(Action<string> say)
        {
            ServicePointManager.SecurityProtocol |= (SecurityProtocolType)3072;   // TLS 1.2
            var req = (HttpWebRequest)WebRequest.Create("https://api.github.com/repos/Sushii-dev/mhfz-plus/releases/latest");
            req.UserAgent = "MHFZ-Plus/" + version;
            req.Timeout = 15000;
            string json;
            using (var resp = req.GetResponse())
            using (var rd = new StreamReader(resp.GetResponseStream())) json = rd.ReadToEnd();
            var rel = ReadRelease(json, "MHFZ-Plus-");
            if (!Newer(rel.Tag)) throw new Exception("MHFZ Plus " + version + " is already the newest version.");
            string dir = Path.Combine(Path.GetTempPath(), "MHFZ-Plus-update");
            if (Directory.Exists(dir)) Directory.Delete(dir, true);
            Directory.CreateDirectory(dir);
            string zip = Path.Combine(dir, "MHFZ-Plus-" + rel.Tag + ".zip");
            var dl = (HttpWebRequest)WebRequest.Create(rel.Url);
            dl.UserAgent = req.UserAgent;
            dl.Timeout = 30000;
            dl.ReadWriteTimeout = 30000;
            using (var resp = dl.GetResponse())
            using (var s = resp.GetResponseStream())
            using (var f = File.Create(zip)) {
                var buf = new byte[1 << 16];
                long got = 0, total = rel.Size > 0 ? rel.Size : resp.ContentLength;
                int n;
                DateTime said = DateTime.MinValue;
                while ((n = s.Read(buf, 0, buf.Length)) > 0) {
                    f.Write(buf, 0, n);
                    got += n;
                    if ((DateTime.Now - said).TotalMilliseconds > 150) {
                        said = DateTime.Now;
                        say("Downloading MHFZ Plus " + rel.Tag + ": " + (total > 0 ? (got * 100 / total) + "% of " + (total / 1048576.0).ToString("0.0") + " MB" : (got / 1048576.0).ToString("0.0") + " MB"));
                    }
                }
            }
            say("Checking the download...");
            if (!SameSha256(zip, rel.Sha256)) throw new Exception("The download doesn't match GitHub's checksum (damaged on the way?), so nothing was changed.");
            say("Unpacking...");
            string pkg = Path.Combine(dir, "pkg");
            ZipFile.ExtractToDirectory(zip, pkg);   // (entries can't land outside pkg: .NET refuses those)
            string setup = Directory.GetFiles(pkg, "Install.ps1", SearchOption.AllDirectories)
                                    .FirstOrDefault(p => string.Equals(Path.GetFileName(Path.GetDirectoryName(p)), "setup", StringComparison.OrdinalIgnoreCase));
            if (setup == null) throw new Exception("The download has no setup in it.");
            say("Installing: MHFZ Plus closes now and opens again in a moment.");
            return setup;
        }

        // after an update: what's new, once
        void SayUpdated()
        {
            string from = IniGet("general", "updated_from", "");
            if (from == "") return;
            IniPut("general", "updated_from", "");
            try { Directory.Delete(Path.Combine(Path.GetTempPath(), "MHFZ-Plus-update"), true); } catch { }   // (the download, done with)
            if (from == version) { ShowToast("The update didn't go through", "MHFZ Plus is still " + version + ". Help > Check for updates tries again; the message setup showed says why.", true); return; }
            var t = ShowToast("Updated to " + version, "MHFZ Plus " + from + " is now " + version + ". Your settings stayed as they were.", false);
            string page = ReleasePage(Repo + "/releases/tag/v" + version);
            t.Buttons.Add(new KeyValuePair<string, Action>("What's new", () => OpenUrl(page)));
        }
    }
}
