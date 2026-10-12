// The orbit camera's math (opt-in, camera=1): the right stick turns a camera that circles the hunter, with smoothing,
// tilt limits and recentering, and the collision shortens its distance and lets it grow back. No game memory here:
// camera.h reads the game, feeds OrbitStep and writes back what it returns, so this file is tested on its own
// (test\cameratest.cpp). Based on Frontier Orbit by TYCHKA (github.com/Tychkaneg/Frontier-helper), used in MHFZ Plus
// with permission under the MIT license.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Angles in radians. Yaw turns around the vertical axis: the camera looks along (sin yaw, 0, cos yaw). Pitch is
// negative with the camera above the hunter, looking down. The game's own angles are 16-bit: 65536 = a full turn.
static const double kOrbitTau = 6.283185307179586;
static const double kOrbitRad = kOrbitTau / 360;

struct OrbitSettings {
    double yawSpeed = 250, pitchSpeed = 175;  // degrees per second at full stick
    double lookDown = 65, lookUp = 35;        // tilt limits: degrees with the camera above / below the hunter
    double resetTilt = 15;                    // after recentering or a scene change: degrees looking down
    double response = 16;                     // smoothing: how fast the camera catches up with the stick (1/s)
    double deadzone = 0.18;                   // of the stick's travel, round
    double distance = 1.0;                    // x the game's own camera distance
    double height = 0, shoulder = 0;          // where the camera looks: game units above, to the right of the hunter
    bool invertX = false, invertY = false;
};

static inline double OrbitWrap(double a) { return std::atan2(std::sin(a), std::cos(a)); }

// XInput's stick (-32768..32767 per axis) with a round deadzone: -1..1 per axis, 0 inside the deadzone, the full
// value at its edge's far side (a square gate's corner is held to 1 too).
static inline void OrbitStick(short sx, short sy, double deadzone, double& x, double& y) {
    double fx = sx < 0 ? sx / 32768.0 : sx / 32767.0, fy = sy < 0 ? sy / 32768.0 : sy / 32767.0;
    double m = std::sqrt(fx * fx + fy * fy);
    x = y = 0;
    if (m <= deadzone || m <= 0 || deadzone >= 1) return;
    double s = std::min(1.0, (m - deadzone) / (1 - deadzone)) / m;
    x = fx * s;
    y = fy * s;
}

struct Orbit { double yaw = 0, pitch = 0, wantYaw = 0, wantPitch = 0; };

// One step: the stick moves where the camera wants to be (held to the tilt limits), and the camera follows that
// smoothly. Recentering puts it behind the hunter (heading: the hunter's facing as a yaw) at the reset tilt.
static inline void OrbitTurn(Orbit& o, double x, double y, double dt, const OrbitSettings& s, bool recenter, double heading) {
    dt = std::clamp(dt, 0.0, 0.05);  // (a long hitch must not swing the camera around)
    if (s.invertX) x = -x;
    if (s.invertY) y = -y;
    double lo = -s.lookDown * kOrbitRad, hi = s.lookUp * kOrbitRad;
    if (recenter) {
        o.wantYaw = OrbitWrap(heading);
        o.wantPitch = -s.resetTilt * kOrbitRad;
    } else {  // (stick up looks up: the camera sinks below the hunter - the owner's test, 2026-10-12)
        o.wantYaw = OrbitWrap(o.wantYaw - x * s.yawSpeed * kOrbitRad * dt);
        o.wantPitch += y * s.pitchSpeed * kOrbitRad * dt;
    }
    o.wantPitch = std::clamp(o.wantPitch, lo, hi);
    double a = 1 - std::exp(-s.response * dt);
    o.yaw = OrbitWrap(o.yaw + OrbitWrap(o.wantYaw - o.yaw) * a);
    o.pitch += (o.wantPitch - o.pitch) * a;
}

static inline void OrbitEye(const Orbit& o, double radius, const float target[3], float eye[3]) {
    double h = std::cos(o.pitch) * radius;
    eye[0] = (float)(target[0] - std::sin(o.yaw) * h);
    eye[1] = (float)(target[1] - std::sin(o.pitch) * radius);
    eye[2] = (float)(target[2] - std::cos(o.yaw) * h);
}

