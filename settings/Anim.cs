// The MHFZ Plus window's animations: easing curves (the usual set, as in CSS and Robert Penner's equations) and values
// that glide to a target over time. Drawing reads the values every frame; the window repaints while one still moves.
// Small on purpose: GDI+ drawing needs no more, and it runs the same on every Windows version and under Wine (the
// Windows Animation Manager, the one built into Windows, is missing in Wine).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;

namespace MhfzPlus
{
    static class Ease
    {
        public static float Linear(float t) { return t; }
        public static float OutCubic(float t) { t = 1 - t; return 1 - t * t * t; }
        public static float InOutCubic(float t) { return t < 0.5f ? 4 * t * t * t : 1 - (float)Math.Pow(2 - 2 * t, 3) / 2; }
        // a little past the target and back: things that pop in
        public static float OutBack(float t) { const float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }

        public static float Clamp(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
        public static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        public static Color Mix(Color a, Color b, float t)
        {
            t = Clamp(t);
            return Color.FromArgb((int)Lerp(a.A, b.A, t), (int)Lerp(a.R, b.R, t), (int)Lerp(a.G, b.G, t), (int)Lerp(a.B, b.B, t));
        }
        public static Color Fade(Color c, float t) { return Color.FromArgb((int)(c.A * Clamp(t)), c.R, c.G, c.B); }
        public static RectangleF Lerp(RectangleF a, RectangleF b, float t)
        {
            return new RectangleF(Lerp(a.X, b.X, t), Lerp(a.Y, b.Y, t), Lerp(a.Width, b.Width, t), Lerp(a.Height, b.Height, t));
        }
        // 0 -> 1 -> 0 once per period (breathing glows, the cursor's nudge)
        public static float Wave(double ms, double period) { return (float)(0.5 - 0.5 * Math.Cos(ms / period * 2 * Math.PI)); }
    }

    // one value gliding from where it is to a target
    class Tween
    {
        static readonly Stopwatch clock = Stopwatch.StartNew();
        public static double FakeNow = -1;   // (screenshots of a moment in an animation)
        public static double Now { get { return FakeNow >= 0 ? FakeNow : clock.Elapsed.TotalMilliseconds; } }
        public static bool Off;              // animations off: every value is at its target at once

        float from, to;
        double start, dur;
        Func<float, float> ease = Ease.OutCubic;

        public Tween(float v) { from = to = v; }
        public float Target { get { return to; } }
        public float Progress { get { return Off || dur <= 0 ? 1 : Ease.Clamp((float)((Now - start) / dur)); } }
        public float Value { get { float p = Progress; return p >= 1 ? to : Ease.Lerp(from, to, ease(p)); } }
        public bool Moving { get { return Progress < 1; } }

        // from where it is now (a change of mind halfway glides on smoothly)
        public void To(float target, double ms, Func<float, float> e = null)
        {
            if (target == to) return;
            from = Value; to = target; start = Now; dur = ms; ease = e ?? Ease.OutCubic;
        }
        // from a given start, after a delay (the delay holds the start value)
        public void Run(float a, float b, double ms, Func<float, float> e = null, double delay = 0)
        {
            from = a; to = b; start = Now + delay; dur = ms; ease = e ?? Ease.OutCubic;
        }
        public void Snap(float v) { from = to = v; dur = 0; }
    }

    // fades by name (hover highlights): Get(id) glides toward 1 while id is on, back to 0 after
    class Fades
    {
        readonly Dictionary<string, Tween> map = new Dictionary<string, Tween>();
        public float Get(string id)
        {
            Tween t;
            return id != null && map.TryGetValue(id, out t) ? t.Value : 0;
        }
        public void Set(string id, bool on, double ms)
        {
            if (id == null) return;
            Tween t;
            if (!map.TryGetValue(id, out t)) { if (!on) return; map[id] = t = new Tween(0); }
            t.To(on ? 1 : 0, ms, Ease.Linear);
        }
        public List<string> Moving()
        {
            var list = new List<string>();
            var done = new List<string>();
            foreach (var kv in map) {
                if (kv.Value.Moving) list.Add(kv.Key);
                else if (kv.Value.Target == 0) done.Add(kv.Key);
            }
            foreach (string id in done) map.Remove(id);
            return list;
        }
    }
}
