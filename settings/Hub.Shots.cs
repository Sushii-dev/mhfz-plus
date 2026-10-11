// MHFZ Plus window (see Hub.cs): Pictures of every page without a window, for tests (toolspptest.ps1).
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
        // ---------------------------------------------------------------- screenshots (testing: MHFZ-Plus-App.exe --shots <folder>)
        public static int Shots(string dir)
        {
            Directory.CreateDirectory(dir);
            var f = new HubForm(1f, true);
            Tween.Off = true;   // (the pages at rest; moments of the animations at the end)
            Action<string> save = name => { using (var b = f.Shot()) b.Save(Path.Combine(dir, name + ".png"), ImageFormat.Png); };
            f.page = Page.Play; save("1-play");
            f.session = new Session { Server = "rain", Head = "LAUNCHER OPEN", Title = "Rain's launcher is open: log in and press Play there.", Text = "The English files and the frame-rate mod go in the moment its file check passes." };
            save("2-play-session");
            f.session = null;
            f.page = Page.Settings; f.LoadGames(); f.layerFor = (Page)(-1);
            foreach (string c in f.Categories()) { f.cat = c; save("3-settings-" + c.Replace(" ", "")); }
            f.advanced = true; f.cat = "Quality"; save("4-settings-advanced-quality");
            f.capture = Schema.All().First(s => s.Kind == Kind.Key); save("5-key-capture"); f.capture = null;
            f.page = Page.Help; f.layerFor = (Page)(-1); save("6-help");
            f.toast = new Toast { Title = "Bug report ready", Text = "Saved on your desktop (your user name is left out). Next: GitHub opens with your setup filled in, and the report zip shows next to it: write what happened and drag the zip into Files. The newest clip (saved 2026-10-09 10:07) is in 5 zips beside it: drag them in too, if it shows this problem. No GitHub account? Send the zips to Sushii on Discord." };
            f.toast.Buttons.Add(new KeyValuePair<string, Action>("Report it on GitHub", null));
            f.toast.Buttons.Add(new KeyValuePair<string, Action>("Show the files", null));
            save("7-help-toast");
            // the lists' cursor under the mouse (it rests on the choice otherwise: the shots above)
            f.toast = null;
            f.SetHover("act2"); save("12-help-hover");
            f.page = Page.Settings; f.cat = "General"; f.SetHover("catKeys"); save("13-settings-hover");
            f.page = Page.Play; f.SetHover("srv" + (1 - f.sel)); save("14-play-hover");
            f.SetHover(null);
            // moments of the animations, on a made-up clock
            Tween.Off = false; Tween.FakeNow = 10000;
            f.toast = null; f.page = Page.Play; f.homeAmt.Snap(1); f.layerFor = (Page)(-1);
            f.StartIntro();
            foreach (int ms in new[] { 200, 420, 600, 800 }) { Tween.FakeNow = 10000 + ms; save("8-intro-" + ms + "ms"); }
            Tween.FakeNow = 20000; f.GoTo(Page.Settings);
            foreach (int ms in new[] { 110, 220 }) { Tween.FakeNow = 20000 + ms; save("9-to-settings-" + ms + "ms"); }
            Tween.FakeNow = 30000; f.GoTo(Page.Play);
            f.ShowToast("Up to date", "MHFZ Plus " + f.version + " is the newest version.", false);
            using (f.Shot()) { }
            Tween.FakeNow = 30090; save("10-to-play-and-message-90ms");
            Tween.FakeNow = 40000; if (f.servers.Count > 1) f.ChooseServer(1 - f.sel, false);   // (not remembered: shots change nothing)
            Tween.FakeNow = 40070; save("11-selection-sliding-70ms");
            Tween.FakeNow = -1;
            // how long drawing takes at 150% (render-ms.txt): a whole frame (page changes, the intro) and one small idle
            // area (the cursor's nudge), the way OnPaint draws them
            Tween.Off = true; f.toast = null; f.k = 1.5f; f.layerFor = (Page)(-1);
            var sb = new StringBuilder();
            foreach (var pg in new[] { Page.Play, Page.Settings, Page.Help }) {
                f.page = pg; f.homeAmt.Snap(pg == Page.Play ? 1 : 0); f.panelH.Snap(pg == Page.Settings ? 486 : 430);
                f.EnsureLayers(false);
                using (var bmp = new Bitmap(f.layer.Width, f.layer.Height, PixelFormat.Format32bppPArgb)) {
                    Func<RectangleF, double> time = clip => {
                        var sw = Stopwatch.StartNew();
                        for (int i = 0; i < 20; i++)
                            using (var g = Graphics.FromImage(bmp)) {
                                if (!clip.IsEmpty) g.SetClip(new RectangleF(clip.X * f.k, clip.Y * f.k, clip.Width * f.k, clip.Height * f.k));
                                g.DrawImageUnscaled(f.layer, 0, 0);
                                g.ScaleTransform(f.k, f.k);
                                f.Render(g, false);
                            }
                        return sw.Elapsed.TotalMilliseconds / 20;
                    };
                    time(RectangleF.Empty);
                    sb.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, "{0}: whole window {1:0.0} ms, cursor area {2:0.0} ms\r\n",
                                    pg, time(RectangleF.Empty), time(f.idle.Count > 0 ? f.idle[0] : new RectangleF(130, 110, 17, 16)));
                }
            }
            File.WriteAllText(Path.Combine(dir, "render-ms.txt"), sb.ToString());
            return 0;
        }
    }
}