// ------------------------------------------------------------------------------------------------ collision math
// Where along start->end a hit point lies (0..1).
static inline double OrbitHitFraction(const float start[3], const float end[3], const float hit[3]) {
    double den = 0, num = 0;
    for (int i = 0; i < 3; i++) {
        double d = end[i] - start[i];
        den += d * d;
        num += (hit[i] - start[i]) * d;
    }
    if (den < 1e-12) return 0;
    return std::clamp(num / den, 0.0, 1.0);
}
// How much of start->end stays inside the map's box on the ground plane (x, z); -1: start is outside it.
static inline double OrbitBoxFraction(const float start[3], const float end[3], double xmin, double xmax, double zmin, double zmax) {
    if (start[0] < xmin || start[0] > xmax || start[2] < zmin || start[2] > zmax) return -1;
    double t = 1;
    if (end[0] < xmin) t = std::min(t, (xmin - start[0]) / (end[0] - start[0]));
    if (end[0] > xmax) t = std::min(t, (xmax - start[0]) / (end[0] - start[0]));
    if (end[2] < zmin) t = std::min(t, (zmin - start[2]) / (end[2] - start[2]));
    if (end[2] > zmax) t = std::min(t, (zmax - start[2]) / (end[2] - start[2]));
    return t;
}
// The ground under the segment: samples along it, then halves the first interval that comes closer to the ground than
// the clearance. No flat floor is assumed: where height() knows no ground, nothing limits. Returns the safe fraction.
typedef bool (*OrbitGround)(const float point[3], double* height, void* ctx);
static inline double OrbitGroundFraction(const float start[3], const float end[3], double clearance, int samples, OrbitGround height, void* ctx) {
    double prev = 0;
    for (int step = 0; step <= samples; step++) {
        double t = (double)step / samples, h;
        float p[3];
        for (int i = 0; i < 3; i++) p[i] = (float)(start[i] + (end[i] - start[i]) * t);
        if (height(p, &h, ctx) && p[1] < h + clearance) {
            if (step == 0) return 0;
            double lo = prev, hi = t;
            for (int j = 0; j < 10; j++) {
                double mid = (lo + hi) / 2;
                for (int i = 0; i < 3; i++) p[i] = (float)(start[i] + (end[i] - start[i]) * mid);
                if (height(p, &h, ctx) && p[1] < h + clearance) hi = mid;
                else lo = mid;
            }
            return lo;
        }
        prev = t;
    }
    return 1;
}
// The collision distance closes in at once and grows back smoothly (response: 1/s).
static inline double OrbitRecover(double current, double allowed, double dt, double response) {
    allowed = std::max(0.0, allowed);
    current = std::max(0.0, current);
    if (allowed <= current) return allowed;
    dt = std::clamp(dt, 0.0, 0.05);
    return current + (allowed - current) * (1 - std::exp(-response * dt));
}

// ------------------------------------------------------------------------------------------------ scenes
// A new scene (another hunter object, map or collision data, a teleport, a pause of more than half a second, the camera
// switched on) resyncs the orbit with the game's camera once the scene has been steady for half a second. The
// reference distance and height come from the game's camera the first time and are kept: collision never changes them.
struct OrbitSession {
    double radius = 0, height = 0;
    bool calibrated = false, active = false, pending = false, havePos = false;
    uintptr_t actor = 0, map = 0, walls = 0, floor = 0;
    double stableSince = 0, lastSeen = 0;  // ms
    long epoch = 0;
    float pos[3] = {};
};
static inline void OrbitSuspend(OrbitSession& s) { s.active = s.havePos = false; }
static inline bool OrbitReady(OrbitSession& s, double now, uintptr_t actor, uintptr_t map, uintptr_t walls, uintptr_t floor,
                              const float pos[3], long epoch) {
    double moved = 0;
    if (s.havePos)
        for (int i = 0; i < 3; i++) moved += (pos[i] - s.pos[i]) * (double)(pos[i] - s.pos[i]);
    bool changed = !s.active || s.actor != actor || s.map != map || s.walls != walls || s.floor != floor || s.epoch != epoch ||
                   now - s.lastSeen > 500 || moved > 1500.0 * 1500.0;
    if (changed) { s.pending = true; s.stableSince = now; }
    s.active = s.havePos = true;
    s.actor = actor; s.map = map; s.walls = walls; s.floor = floor; s.epoch = epoch;
    s.lastSeen = now;
    for (int i = 0; i < 3; i++) s.pos[i] = pos[i];
    return !s.pending || now - s.stableSince >= 500;
}
static inline bool OrbitCalibrate(OrbitSession& s, double radius, double height) {
    if (s.calibrated) return true;
    if (!std::isfinite(radius) || radius < 80 || radius > 2500 || !std::isfinite(height) || std::fabs(height) > 1000) return false;
    s.radius = radius;
    s.height = height;
    s.calibrated = true;
    return true;
}

// ------------------------------------------------------------------------------------------------ one camera step
enum OrbitState { kOrbitOff, kOrbitWaitScene, kOrbitSettling, kOrbitBadReference, kOrbitNoGeometry, kOrbitActive };
static const char* const kOrbitStateNames[] = {"off", "waiting for a scene", "settling", "bad reference", "no collision data", "active"};

struct OrbitFrame {  // what the game shows this tick
    double now = 0;  // ms
    long epoch = 0;  // changes when the camera is switched on (resync)
    bool enabled = false, sceneValid = false, input = false;  // input: the game is in front and a controller is there
    uintptr_t actor = 0, map = 0, walls = 0, floor = 0;
    float pos[3] = {}, eye[3] = {}, target[3] = {};  // the hunter, the game's own camera
    double heading = 0;                              // the hunter's facing as a yaw
    short stickX = 0, stickY = 0;
    bool recenter = false;                           // pressed this tick
};
struct OrbitView { float eye[3], target[3]; double wanted, actual; };
// The collision: the safe fraction of start->end (false: no collision data now - the game's camera is kept).
typedef bool (*OrbitSweep)(const float start[3], const float end[3], double* fraction, void* ctx);

