// MHFZ Plus window (see Hub.cs): The Settings page: the frame-rate mod's settings (each game's mhfz-fps.ini) and MHFZ
// Plus's own, saved as they change.
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
        // ---------------------------------------------------------------- data: settings
        void LoadGames()
        {
            games = Game.Load(baseDir);
            gameIdx = Math.Max(0, Math.Min(gameIdx, games.Count - 1));
            LoadValues();
        }
        void LoadValues()
        {
            vals.Clear();
            ini = null;
            if (games.Count == 0) return;
            var g = games[gameIdx];
            string path = g.IniPath;
            try {
                if (!File.Exists(path) && File.Exists(g.DefaultIni)) { Directory.CreateDirectory(Path.GetDirectoryName(path)); File.Copy(g.DefaultIni, path); }
            } catch { }
            ini = new IniFile(path);
            foreach (var s in Schema.All()) {
                string v = ini.Get(s.Key);
                vals[s.Key] = v == null ? s.Default : s.Kind == Kind.Key ? Keys2.Normalize(v) : v;
            }
            if (string.Equals(vals["d3d9_backend"], g.BundledDxvk, StringComparison.OrdinalIgnoreCase)) vals["d3d9_backend"] = "dxvk";
        }
        void Change(Setting s, string v)
        {
            vals[s.Key] = v;
            saveAt = DateTime.Now.AddMilliseconds(350);
            Invalidate();
        }
        void SaveNow()
        {
            saveAt = DateTime.MaxValue;
            if (games.Count == 0) return;
            var g = games[gameIdx];
            string path = g.IniPath;   // (Rain: where the game reads it now - it moves while the game runs)
            try {
                if (!File.Exists(path) && File.Exists(g.DefaultIni)) { Directory.CreateDirectory(Path.GetDirectoryName(path)); File.Copy(g.DefaultIni, path); }
                var cur = new IniFile(path);
                foreach (var s in Schema.All()) {
                    string v = vals[s.Key];
                    if (s.Key == "d3d9_backend" && v == "dxvk") v = g.BundledDxvk;
                    string old = cur.Get(s.Key);
                    if (old == null ? v == s.Default : (s.Kind == Kind.Key ? Keys2.Normalize(old) == v : old == v)) continue;
                    cur.Set(s.Key, v);
                }
                cur.Save();
                ini = cur;
                saveError = null;
                savedAt = DateTime.Now;
            } catch (Exception ex) { saveError = "Could not save: " + ex.Message; }
            LoadServers();
            Invalidate();
        }
        List<string> Warnings()
        {
            var msg = new List<string>();
            if (vals.Count == 0) return msg;
            var keys = Schema.All().Where(s => s.Kind == Kind.Key && vals[s.Key] != "none").ToList();
            var dup = keys.GroupBy(s => vals[s.Key]).Where(x => x.Count() > 1).Select(x => Keys2.Display(x.Key)).ToList();
            var gk = keys.Where(s => Keys2.GameKey(vals[s.Key])).Select(s => Keys2.Display(vals[s.Key])).ToList();
            if (dup.Count > 0) msg.Add("The same key is set twice: " + string.Join(", ", dup) + ".");
            if (gk.Count > 0) msg.Add("The game uses " + string.Join(", ", gk) + " itself: pressing it does both.");
            if (vals["retro"] == "0" && vals["key_capture"] != "none") msg.Add("The clip recorder is off: the clip key does nothing.");
            if (vals["d3d9_backend"] == "dxvk" && games.Count > 0 && !File.Exists(games[gameIdx].BundledDxvk)) msg.Add("DXVK isn't installed: run the MHFZ Plus setup to add it.");
            return msg;
        }
        // MHFZ Plus's own options (MHFZ-Plus.ini), shown as the first settings page
        List<AppRow> AppRows()
        {
            var rows = new List<AppRow>();
            foreach (var s in servers) {
                if (s.English == "not included") continue;
                string id = s.Id;
                rows.Add(new AppRow { Label = "English (" + s.Title + ")", Help = "Items, menus, quests and town dialogue in English. Tested parts only: menus, items, quests and town dialogue, if anything else gives trouble.",
                    Options = new[] { new[] { "1", "ON" }, new[] { "0", "OFF" }, new[] { "safe", "Tested parts only" } },
                    Get = () => IniGet(id, "english", "1").ToLowerInvariant(), Set = v => { IniPut(id, "english", v); LoadServers(); } });
            }
            rows.Add(new AppRow { Label = "Close this window when a game starts", Help = "MHFZ Plus keeps working in the background either way.",
                Options = new[] { new[] { "0", "OFF" }, new[] { "1", "ON" } }, Get = () => IniGet("general", "close_on_start", "0"), Set = v => IniPut("general", "close_on_start", v) });
            rows.Add(new AppRow { Label = "Check for updates", Help = "When this window opens, at most once a day: asks GitHub for the newest version number. Nothing else is sent.",
                Options = new[] { new[] { "1", "ON" }, new[] { "0", "OFF" } }, Get = () => IniGet("general", "update_check", "1"), Set = v => IniPut("general", "update_check", v) });
            rows.Add(new AppRow { Label = "Animations", Help = "Menus that move like the game's. Off: everything appears at once." +
                    (!winAnims ? " Windows' own animation effects are off, so they stay off here too." : wine ? " Off by default on Linux: Wine draws this window slowly." : ""),
                Options = new[] { new[] { "1", "ON" }, new[] { "0", "OFF" } }, Get = () => IniGet("general", "animations", AnimDefault),
                Set = v => { IniPut("general", "animations", v); animSetting = v != "0"; Tween.Off = !(winAnims && animSetting); Kick(); } });
            return rows;
        }
        List<string> Categories()
        {
            var list = new List<string> { "MHFZ Plus" };
            foreach (var s in Schema.All()) if (!list.Contains(s.Group) && (advanced || !s.Advanced)) list.Add(s.Group);
            return list;
        }

        // ---------------------------------------------------------------- Settings
        void DrawSettings(Graphics g, RectangleF p)
        {
            if (games.Count == 0 && servers.Count == 0) {
                using (var b = new SolidBrush(Ui.Ink)) g.DrawString("No game is set up yet: run the MHFZ Plus setup first.", fBody, b, p.X + 26, p.Y + 30);
                return;
            }
            // the categories, with the cursor on the open one
            float cx = p.X + 18, cy = p.Y + 16;
            float cy0 = cy;
            float? catHover = null, catOpen = null;
            foreach (string c in Categories()) {
                var r = new RectangleF(cx, cy, 168, 30);
                string id = "cat" + c;
                if (c == cat) { using (var b = new SolidBrush(Ui.RowOn)) g.FillRectangle(b, r); catOpen = cy - cy0 + 9; }
                else if (Hv(id) > 0) using (var b = new SolidBrush(Ease.Fade(Ui.RowHover, Hv(id)))) g.FillRectangle(b, r);
                if (hover == id) catHover = cy - cy0 + 9;
                using (var b = new SolidBrush(c == cat ? Ui.Navy : Ui.Ink)) g.DrawString(c, fBold, b, r.X + 24, r.Y + 6);
                string cc = c;
                Hot(r, id, () => OpenCat(cc));
                cy += 32;
            }
            ListCursor(g, "cat", cx + 6, cy0, catHover, catOpen);
            // advanced settings on/off, at the foot of the list
            var ar = new RectangleF(cx, p.Bottom - 46, 168, 30);
            using (var pen = new Pen(Ui.ParchBorder)) g.DrawRectangle(pen, ar.X + 6, ar.Y + 8, 13, 13);
            if (advanced) using (var b = new SolidBrush(Ui.Green)) g.FillRectangle(b, ar.X + 9, ar.Y + 11, 8, 8);
            using (var b = new SolidBrush(Ease.Mix(Ui.Ink, Ui.Navy, Hv("adv")))) g.DrawString("Advanced", fBold, b, ar.X + 26, ar.Y + 6);
            Hot(ar, "adv", () => {
                advanced = !advanced;
                OpenCat(Categories().Contains(cat) ? cat : "General");
            });
            using (var pen = new Pen(Ui.Ink) { DashPattern = new float[] { 1.5f, 2.5f } }) g.DrawLine(pen, p.X + 196, p.Y + 14, p.X + 196, p.Bottom - 14);

            // the rows of the open category
            var area = new RectangleF(p.X + 214, p.Y + 10, p.Width - 238, p.Height - 62);
            if (games.Count > 1 && cat != "MHFZ Plus") {   // whose settings (each game has its own file)
                var gr = new RectangleF(area.X, area.Y, area.Width, 30);
                using (var b = new SolidBrush(Ui.Muted)) g.DrawString("Settings for", fSmall, b, gr.X, gr.Y + 7);
                Selector(g, new RectangleF(gr.Right - 222, gr.Y + 1, 222, 28), games[gameIdx].Title, "game", d => { SaveNow(); gameIdx = (gameIdx + games.Count + d) % games.Count; LoadValues(); Invalidate(); }, null);
                area = new RectangleF(area.X, area.Y + 36, area.Width, area.Height - 36);
            }
            var state = g.Save();
            g.SetClip(area, CombineMode.Intersect);
            float y = area.Y - scroll;
            float textW = area.Width - 250;
            if (cat == "MHFZ Plus") {
                foreach (var row in AppRows()) {
                    float rh = RowText(g, row.Label, row.Help, area.X, y, textW);
                    string v = row.Get();
                    int i = Math.Max(0, Array.FindIndex(row.Options, o => o[0] == v));
                    var rr = row;
                    Selector(g, new RectangleF(area.Right - 230, y + rh / 2f - 14, 222, 28), row.Options[i][1], "app" + row.Label,
                             d => { rr.Set(rr.Options[(i + rr.Options.Length + d) % rr.Options.Length][0]); Invalidate(); }, null);
                    y += rh + 18;
                    DottedRow(g, area.X, area.Right, y - 9);
                }
            } else if (vals.Count > 0) {
                foreach (var s in Schema.All().Where(x => x.Group == cat && (advanced || !x.Advanced))) {
                    float rh = RowText(g, s.Label, s.Help, area.X, y, textW);
                    var sr = new RectangleF(area.Right - 230, y + rh / 2f - 14, 222, 28);
                    if (sr.Bottom > area.Top && sr.Top < area.Bottom) Control(g, s, sr);
                    y += rh + 18;
                    DottedRow(g, area.X, area.Right, y - 9);
                }
            }
            scrollMax = Math.Max(0, y + scroll - area.Y - area.Height + 8);
            g.Restore(state);
            if (scrollMax > 0) {   // a thin scroll bar
                float th = area.Height * area.Height / (area.Height + scrollMax);
                float ty = area.Y + (area.Height - th) * (scroll / scrollMax);
                using (var b = new SolidBrush(Color.FromArgb(70, Ui.Ink))) g.FillRectangle(b, area.Right + 8, area.Y, 3, area.Height);
                using (var b = new SolidBrush(Ui.ParchBorder)) g.FillRectangle(b, area.Right + 7, ty, 5, th);
            }
            // the foot: what saving does, warnings, and two buttons
            float fy = p.Bottom - 44;
            using (var pen = new Pen(Color.FromArgb(110, Ui.Ink))) g.DrawLine(pen, p.X + 214, fy - 6, p.Right - 18, fy - 6);
            var warn = Warnings();
            string status = saveError ?? (warn.Count > 0 ? warn[0] : StatusLine());
            bool fresh = saveError == null && warn.Count == 0 && (DateTime.Now - savedAt).TotalSeconds < 2.5;
            using (var b = new SolidBrush(saveError != null || warn.Count > 0 ? Ui.Warn : fresh ? Ui.Green : Ui.Muted))
                g.DrawString(fresh ? "Saved. " + status : status, fSmall, b, new RectangleF(p.X + 214, fy + 4, p.Width - 214 - 300, 36));
            if (cat != "MHFZ Plus" && games.Count > 0) {
                TextButton(g, new RectangleF(p.Right - 290, fy + 2, 160, 28), "Recommended", "reco", () => {
                    var t = ShowToast("Recommended settings?", "Every setting of the frame-rate mod (keys too) back to the recommended value. The graphics backend stays as you chose.", false);
                    t.Until = DateTime.MaxValue;
                    t.Buttons.Add(new KeyValuePair<string, Action>("Set them", () => {
                        foreach (var s in Schema.All()) if (s.Key != "d3d9_backend") vals[s.Key] = s.Default;
                        SaveNow(); toast = null;
                    }));
                    t.Buttons.Add(new KeyValuePair<string, Action>("Cancel", () => { toast = null; }));
                }, true);
                TextButton(g, new RectangleF(p.Right - 122, fy + 2, 104, 28), "The file", "file", () => {
                    SaveNow();
                    try { Process.Start("notepad.exe", "\"" + games[gameIdx].IniPath + "\""); } catch { }
                }, true);
            }
        }
        string StatusLine()
        {
            if (cat == "MHFZ Plus") return "MHFZ Plus's own options. They apply at once.";
            if (games.Count == 0) return "";
            var g = games[gameIdx];
            if (g.Live) return "The game is running: changes apply in the game at once.";
            return g.Mode == "swap" ? "Changes apply the next time you start the game." : "Changes are saved as you make them and apply at the next start, or at once while the game runs.";
        }
        float RowText(Graphics g, string label, string help, float x, float y, float w)
        {
            using (var b = new SolidBrush(Ui.Navy)) g.DrawString(label, fBold, b, new RectangleF(x, y, w, 40));
            float lh = g.MeasureString(label, fBold, (int)w).Height;
            if (string.IsNullOrEmpty(help)) return Math.Max(lh, 28);
            var hs = g.MeasureString(help, fSmall, (int)w);
            using (var b = new SolidBrush(Ui.Muted)) g.DrawString(help, fSmall, b, new RectangleF(x, y + lh + 2, w, hs.Height + 2));
            return Math.Max(lh + 2 + hs.Height, 28);
        }
        void DottedRow(Graphics g, float x1, float x2, float y)
        {
            using (var b = new SolidBrush(Color.FromArgb(70, Ui.Ink))) for (float x = x1; x < x2; x += 6) g.FillRectangle(b, x, y, 2, 2);
        }
        // a setting's control: ◀ value ▶ for on/off, choices and numbers; a key box for keys
        void Control(Graphics g, Setting s, RectangleF r)
        {
            string v = vals[s.Key];
            switch (s.Kind) {
                case Kind.Bool: {
                    bool on = v.Trim() != "0" && v.Trim() != "";
                    Selector(g, r, on ? "ON" : "OFF", "set" + s.Key, d => Change(s, on ? "0" : "1"), () => Change(s, on ? "0" : "1"));
                    break;
                }
                case Kind.Choice: {
                    int i = Array.FindIndex(s.Choices, c => c[0] == v);
                    string shown = i >= 0 ? ShortChoice(s.Choices[i][1]) : (s.Editable ? v : v + " (custom)");
                    Selector(g, r, shown, "set" + s.Key, d => {
                        int j = i < 0 ? 0 : (i + s.Choices.Length + d) % s.Choices.Length;
                        Change(s, s.Choices[j][0]);
                    }, s.Editable ? (Action)(() => BeginEdit(s, r)) : null);
                    break;
                }
                case Kind.Int: {
                    int n; int.TryParse(v, out n);
                    int step = Step(s);
                    Selector(g, r, n.ToString(), "set" + s.Key, d => Change(s, Math.Max(s.Min, Math.Min(s.Max, (n / step) * step + d * step)).ToString()), () => BeginEdit(s, r));
                    break;
                }
                case Kind.Key: {
                    var kr = new RectangleF(r.X + 8, r.Y, 120, r.Height);
                    using (var b = new SolidBrush(Color.FromArgb(44, 46, 40))) g.FillRectangle(b, kr);
                    using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, kr.X, kr.Y, kr.Width, kr.Height);
                    string name = Keys2.Display(v);
                    var ns = g.MeasureString(name, fSmall);
                    Outlined(g, name, fSmall, Ui.HudText, kr.X + kr.Width / 2f - ns.Width / 2f, kr.Y + 6);
                    TextButton(g, new RectangleF(kr.Right + 8, r.Y, r.Right - kr.Right - 8, r.Height), "Change", "key" + s.Key, () => { capture = s; Invalidate(); }, true);
                    break;
                }
                default: {
                    using (var b = new SolidBrush(Ui.Green)) g.DrawString(v, fBody, b, r.X + 30, r.Y + 6);
                    break;
                }
            }
        }
        static string ShortChoice(string label)
        {
            int i = label.IndexOf(" (");
            string s = i > 0 && !label.StartsWith("Automatic") ? label.Substring(0, i) : label;
            return s.Replace("Automatic (your monitor's refresh rate)", "Automatic");
        }
        static int Step(Setting s)
        {
            int range = s.Max - s.Min;
            foreach (int st in new[] { 1, 2, 5, 10, 25, 50, 100, 250, 500 }) if (range / st <= 60) return st;
            return 1000;
        }
        void Selector(Graphics g, RectangleF r, string value, string id, Action<int> step, Action clickValue)
        {
            var left = new RectangleF(r.X, r.Y, 26, r.Height);
            var right = new RectangleF(r.Right - 26, r.Y, 26, r.Height);
            var mid = new RectangleF(r.X + 28, r.Y, r.Width - 56, r.Height);
            using (var b = new SolidBrush(Ease.Mix(Ui.ParchLight, Color.White, Hv(id + "=")))) g.FillRectangle(b, mid);
            using (var p = new Pen(Ui.ParchBorder)) g.DrawRectangle(p, mid.X, mid.Y, mid.Width, mid.Height);
            var vs = g.MeasureString(value, fBold);
            string shown = value;
            while (vs.Width > mid.Width - 8 && shown.Length > 4) { shown = shown.Substring(0, shown.Length - 2); vs = g.MeasureString(shown + "…", fBold); }
            if (shown != value) shown += "…";
            using (var b = new SolidBrush(Ui.Green)) g.DrawString(shown, fBold, b, mid.X + mid.Width / 2f - vs.Width / 2f, mid.Y + 6);
            SideArrow(g, left, true, Hv(id + "<"));
            SideArrow(g, right, false, Hv(id + ">"));
            Hot(left, id + "<", () => step(-1));
            Hot(right, id + ">", () => step(1));
            Hot(mid, id + "=", clickValue ?? (() => step(1)));
        }
        void TextButton(Graphics g, RectangleF r, string text, string id, Action click, bool parchment = false)
        {
            float hov = Hv(id);
            bool down = hover == id && press == id;
            if (parchment) {
                using (var b = new SolidBrush(Ease.Mix(Ui.ParchLight, Color.White, hov))) g.FillRectangle(b, r);
                using (var p = new Pen(Ui.ParchBorder, 2)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
                using (var p = new Pen(Ui.ParchInner)) g.DrawRectangle(p, r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6);
                var ps = g.MeasureString(text, fSmallBold);
                using (var b = new SolidBrush(Ease.Mix(Ui.Navy, Ui.Green, hov))) g.DrawString(text, fSmallBold, b, r.X + r.Width / 2f - ps.Width / 2f, r.Y + r.Height / 2f - ps.Height / 2f + (down ? 1 : 0));
                Hot(r, id, click);
                return;
            }
            using (var b = new SolidBrush(Ease.Mix(Color.FromArgb(28, 30, 26), Color.FromArgb(48, 51, 44), hov))) g.FillRectangle(b, r);
            using (var p = new Pen(Ui.HudLine)) g.DrawRectangle(p, r.X, r.Y, r.Width, r.Height);
            var ts = g.MeasureString(text, fSmallBold);
            Outlined(g, text, fSmallBold, Ease.Mix(Ui.HudText, Ui.HudYellow, hov), r.X + r.Width / 2f - ts.Width / 2f, r.Y + r.Height / 2f - ts.Height / 2f + (down ? 1 : 0));
            Hot(r, id, click);
        }

        // typing a number (or a custom frame rate) into a small text box on top of the selector
        void BeginEdit(Setting s, RectangleF r)
        {
            EndEdit(false);
            editing = s;
            edit = new TextBox {
                Text = vals[s.Key], BorderStyle = BorderStyle.FixedSingle, TextAlign = HorizontalAlignment.Center,
                Font = new Font(mono, 14f * k, FontStyle.Bold, GraphicsUnit.Pixel), BackColor = Color.White, ForeColor = Ui.Green,
                Bounds = new Rectangle((int)((r.X + 28) * k), (int)(r.Y * k), (int)((r.Width - 56) * k), (int)(r.Height * k))
            };
            edit.KeyDown += (o, e) => {
                if (e.KeyCode == Keys.Enter) { EndEdit(true); e.SuppressKeyPress = true; }
                else if (e.KeyCode == Keys.Escape) { EndEdit(false); e.SuppressKeyPress = true; }
            };
            edit.LostFocus += delegate { EndEdit(true); };
            Controls.Add(edit);
            edit.Focus();
            edit.SelectAll();
        }
        void EndEdit(bool commit)
        {
            if (edit == null) return;
            var e = edit; var s = editing;
            edit = null; editing = null;
            if (commit) {
                int n;
                if (int.TryParse(e.Text.Trim(), out n)) {
                    if (s.Kind == Kind.Int) n = Math.Max(s.Min, Math.Min(s.Max, n));
                    if (n >= 0) Change(s, n.ToString());
                }
            }
            Controls.Remove(e);
            e.Dispose();
            Invalidate();
        }

        void DrawCapture(Graphics g)
        {
            if (capture != capSeen) { capSeen = capture; capOpen.Run(0, 1, 220, Ease.OutCubic); Kick(); }
            using (var b = new SolidBrush(Color.FromArgb((int)(110 * Ease.Clamp(capOpen.Value)), 10, 10, 8))) g.FillRectangle(b, 0, 30, W, H - 30);
            var r = new RectangleF(W / 2f - 280, H / 2f - 70, 560, 140);
            var open = Unfold(r, capOpen.Value);
            HudPanel(g, open, true);
            var st = g.Save();
            g.SetClip(open, CombineMode.Intersect);
            Gem(g, r.X + 16, r.Y + 13, 0, 6, 1);
            Outlined(g, "SET A KEY", fHead, Ui.HudYellow, r.X + 28, r.Y + 4);
            using (var p = new Pen(Ui.HudInner)) g.DrawLine(p, r.X + 1, r.Y + 26, r.Right - 1, r.Y + 26);
            Outlined(g, "Press the key or combination for", fSmall, Ui.HudMuted, r.X + 18, r.Y + 40);
            Outlined(g, "\"" + capture.Label + "\"", fBig, Color.White, r.X + 18, r.Y + 60);
            Outlined(g, "A mouse side button works too.  Esc: cancel   Backspace: no key", fSmall, Ui.HudMuted, r.X + 18, r.Y + 104);
            g.Restore(st);
        }
    }
}