struct OrbitCam {
    Orbit orbit;
    OrbitSession ref;
    OrbitSettings set;
    double lastTick = 0, resolved = 0;
    OrbitState state = kOrbitOff;
    uint64_t steps = 0;
};

static inline OrbitState OrbitPause(OrbitCam& c, OrbitState why, bool suspend) {
    if (suspend) OrbitSuspend(c.ref);
    return c.state = why;
}

static inline OrbitState OrbitStep(OrbitCam& c, const OrbitFrame& f, OrbitSweep sweep, void* ctx, OrbitView& v) {
    double dt = f.now >= c.lastTick ? (f.now - c.lastTick) / 1000.0 : 0;
    c.lastTick = f.now;
    if (!f.enabled) return OrbitPause(c, kOrbitOff, true);
    if (!f.sceneValid) return OrbitPause(c, kOrbitWaitScene, true);
    for (int i = 0; i < 3; i++)
        if (!std::isfinite(f.pos[i]) || !std::isfinite(f.eye[i]) || !std::isfinite(f.target[i])) return OrbitPause(c, kOrbitWaitScene, true);
    if (!std::isfinite(f.heading)) return OrbitPause(c, kOrbitWaitScene, true);
    if (!OrbitReady(c.ref, f.now, f.actor, f.map, f.walls, f.floor, f.pos, f.epoch)) return OrbitPause(c, kOrbitSettling, false);
    if (c.ref.pending) {  // resync with the game's camera: its direction the first time, behind the hunter after that
        double x = f.target[0] - f.eye[0], y = f.target[1] - f.eye[1], z = f.target[2] - f.eye[2];
        bool first = !c.ref.calibrated;
        if (!OrbitCalibrate(c.ref, std::sqrt(x * x + y * y + z * z), f.target[1] - f.pos[1])) return OrbitPause(c, kOrbitBadReference, false);
        c.orbit.yaw = c.orbit.wantYaw = first ? std::atan2(x, z) : OrbitWrap(f.heading);
        double p = first ? std::atan2(y, std::sqrt(x * x + z * z)) : -c.set.resetTilt * kOrbitRad;
        c.orbit.pitch = c.orbit.wantPitch = std::clamp(p, -c.set.lookDown * kOrbitRad, c.set.lookUp * kOrbitRad);
        c.resolved = c.ref.radius * c.set.distance;
        c.ref.pending = false;
        dt = 0;
    }
    double sx = 0, sy = 0;
    if (f.input) OrbitStick(f.stickX, f.stickY, c.set.deadzone, sx, sy);
    OrbitTurn(c.orbit, sx, sy, dt, c.set, f.input && f.recenter, f.heading);
    // where the camera looks: above the hunter by the game's own height, plus the height and shoulder offsets - kept
    // out of walls by sweeping there from the plain pivot
    float pivot[3] = {f.pos[0], (float)(f.pos[1] + c.ref.height), f.pos[2]};
    float target[3] = {(float)(pivot[0] + std::cos(c.orbit.yaw) * c.set.shoulder), (float)(pivot[1] + c.set.height),
                       (float)(pivot[2] - std::sin(c.orbit.yaw) * c.set.shoulder)};
    double frac;
    if (c.set.height != 0 || c.set.shoulder != 0) {
        if (!sweep || !sweep(pivot, target, &frac, ctx) || !std::isfinite(frac) || frac < 0 || frac > 1) return OrbitPause(c, kOrbitNoGeometry, false);
        for (int i = 0; i < 3; i++) target[i] = (float)(pivot[i] + (target[i] - pivot[i]) * frac);
    }
    double radius = c.ref.radius * c.set.distance;
    float eye[3];
    OrbitEye(c.orbit, radius, target, eye);
    if (!sweep || !sweep(target, eye, &frac, ctx) || !std::isfinite(frac) || frac < 0 || frac > 1) return OrbitPause(c, kOrbitNoGeometry, false);
    double allowed = radius * frac;
    c.resolved = OrbitRecover(c.resolved, allowed, dt, 5);
    double actual = std::min(allowed, c.resolved);
    if (actual < 0.01) return OrbitPause(c, kOrbitNoGeometry, false);
    for (int i = 0; i < 3; i++) {
        v.target[i] = target[i];
        v.eye[i] = (float)(target[i] + (eye[i] - target[i]) * actual / radius);
    }
    v.wanted = radius;
    v.actual = actual;
    c.steps++;
    return c.state = kOrbitActive;
}

// The game's 16-bit camera yaw for an orbit yaw (the direction from the hunter to the camera).
static inline uint16_t OrbitGameYaw(double yaw) { return (uint16_t)(std::lrint(yaw * 65536.0 / kOrbitTau) - 32768); }
