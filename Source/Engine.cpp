#include "Engine.h"
#include <numeric>

/* Ported from half-life.html. Function names and the order random numbers are drawn in match the prototype,
   so a genome renders the same sound here as it did there (at the same sample rate, with the same sources). */
namespace hl
{
constexpr double REFSR = 48000, ABS_FLOOR = -70;

const char* roleName (Role r) { switch (r) { case Role::Transient: return "transient"; case Role::Body: return "body"; case Role::Tail: return "tail"; default: return "texture"; } }
const char* targetName (Target t) { const char* n[] = { "any", "punchy", "long", "bright", "dark", "wrong" }; return n[(int) t]; }
std::string keyName (int key) { const char* n[] = { "A", "B", "C", "D", "Noise", "Click", "Blip" }; return n[key]; }

struct Rg { double a, b; };
struct RoleDef { Rg grain, dens, count, spread, pjit, rev, gain, start, decay, bright, pitch; };
static const RoleDef& ROLES (Role r)
{
    static const RoleDef T[4] = {
        { {4,28}, {0,0}, {1,3}, {0,0.08}, {0,1}, {0,0.1}, {0.8,1}, {0,0}, {30,160}, {0.7,1}, {-5,7} },
        { {25,110}, {18,60}, {1,1}, {0.02,0.15}, {0,0.6}, {0,0.3}, {0.55,0.85}, {0,6}, {150,650}, {0.45,0.85}, {-7,5} },
        { {60,260}, {8,28}, {1,1}, {0.05,0.3}, {0,2}, {0.1,0.6}, {0.35,0.6}, {8,90}, {500,2000}, {0.25,0.65}, {-12,3} },
        { {6,45}, {40,160}, {1,1}, {0.1,0.4}, {0.5,4}, {0,0.5}, {0.22,0.45}, {0,40}, {180,1300}, {0.4,0.9}, {-3,12} } };
    return T[(int) r];
}
static double ROLE_OFF (Role r) { switch (r) { case Role::Transient: return 12; case Role::Body: return 0; case Role::Tail: return -18; default: return -12; } }
static double jround (double x) { return std::floor (x + 0.5); }   // JavaScript's Math.round
static double rrg (Rng& r, Rg g) { return g.a + (g.b - g.a) * r(); }

/* ---------- chaos ---------- */
static double chuaF (double x) { return -0.714 * x - 0.2145 * (std::abs (x + 1) - std::abs (x - 1)) + 0.012 * x * x * x; }

Circuit newCircuit (double a, double sp, const std::function<double()>& r)
{
    Circuit c; c.a = a; c.sp = sp;
    const double mag = 0.2 + 0.6 * r(); const double sg = r() < 0.5 ? -1 : 1;
    c.s[0] = mag * sg; c.s[1] = (r() - 0.5) * 0.2; c.s[2] = (r() - 0.5) * 1.5;
    advanceNet ({ &c }, 60 + 40 * r(), 0);
    return c;
}

void advanceNet (std::vector<Circuit*> net, double units, double k)
{
    const int n = (int) net.size(); if (units <= 0 || ! n) return;
    const int steps = (int) std::ceil (units / 0.01); const double h = units / steps;
    std::vector<double> X ((size_t) n);
    for (int st = 0; st < steps; ++st)
    {
        for (int i = 0; i < n; ++i) X[(size_t) i] = net[(size_t) i]->s[0];
        for (int i = 0; i < n; ++i)
        {
            Circuit& c = *net[(size_t) i]; const double dt = h * c.sp, a = c.a;
            const double cx = n > 1 ? k * ((X[(size_t) ((i + 1) % n)] + X[(size_t) ((i + n - 1) % n)]) / 2 - X[(size_t) i]) : 0;
            auto d = [&] (double x, double y, double z, double* o) { o[0] = a * (y - x - chuaF (x)) + cx; o[1] = x - y + z; o[2] = -28 * y; };
            const double x = c.s[0], y = c.s[1], z = c.s[2]; double k1[3], k2[3], k3[3], k4[3];
            d (x, y, z, k1); d (x + dt / 2 * k1[0], y + dt / 2 * k1[1], z + dt / 2 * k1[2], k2);
            d (x + dt / 2 * k2[0], y + dt / 2 * k2[1], z + dt / 2 * k2[2], k3); d (x + dt * k3[0], y + dt * k3[1], z + dt * k3[2], k4);
            for (int j = 0; j < 3; ++j) c.s[j] += dt / 6 * (k1[j] + 2 * k2[j] + 2 * k3[j] + k4[j]);
            if (! (std::abs (c.s[0]) < 40)) { c.s[0] = 0.7; c.s[1] = 0; c.s[2] = -0.5; }
            const double b = std::min (1.0, dt / 25);
            for (int j = 0; j < 3; ++j) { c.m[j] += (c.s[j] - c.m[j]) * b; c.d[j] += (std::abs (c.s[j] - c.m[j]) - c.d[j]) * b; }
        }
    }
}

double chOut (const Circuit& c, int j) { return std::tanh ((c.s[j] - c.m[j]) / (1.5 * c.d[j] + 1e-6)); }

const char* HKEYS[NH] = { "jitter", "lpgTime", "tilt", "drift", "density", "reverse", "width", "wobble" };
const char* HLABEL[NH] = { "Grain scatter", "Gate time", "Brightness tilt", "Pitch drift", "Grain density", "Reverse bias", "Stereo width", "Filter wobble" };
const char* GLABEL[NG] = { "position", "grain size", "pitch", "density", "gate time", "brightness", "pan", "reverse" };
const char* ROTLABEL[NROT] = { "Pitch sag", "Flutter", "Band erosion", "Rate decay", "Bit rot", "Dropouts", "Dust", "Grain starvation", "Last gasps" };

HRoute makeHRoute (const std::function<double()>& r)
{
    HRoute o; auto sg = [&] { return r() < 0.5 ? -1.0 : 1.0; };
    for (auto& t : o) { t.a = (int) (r() * 12); t.b = (int) (r() * 12); if (t.b == t.a) t.b = (t.a + 5) % 12; t.w = 0.55 + 0.4 * r(); t.sa = sg(); t.sb = sg(); }
    return o;
}
static double sig (const std::array<Circuit, 4>& net, int i) { return chOut (net[(size_t) (i / 3)], i % 3); }
HVals hvalsOf (const std::array<Circuit, 4>& net, const HRoute& route)
{
    HVals o {};
    for (int k = 0; k < NH; ++k) { const auto& t = route[(size_t) k]; o[(size_t) k] = clampd (t.w * t.sa * sig (net, t.a) + (1 - t.w) * t.sb * sig (net, t.b), -1, 1); }
    return o;
}

Glide makeGlide (int i)
{
    Glide g;
    std::vector<int> pool (NG); std::iota (pool.begin(), pool.end(), 0);
    for (int k = NG - 1; k > 0; --k) std::swap (pool[(size_t) k], pool[(size_t) (int) (rnd() * (k + 1))]);
    const int n = 2 + (int) (rnd() * 2);
    for (int k = 0; k < n; ++k) { GRoute rt; rt.dest = pool[(size_t) k]; rt.depth = (rnd() < 0.5 ? -1 : 1) * (0.4 + rnd() * 0.6); rt.comp = (int) (rnd() * 3); g.routes.push_back (rt); }
    const double A[] = { 12, 14, 15.6, 16.5 }, SP[] = { 1.15, 0.8, 0.6, 1.45 };
    g.c = newCircuit (A[i], SP[i], rnd);
    readGlide (g);
    return g;
}
void readGlide (Glide& g) { for (int j = 0; j < 3; ++j) g.vals[j] = chOut (g.c, j); g.val = g.vals[0]; }

/* ---------- engine ---------- */
Engine::Engine()
{
    setRate (48000);
    HNET = { newCircuit (12, 1.0, rnd), newCircuit (14, 0.7, rnd), newCircuit (15.6, 1.3, rnd), newCircuit (16.5, 0.55, rnd) };
    HROUTE = makeHRoute (rnd);
    live = hvalsOf (HNET, HROUTE);
    for (int i = 0; i < 4; ++i) { slots[(size_t) i].idx = i; slots[(size_t) i].glide = makeGlide (i); }
}

void Engine::setRate (double sr) { SR = sr; DC_R = 1 - 2 * PI * 5 / SR; }

static void dcBlock (std::vector<float>& d, double DC_R)
{
    double x1 = 0, y1 = 0;
    for (auto& v : d) { const double x = v, y = x - x1 + DC_R * y1; x1 = x; y1 = y; v = (float) y; }
}

void Engine::setSlotData (int i, std::vector<float> data, const std::string& name, bool example, bool dc)
{
    auto& sl = slots[(size_t) i];
    if (dc) dcBlock (data, DC_R);
    sl.data = std::move (data);
    sl.mip = std::make_unique<Mip> (sl.data.data(), (int) sl.data.size());
    sl.an = analyze (sl.data, SR); sl.name = name; sl.example = example;
    if (example) { sl.path.clear(); sl.fromPool = false; sl.regionA = sl.regionB = -1; sl.poolFile = -1; }
}

void Engine::loadBuiltin (int i, const std::string& name, bool example)
{
    setSlotData (i, makeBuiltin (name, SR), name, true);
    (void) example;
}

Analysis Engine::analyze (const std::vector<float>& data, double sr)
{
    Analysis an;
    const int win = (int) jround (sr * 0.01), len = (int) data.size(), n = std::max (1, len / std::max (1, win));
    an.win = win; an.n = n; an.rms.resize ((size_t) n); an.db.resize ((size_t) n); an.abs.resize ((size_t) n);
    double ref = 0;
    for (int w = 0; w < n; ++w)
    {
        double s = 0; const int o = w * win;
        for (int i = 0; i < win; ++i) { const double v = o + i < len ? data[(size_t) (o + i)] : 0; s += v * v; }
        an.rms[(size_t) w] = (float) std::sqrt (s / win); ref = std::max (ref, (double) an.rms[(size_t) w]);
    }
    for (int w = 0; w < n; ++w) { an.db[(size_t) w] = (float) linToDb (an.rms[(size_t) w] / (ref > 0 ? ref : 1e-9)); an.abs[(size_t) w] = (float) linToDb (an.rms[(size_t) w]); }
    for (int w = 0; w < n; ++w)
    {
        const double prev = w >= 3 ? ((double) an.rms[(size_t) w - 1] + an.rms[(size_t) w - 2] + an.rms[(size_t) w - 3]) / 3 : 0;
        if (an.rms[(size_t) w] > prev * 2.2 + 1e-5 && an.db[(size_t) w] > -24 && (an.onsets.empty() || w - an.onsets.back() > 4)) an.onsets.push_back (w);
    }
    an.ref = ref;
    return an;
}

std::vector<int> Engine::validFor (const Analysis& an, Role role) const
{
    if (role == Role::Transient)
    {
        const double thr = P.threshold + ROLE_OFF (Role::Body); std::vector<int> on;
        for (int w : an.onsets) if (an.db[(size_t) w] > thr && an.abs[(size_t) w] > ABS_FLOOR) on.push_back (w);
        if (! on.empty()) return on;
    }
    const double thr = P.threshold + ROLE_OFF (role); std::vector<int> out;
    for (int w = 0; w < an.n; ++w) if (an.db[(size_t) w] > thr && an.abs[(size_t) w] > ABS_FLOOR) out.push_back (w);
    return out;
}

std::vector<int> Engine::validChain (const Analysis& an, Role role) const
{
    std::vector<Role> chain;
    switch (role)
    {
        case Role::Transient: chain = { Role::Transient, Role::Body }; break;
        case Role::Body: chain = { Role::Body, Role::Texture }; break;
        case Role::Tail: chain = { Role::Tail, Role::Texture, Role::Body }; break;
        default: chain = { Role::Texture, Role::Body }; break;
    }
    for (auto r : chain) { auto v = validFor (an, r); if (! v.empty()) return v; }
    return {};
}

Role Engine::pickRole (const Analysis& an, Rng& r)
{
    const double dur = an.n * 0.01, onRate = an.onsets.size() / std::max (0.1, dur);
    int sus = 0; for (int w = 0; w < an.n; ++w) if (an.db[(size_t) w] > -20) sus++;
    const double susF = (double) sus / an.n;
    const double W[4] = { 0.6 + std::min (2.0, onRate * 0.4), 1.2, 0.5 + susF * 2, 0.6 + susF };
    double tot = 0; for (double w : W) tot += w;
    double x = r() * tot;
    for (int k = 0; k < 4; ++k) { x -= W[k]; if (x <= 0) return (Role) k; }
    return Role::Body;
}

double Engine::fitScore (const Analysis& an, Role role)
{
    const double dur = an.n * 0.01; const int hits = (int) an.onsets.size(); const double rate = hits / std::max (0.1, dur);
    double sus = 0; for (int w = 0; w < an.n; ++w) if (an.db[(size_t) w] > -20) sus++; sus /= an.n;
    double s = 0;
    switch (role)
    {
        case Role::Transient: s = (dur < 0.6 ? 1 : dur < 1.5 ? 0.3 : -0.7) + (hits ? 0.3 : -0.4) - sus * 0.6; break;
        case Role::Body: s = 0.9 - std::abs (std::log2 (std::max (0.05, dur) / 0.8)) * 0.5 + (hits ? 0.1 : 0); break;
        case Role::Tail: s = (dur > 2 ? 0.9 : dur > 1 ? 0.2 : -0.8) + sus * 0.8 - 0.3; break;
        default: s = std::min (1.0, rate / 6) + sus * 0.4 - 0.3; break;
    }
    return clampd (s, -1, 1);
}

std::vector<Engine::Src> Engine::sources() const
{
    std::vector<Src> s;
    for (int i = 0; i < 4; ++i) if (slots[(size_t) i].has()) s.push_back ({ i, Kind::Slot });
    if (P.noiseOn) s.push_back ({ KNoise, Kind::Noise });
    if (P.clickOn) s.push_back ({ KClick, Kind::Click });
    if (P.blipOn) s.push_back ({ KBlip, Kind::Blip });
    return s;
}

static Role fromSlotRole (SlotRole s) { return (Role) ((int) s - 1); }

Role Engine::roleFor (const Src& s, Rng& r) const
{
    if (s.kind == Kind::Slot) { const auto sr = P.slotRole[(size_t) s.key]; return sr == SlotRole::Auto ? pickRole (slots[(size_t) s.key].an, r) : fromSlotRole (sr); }
    if (s.kind == Kind::Noise) return P.noiseRole;
    if (s.kind == Kind::Click) return Role::Transient;
    return Role::Body;
}

Layer Engine::buildLayer (const Src& s, Rng& r, const Role* force) const
{
    Layer L; L.key = s.key; L.kind = s.kind; L.role = force ? *force : roleFor (s, r);
    const auto& R = ROLES (L.role);
    L.seed = (uint32_t) (r() * 4294967296.0);
    L.pos = r(); L.spread = rrg (r, R.spread); L.grain = rrg (r, R.grain); L.dens = rrg (r, R.dens); L.count = (int) jround (rrg (r, R.count));
    L.pitch = rrg (r, R.pitch); L.pjit = rrg (r, R.pjit); L.rev = rrg (r, R.rev); L.pan = (r() * 2 - 1) * 0.6; L.width = rr (r, 0.1, 0.7);
    L.gain = rrg (r, R.gain); L.start = rrg (r, R.start); L.decay = rrg (r, R.decay); L.curve = rr (r, 0.2, 0.85);
    L.bright = rrg (r, R.bright); L.reso = rr (r, 0, 0.6); L.f0 = rr (r, 70, 700);
    return L;
}

void Engine::perturb (Layer& L, Rng& r, double amt) const
{
    const auto& R = ROLES (L.role);
    struct M { double Layer::* f; Rg g; bool pitch; };
    const M mut[] = { { &Layer::grain, R.grain, false }, { &Layer::dens, R.dens, false }, { &Layer::spread, R.spread, false }, { &Layer::pjit, R.pjit, false },
                      { &Layer::rev, R.rev, false }, { &Layer::gain, R.gain, false }, { &Layer::start, R.start, false }, { &Layer::decay, R.decay, false },
                      { &Layer::bright, R.bright, false }, { &Layer::pitch, R.pitch, true } };
    for (auto& m : mut)
    {
        const double lo = m.g.a, hi = m.g.b, span = hi - lo; if (span <= 0) continue;
        const double floorV = m.pitch ? -24.0 : m.f == &Layer::decay ? lo * 0.5 : 0.0;   // a decay can shrink, but not to nothing
        L.*m.f = clampd (L.*m.f + (r() * 2 - 1) * amt * span, std::max (floorV, lo - span * 0.25), hi + span * 0.25);
    }
    // usually a nudge; sometimes (more often at high Mutation) a jump to a different part of the sample
    if (r() < amt * 0.3) L.pos = r(); else L.pos = std::fmod (L.pos + gauss (r) * amt * 0.35 + 1, 1.0);
    L.pan = clampd (L.pan + (r() * 2 - 1) * amt * 0.8, -1, 1);
    L.width = clampd (L.width + (r() * 2 - 1) * amt * 0.5, 0, 1);
    L.curve = clampd (L.curve + (r() * 2 - 1) * amt * 0.5, 0.05, 0.95);
    L.reso = clampd (L.reso + (r() * 2 - 1) * amt * 0.4, 0, 0.9);
    L.f0 = clampd (L.f0 * std::pow (2.0, (r() * 2 - 1) * amt * 1.5), 40, 1500);
    if (L.role == Role::Transient) L.count = (int) clampd (jround (L.count + (r() * 2 - 1) * amt * 2), 1, 4);
    if (r() < 0.25 + amt * 0.75) L.seed = (uint32_t) (r() * 4294967296.0);
}

std::array<GlideSnap, 4> Engine::snapGlide() const
{
    std::array<GlideSnap, 4> o;
    for (int i = 0; i < 4; ++i) { const auto& g = slots[(size_t) i].glide; o[(size_t) i].val = g.val; for (int j = 0; j < 3; ++j) o[(size_t) i].vals[j] = g.vals[j]; o[(size_t) i].routes = g.routes; }
    return o;
}

ChaosSnap Engine::snapChaos() const
{
    ChaosSnap c; c.h = HNET; for (int i = 0; i < 4; ++i) c.sl[(size_t) i] = slots[(size_t) i].glide.c; c.route = HROUTE; c.k = P.chaosCouple;
    return c;
}

ChaosSnap Engine::mutChaos (const ChaosSnap& pc, Rng& r, double amt) const
{
    ChaosSnap c = pc;
    auto nudge = [&] (Circuit& x) { x.s[0] += (r() - 0.5) * 0.004 * amt; x.s[2] += (r() - 0.5) * 0.004 * amt; };
    for (auto& x : c.h) nudge (x);
    for (auto& x : c.sl) nudge (x);
    const double run = 8 * amt * amt;
    advanceNet ({ &c.h[0], &c.h[1], &c.h[2], &c.h[3] }, run, c.k);
    advanceNet ({ &c.sl[0], &c.sl[1], &c.sl[2], &c.sl[3] }, run, c.k);
    return c;
}

void Engine::chaosStart (Genome& g) const
{
    g.hidden = hvalsOf (g.chaos.h, g.chaos.route);
    for (int i = 0; i < 4; ++i) { for (int j = 0; j < 3; ++j) g.glide[(size_t) i].vals[j] = chOut (g.chaos.sl[(size_t) i], j); g.glide[(size_t) i].val = g.glide[(size_t) i].vals[0]; }
}

constexpr int CHF = 256;
static double trkAt (const std::vector<float>& a, double i)
{
    const double x = std::max (0.0, i) / CHF; const int j = std::min ((int) a.size() - 2, (int) x); const double f = std::min (1.0, x - j);
    return a[(size_t) j] + (a[(size_t) j + 1] - a[(size_t) j]) * f;
}

void Engine::chaosTrack (const Genome& g, int N)
{
    trk.on = false;
    if (P.chaosTurb <= 0) return;
    const int F = (int) std::ceil ((double) N / CHF) + 2;
    auto h = g.chaos.h; auto sl = g.chaos.sl;
    for (auto& v : trk.h) v.assign ((size_t) F, 0.f);
    for (auto& s : trk.s) for (auto& v : s) v.assign ((size_t) F, 0.f);
    const double u = CHF / SR * P.chaosTurb;
    for (int f = 0; f < F; ++f)
    {
        const auto hv = hvalsOf (h, g.chaos.route);
        for (int k = 0; k < NH; ++k) trk.h[(size_t) k][(size_t) f] = (float) hv[(size_t) k];
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) trk.s[(size_t) i][(size_t) j][(size_t) f] = (float) chOut (sl[(size_t) i], j);
        advanceNet ({ &h[0], &h[1], &h[2], &h[3] }, u, g.chaos.k);
        advanceNet ({ &sl[0], &sl[1], &sl[2], &sl[3] }, u, g.chaos.k);
    }
    trk.on = true;
}

double Engine::Hv (const Genome& g, int key, double i) const { return trk.on ? trkAt (trk.h[(size_t) key], i) : g.hidden[(size_t) key]; }

double Engine::glideMod (const Genome& g, int key, int dest, double i) const
{
    if (key > 3) return 0;
    const auto& s = g.glide[(size_t) key];
    double m = 0;
    for (const auto& rt : s.routes) { if (rt.dest != dest) continue; const int j = rt.comp; m += rt.depth * (trk.on ? trkAt (trk.s[(size_t) key][(size_t) j], i) : s.vals[j]); }
    return m * P.glide;
}

/* ---------- Shape: an envelope the sound is built toward ----------
   It doesn't touch pitch: the gate times follow it per role (transients short, tails long), the LPG body stage holds
   at its sustain, a slow attack softens and staggers the hit, a final gain ride (±12 dB) nudges the loudness curve,
   and Decide by keeps the candidate closest to it. Follow sets how strictly: 0 = none of this. */
static double lerpd (double a, double b, double t) { return a + (b - a) * t; }
static double shRole (Role r) { switch (r) { case Role::Transient: return 0.25; case Role::Body: return 0.8; case Role::Tail: return 1.6; case Role::Texture: return 1.2; } return 1; }

double Engine::shapeT (double t) const { return shapeLevel (P, t); }

void Engine::bodyEff (double& level, double& time) const
{
    const double F = P.shFollow, S = P.shS;
    level = lerpd (P.body, S, F);
    time = lerpd (P.bodyTime, S > 0.01 ? P.maxLen * 4 : std::max (0.3, P.shD / 1000), F);
}

double Engine::effDecay (const Layer& lay, const Genome& g) const
{
    const double I = P.instability, st = std::floor (lay.start * SR / 1000);
    const double dec = lerpd (lay.decay, P.shD * shRole (lay.role), P.shFollow);
    // never 0: the gate divides by it (a 0 here turned the whole sound into NaN, i.e. silence)
    return std::max (1.0, dec * P.lpgResp * std::pow (2.0, Hv (g, LpgTime, st) * I * 1.2 + glideMod (g, lay.key, GGate, st) * 1.2));
}

int Engine::lifeS (double decayS) const
{
    double bl, bt; bodyEff (bl, bt);
    return (int) std::floor (std::max (decayS * 1.3, bl > 0.01 ? std::min (P.maxLen, bt) * SR * 1.1 : 0.0));
}

void Engine::shapeCorrect (std::vector<float>& ML, std::vector<float>& MR, double& peak, std::vector<double>& wrOut, double& pwOut) const
{
    const double F = P.shFollow; if (F < 0.01) return;
    const int N = (int) ML.size(), w = std::max (1, (int) std::floor (SR * 0.01)), nw = (int) std::ceil ((double) N / w);
    std::vector<double> e ((size_t) nw); double mx = 0;
    for (int j = 0; j < nw; ++j) { double s = 0; const int o = j * w; for (int i = o; i < o + w && i < N; ++i) s += (double) ML[(size_t) i] * ML[(size_t) i] + (double) MR[(size_t) i] * MR[(size_t) i]; e[(size_t) j] = std::sqrt (s / (2 * w)); mx = std::max (mx, e[(size_t) j]); }
    if (mx <= 0) return;
    std::vector<double> gw ((size_t) nw);
    for (int j = 0; j < nw; ++j)
    {
        const double t = (j + 0.5) * w / SR, T = shapeT (t), x = e[(size_t) j] / mx;
        double k = x > 0.003 ? T / x : (T < 0.003 ? 0 : 1); k = clampd (k, 0.25, 4); if (T < 0.001) k = std::min (k, x > 0 ? 0.001 / x : 1.0);
        gw[(size_t) j] = std::pow (k, F);
    }
    // smooth the gain curve (about 30 ms) so it rides, never pumps
    for (int pass = 0; pass < 2; ++pass)
    {
        double y = gw[0]; for (int j = 0; j < nw; ++j) { y += (gw[(size_t) j] - y) * 0.35; gw[(size_t) j] = y; }
        double y2 = gw[(size_t) nw - 1]; for (int j = nw - 1; j >= 0; --j) { y2 += (gw[(size_t) j] - y2) * 0.35; gw[(size_t) j] = y2; }
    }
    double pk = 0;
    for (int i = 0; i < N; ++i)
    {
        const double x = (double) i / w - 0.5; const int j = std::max (0, std::min (nw - 2, (int) std::floor (x))); const double f = std::max (0.0, std::min (1.0, x - j));
        const double k = nw > 1 ? gw[(size_t) j] + (gw[(size_t) j + 1] - gw[(size_t) j]) * f : gw[0];
        ML[(size_t) i] = (float) (ML[(size_t) i] * k); MR[(size_t) i] = (float) (MR[(size_t) i] * k);
        pk = std::max ({ pk, (double) std::abs (ML[(size_t) i]), (double) std::abs (MR[(size_t) i]) });
    }
    peak = pk;
    // the 5 ms windows the half-life reading uses
    const int w5 = (int) std::floor (SR * 0.005); wrOut.clear(); pwOut = 0;
    for (int o = 0; o + w5 <= N; o += w5) { double s = 0; for (int i = 0; i < w5; ++i) { const double v = ((double) ML[(size_t) (o + i)] + MR[(size_t) (o + i)]) * 0.5; s += v * v; } const double xx = std::sqrt (s / w5); wrOut.push_back (xx); pwOut = std::max (pwOut, xx); }
}

// how close a finished sound's loudness curve is to the Shape: mean dB distance over 10 ms windows (floor -40 dB), negated
double Engine::shapeScore (const std::vector<float>& L, const std::vector<float>& R, int n) const
{
    const int w = std::max (1, (int) std::floor (SR * 0.01)), nw = (int) std::ceil (std::max ((double) n, P.maxLen * SR * 0.25) / w);
    std::vector<double> e; double mx = 0;
    for (int j = 0; j < nw; ++j) { double s = 0; const int o = j * w; for (int i = o; i < o + w && i < n; ++i) s += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; const double v = std::sqrt (s / (2 * w)); e.push_back (v); mx = std::max (mx, v); }
    auto db = [] (double x) { return std::max (-40.0, 20 * std::log10 (std::max (x, 1e-9))); };
    double d = 0; for (int j = 0; j < nw; ++j) d += std::abs (db (e[(size_t) j] / (mx > 0 ? mx : 1)) - db (shapeT ((j + 0.5) * w / SR)));
    return -d / nw;
}

/* ---------- genome ---------- */
void Engine::syncGenome (Genome& g)
{
    Rng r (g.seed ^ 0xA5A5A5A5u);
    for (const auto& src : sources())
    {
        auto it = std::find_if (g.layers.begin(), g.layers.end(), [&] (const Layer& l) { return l.key == src.key; });
        bool hasForced = false; Role forced = Role::Body;
        if (src.kind == Kind::Slot) { const auto sr = P.slotRole[(size_t) src.key]; if (sr != SlotRole::Auto) { hasForced = true; forced = fromSlotRole (sr); } }
        else if (src.kind == Kind::Noise) { hasForced = true; forced = P.noiseRole; }
        if (it == g.layers.end()) { g.layers.push_back (buildLayer (src, r)); continue; }
        if (hasForced && it->role != forced)
        {
            Layer nl = buildLayer (src, r, &forced);
            nl.seed = it->seed; nl.pos = it->pos; nl.pan = it->pan;
            *it = nl;
        }
    }
    for (const auto& s : sources()) if (std::find (g.order.begin(), g.order.end(), s.key) == g.order.end()) g.order.push_back (s.key);
}

static std::array<double, NROT> makeRot (Rng& r) { std::array<double, NROT> o {}; for (auto& v : o) v = 0.2 + 0.8 * r(); return o; }

void Engine::addLink (std::vector<int>& order, std::vector<Link>& links, Rng& r) const
{
    static const int FOLLOW_SLOT[] = { LDensity, LPosition, LPitch, LBright, LDuck }, FOLLOW_GEN[] = { LBright, LDuck };
    for (int tries = 0; tries < 30 && order.size() > 1; ++tries)
    {
        const int len = (int) order.size();
        const int i = (int) std::floor (r() * (len - 1)), j = i + 1 + (int) std::floor (r() * (len - 1 - i));
        const int from = order[(size_t) i], to = order[(size_t) j];
        if (from == KClick) continue;
        const bool toGen = to > 3;
        const LinkType type = r() < 0.32 ? LinkType::Clock : LinkType::Follow;
        int dest = LClock;
        if (type == LinkType::Follow) dest = toGen ? FOLLOW_GEN[(int) std::floor (r() * 2)] : FOLLOW_SLOT[(int) std::floor (r() * 5)];
        if (std::any_of (links.begin(), links.end(), [&] (const Link& l) { return l.from == from && l.to == to && l.dest == dest; })) continue;
        double amount = 0.35 + 0.65 * r();
        if (type == LinkType::Follow && dest != LDuck && r() < 0.5) amount = -amount;
        links.push_back ({ from, to, type, dest, amount });
        return;
    }
}

std::pair<std::vector<int>, std::vector<Link>> Engine::makeLinks (const std::vector<int>& keys, Rng& r) const
{
    auto order = keys;
    for (int i = (int) order.size() - 1; i > 0; --i) { const int j = (int) std::floor (r() * (i + 1)); std::swap (order[(size_t) i], order[(size_t) j]); }
    std::vector<Link> links; const int want = (int) jround (P.interact * 4 + r() * P.interact * 2);
    for (int k = 0; k < want; ++k) addLink (order, links, r);
    return { order, links };
}

std::pair<std::vector<int>, std::vector<Link>> Engine::mutateLinks (const Genome& pg, const std::vector<int>& keys, Rng& r, double amt) const
{
    std::vector<int> order;
    for (int k : pg.order) if (std::find (keys.begin(), keys.end(), k) != keys.end()) order.push_back (k);
    for (int k : keys) if (std::find (order.begin(), order.end(), k) == order.end()) order.insert (order.begin() + (int) std::floor (r() * (order.size() + 1)), k);
    auto idx = [&] (int k) { return (int) (std::find (order.begin(), order.end(), k) - order.begin()); };
    auto has = [&] (int k) { return std::find (order.begin(), order.end(), k) != order.end(); };
    std::vector<Link> links;
    for (const auto& l : pg.links) if (has (l.from) && has (l.to) && idx (l.from) < idx (l.to)) links.push_back (l);
    for (auto& l : links) l.amount = clampd (l.amount + (r() * 2 - 1) * amt * 0.6, -1, 1);
    if (! links.empty() && r() < amt * 0.6) links.erase (links.begin() + (int) std::floor (r() * links.size()));
    if (r() < amt * 0.6) addLink (order, links, r);
    return { order, links };
}

Genome Engine::freshGenome()
{
    Genome g; g.seed = newSeed(); Rng r (g.seed);
    const Genome* prev = current ? &current->genome : nullptr;
    for (const auto& src : sources())
    {
        const Layer* old = nullptr;
        if (prev) for (const auto& l : prev->layers) if (l.key == src.key) old = &l;
        if (old && locks.count (src.key)) g.layers.push_back (*old);
        else g.layers.push_back (buildLayer (src, r));
    }
    std::vector<int> keys; for (const auto& s : sources()) keys.push_back (s.key);
    auto net = P.lockLinks && prev ? mutateLinks (*prev, keys, r, 0) : makeLinks (keys, r);
    g.hidden = live; g.glide = snapGlide(); g.chaos = snapChaos(); g.order = net.first; g.links = net.second;
    g.resJit = r() * 2 - 1; g.rot = makeRot (r);
    return g;
}

Genome Engine::mutGenome()
{
    const double amt = P.mutation; Genome c; c.seed = newSeed(); Rng r (c.seed);
    const Genome& parent = current->genome;
    for (const auto& src : sources())
    {
        const Layer* old = nullptr; for (const auto& l : parent.layers) if (l.key == src.key) old = &l;
        if (! old) { c.layers.push_back (buildLayer (src, r)); continue; }
        if (locks.count (src.key)) { c.layers.push_back (*old); continue; }
        const bool autoRole = src.kind == Kind::Slot && P.slotRole[(size_t) src.key] == SlotRole::Auto;
        if (autoRole && r() < amt * 0.3)
        {
            const Role nr = pickRole (slots[(size_t) src.key].an, r);
            if (nr != old->role) { Layer nl = buildLayer (src, r, &nr); nl.pos = old->pos; nl.pan = old->pan; c.layers.push_back (nl); continue; }
        }
        Layer L = *old; perturb (L, r, amt); c.layers.push_back (L);
    }
    c.chaos = mutChaos (parent.chaos, r, amt);
    const auto now = snapGlide();
    for (int i = 0; i < 4; ++i) { c.glide[(size_t) i] = {}; c.glide[(size_t) i].routes = now[(size_t) i].routes; }
    std::vector<int> keys; for (const auto& s : sources()) keys.push_back (s.key);
    auto net = mutateLinks (parent, keys, r, P.lockLinks ? 0 : amt);
    c.order = net.first; c.links = net.second;
    c.resJit = clampd (parent.resJit + (r() * 2 - 1) * amt, -1, 1);
    for (int k = 0; k < NROT; ++k) c.rot[(size_t) k] = clampd (parent.rot[(size_t) k] + (r() * 2 - 1) * amt * 0.5, 0.05, 1);
    chaosStart (c);
    return c;
}

/* ---------- interaction: followers ---------- */
constexpr int FSTEP = 32;
static std::vector<float> follower (const std::vector<float>& L, const std::vector<float>& R, double SR)
{
    const int N = (int) L.size(), n = (N + FSTEP - 1) / FSTEP; std::vector<float> e ((size_t) n);
    const double atk = 1 - std::exp (-FSTEP / (SR * 0.002)), rel = 1 - std::exp (-FSTEP / (SR * 0.05));
    double y = 0, mx = 0;
    for (int j = 0; j < n; ++j)
    {
        double pk = 0; const int o = j * FSTEP;
        for (int i = o; i < o + FSTEP && i < N; ++i) { const double v = std::abs (L[(size_t) i]) + std::abs (R[(size_t) i]); if (v > pk) pk = v; }
        y += (pk > y ? atk : rel) * (pk - y); e[(size_t) j] = (float) y; if (y > mx) mx = y;
    }
    if (mx > 0) for (auto& v : e) v = (float) (v / mx);
    return e;
}
static std::vector<int> onsetsOf (const std::vector<float>& e, double SR)
{
    const int gap = (int) jround (SR * 0.04 / FSTEP), lag = (int) jround (SR * 0.006 / FSTEP); std::vector<int> out;
    for (int j = 1; j < (int) e.size(); ++j)
        if (e[(size_t) j] > 0.2 && e[(size_t) j] > e[(size_t) std::max (0, j - lag)] * 1.6 + 0.04 && (out.empty() || j - out.back() > gap)) out.push_back (j);
    if (out.size() > 16) out.resize (16);
    return out;
}
double Engine::modAt (const std::vector<std::pair<const std::vector<float>*, double>>& list, double i)
{
    if (list.empty()) return 0;
    const double x = i / FSTEP; const int j = (int) x; const double f = x - j; double s = 0;
    for (const auto& o : list)
    {
        const auto& e = *o.first; const int last = (int) e.size() - 1;
        const double a = e[(size_t) std::min (last, j)], b = e[(size_t) std::min (last, j + 1)];
        s += o.second * (a + (b - a) * f);
    }
    return s;
}

void Engine::addGasps (Mods& m, const Layer& lay, const Genome& g) const
{
    const double k = P.rot * g.rot[Gasp]; if (k < 0.05) return;
    Rng r (lay.seed ^ 0x51ed27u);
    const double life = effDecay (lay, g) * SR / 1000; const int start = (int) std::floor (lay.start * SR / 1000);
    const int cnt = (int) jround (k * 2 + k * 5 * r());
    for (int c = 0; c < cnt; ++c) { const int t = start + (int) std::floor (life * (0.35 + 0.6 * r())); const double level = (0.02 + 0.12 * r()) * k; m.strikes.push_back ({ t, level }); }
    std::stable_sort (m.strikes.begin(), m.strikes.end(), [] (const Mods::Strike& a, const Mods::Strike& b) { return a.t < b.t; });
}

/* ---------- rot ---------- */
void Engine::applyRot (std::vector<float>& L, std::vector<float>& R, int n, const Genome& g) const
{
    const double rot = P.rot; if (rot <= 0) return;
    const auto& W = g.rot; Rng r (g.seed ^ 0x6c8e9cf5u); const double on = P.rotOnset;
    std::vector<float> env ((size_t) n), age ((size_t) n);
    const double at = 1 - std::exp (-1 / (SR * 0.001)), rl = 1 - std::exp (-1 / (SR * 0.06)); double y = 0, mx = 0;
    for (int i = 0; i < n; ++i) { const double x = std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])); y += (x > y ? at : rl) * (x - y); env[(size_t) i] = (float) y; if (y > mx) mx = y; }
    for (int i = 0; i < n; ++i) { const double a0 = 1 - std::pow (env[(size_t) i] / (mx > 0 ? mx : 1), 0.35); age[(size_t) i] = (float) (rot * clampd ((a0 - on) / (1 - on + 1e-6), 0, 1)); }
    const double step = SR * 0.012; std::vector<double> fl;
    for (int j = 0; j < (int) std::ceil (n / step) + 3; ++j) fl.push_back (r() * 2 - 1);
    auto flAt = [&] (int i) { const double x = i / step; const int j = (int) x; const double f = x - j, c = 0.5 - 0.5 * std::cos (PI * f); return fl[(size_t) j] + (fl[(size_t) j + 1] - fl[(size_t) j]) * c; };
    std::vector<float> XL ((size_t) n), XR ((size_t) n); double p = 0;
    for (int i = 0; i < n; ++i)
    {
        XL[(size_t) i] = (float) sread (L.data(), n, p); XR[(size_t) i] = (float) sread (R.data(), n, p);
        const double A = age[(size_t) i];
        p = std::min ((double) n - 3, p + std::pow (2.0, (-W[Droop] * A * 4 + W[Flutter] * A * 0.5 * flAt (i)) / 12));
    }
    const double ms = SR / 1000, dgk = 1 - std::exp (-1 / (SR * 0.0015));
    double lpL = 0, lpR = 0, hpL = 0, hpR = 0, hxL = 0, hxR = 0, ph = 1, hL = 0, hR = 0, dg = 1, dust = 0; int dLeft = 0;
    const double dustK = std::pow (0.6, REFSR / SR), rrate = SR / REFSR;
    for (int i = 0; i < n; ++i)
    {
        const double A = age[(size_t) i]; double a = XL[(size_t) i], b = XR[(size_t) i];
        if (r() < W[Dust] * A * 80 / SR) dust = (r() * 2 - 1) * 0.25 * env[(size_t) i] / (mx > 0 ? mx : 1);
        a += dust; b += dust * 0.8; dust *= dustK;
        const double e = clampd (W[Erode] * A * 3, 0, 1);
        if (e > 0)
        {
            const double al = 1 - std::exp (-2 * PI * (16000 * std::pow (2.0, -W[Erode] * A * 3.4)) / SR);
            const double ah = std::exp (-2 * PI * (20 * std::pow (2.0, W[Erode] * A * 4)) / SR);
            lpL += al * (a - lpL); lpR += al * (b - lpR);
            const double yl = ah * (hpL + lpL - hxL), yr = ah * (hpR + lpR - hxR); hxL = lpL; hxR = lpR; hpL = yl; hpR = yr;
            a += (yl - a) * e; b += (yr - b) * e;
        }
        else { lpL = a; lpR = b; hxL = a; hxR = b; hpL = 0; hpR = 0; }
        const double H = 1 + W[Crush] * A * 24 * rrate;
        if (H > 1.001) { ph += 1 / H; if (ph >= 1) { ph -= 1; hL = a; hR = b; } a = hL; b = hR; } else { ph = 1; hL = a; hR = b; }
        const double bits = 24 - W[Bits] * A * 20;
        if (bits < 20) { const double q = std::pow (2.0, 1 - bits); a = jround (a / q) * q; b = jround (b / q) * q; }
        if (dLeft > 0) dLeft--; else if (r() < W[Drop] * A * 15 / SR) dLeft = (int) std::floor ((5 + r() * 55) * ms);
        dg += ((dLeft > 0 ? 0.03 : 1) - dg) * dgk;
        L[(size_t) i] = (float) (a * dg); R[(size_t) i] = (float) (b * dg);
    }
}

/* ---------- grains ---------- */
void Engine::fillGrains (std::vector<float>& L, std::vector<float>& R, const Layer& lay, Slot& slot, const std::vector<int>& valid, Rng& r, const Genome& g, const Mods& m) const
{
    const double I = P.instability; const auto& an = slot.an; const int win = an.win, len = (int) slot.data.size(), N = (int) L.size(), K = lay.key;
    const int startS = (int) std::floor (lay.start * SR / 1000);
    const double decayS = effDecay (lay, g) * SR / 1000;
    const double grainMs = lay.grain * std::pow (2.0, glideMod (g, K, GGrain, startS) * 1.2);
    const int lastStrike = m.strikes.empty() ? -1 : m.strikes.back().t;
    const int endS = std::min (N, std::max (startS + lifeS (decayS), lastStrike + (int) std::floor (decayS * 0.8)));
    const double srcGain = std::min (1000.0, 0.3 / std::max (an.ref, 1e-6));
    double pos = lay.pos; const bool trans = lay.role == Role::Transient; double ampNorm = 1;
    auto place = [&] (int t0, double boost)
    {
        const double spread = lay.spread + std::abs (Hv (g, Jitter, t0)) * I * 0.12;
        // Scatter: this grain or hit may come from anywhere in the usable parts of the sample, not just near the layer's spot
        const bool jump = P.scatter > 0 && r() < P.scatter;
        const double idx = jump ? r() * 0.9999 : clampd (std::fmod (pos + glideMod (g, K, GPosition, t0) * 0.25 + modAt (m.position, t0) * 0.3 + 2, 1.0) + gauss (r) * spread, 0, 0.9999);
        const int w = valid[(size_t) std::floor (idx * valid.size())];
        const double gms = lay.grain * std::pow (2.0, glideMod (g, K, GGrain, t0) * 1.2);
        int gl = std::max (16, (int) std::floor (gms * SR / 1000 * (0.7 + 0.6 * r())));
        const double pitchNow = lay.pitch + Hv (g, Drift, t0) * I * 1.5 + glideMod (g, K, GPitch, t0) * 3;
        const double rate = std::pow (2.0, (pitchNow + modAt (m.pitch, t0) * 4 + lay.pjit * (r() * 2 - 1)) / 12);
        const double revP = clampd (lay.rev + Hv (g, Reverse, t0) * I * 0.35 + glideMod (g, K, GReverse, t0) * 0.4, 0, 1);
        const bool rev = ! trans && r() < revP;
        double sp = trans ? std::max (0.0, (double) w * win - win * 0.5) : (double) w * win + r() * win;
        if (rev) sp += gl * rate * 0.5;
        const double width = clampd (lay.width + Hv (g, Width, t0) * I * 0.4, 0, 1), panC = clampd (lay.pan + glideMod (g, K, GPan, t0) * 0.7, -1, 1);
        const double pan = clampd (panC + width * (r() * 2 - 1), -1, 1);
        const double ang = (pan + 1) * PI / 4, gL = std::cos (ang), gR = std::sin (ang);
        const double amp = ampNorm * srcGain * (0.75 + 0.5 * r()) * boost;
        if (boost == 0) return;
        const double dir = rev ? -rate : rate;
        if (sp < 1) sp = 1; if (sp > len - 3) sp = len - 3;
        const int room = (int) std::floor (dir > 0 ? (len - 3 - sp) / rate : (sp - 1) / rate);
        gl = std::min (gl, room);
        if (gl < 16) return;
        const int atk = std::max (2, (int) std::floor (SR * 0.0005)); const int h = levelFor (rate);
        const Level& lv = slot.mip->level (h);
        if (trans && gl < atk * 2) return;
        const int n = std::min (gl, N - t0);
        for (int i = 0; i < n; ++i)
        {
            const double env = trans ? (i < atk ? (double) i / atk : 0.5 + 0.5 * std::cos (PI * (i - atk) / (gl - atk))) : 0.5 - 0.5 * std::cos (2 * PI * i / gl);
            const double v = lvRead (lv, sp + i * dir) * env * amp;
            L[(size_t) (t0 + i)] += (float) (v * gL); R[(size_t) (t0 + i)] += (float) (v * gR);
        }
    };
    if (trans)
    {
        const int c = std::max (1, lay.count);
        for (int k = 0; k < c; ++k) { const int t0 = startS + (k ? (int) std::floor (r() * 0.008 * SR) : 0); place (t0, 1); }
    }
    else
    {
        auto densAt = [&] (double t) { return lay.dens * std::pow (2.0, Hv (g, Density, t) * I * 1.2 + glideMod (g, K, GDensity, t) * 1.2); };
        ampNorm = 1 / std::sqrt (std::max (1.0, densAt (startS) * grainMs / 1000));
        int t = startS;
        Rng rs (lay.seed ^ 0x9a3f5u); const double starve = P.rot * g.rot[Starve];
        while (t < endS)
        {
            const bool starved = starve > 0 && rs() < starve * std::pow ((double) (t - startS) / std::max (1, endS - startS), 1.5);
            place (t, starved ? 0 : 1);
            pos = std::fmod (pos + gauss (r) * 0.01 + 1, 1.0);
            const double dt = densAt (t) * std::pow (2.0, modAt (m.density, t) * 2);
            t += std::max (1, (int) std::floor (SR / dt * (0.4 + 1.2 * r())));
        }
    }
    for (const auto& s : m.strikes)
    {
        const int k = 1 + (int) jround (s.level * 3);
        for (int c = 0; c < k; ++c) { const int t0 = s.t + (int) std::floor (r() * 0.006 * SR); place (t0, 1.2); }
    }
}

/* ---------- generators ---------- */
struct NoiseGen
{
    NoiseType type; Rng r; double SR;
    double q, wn, p0, p1, p2, p3, p4, g0, g1, g2, g3, g4, kb, gb, cP, cK, vP, vA;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, br = 0, cr = 0;
    NoiseGen (NoiseType t, uint32_t seed, double sr) : type (t), r (seed), SR (sr)
    {
        q = 44100 / SR; wn = std::sqrt (SR / 44100);
        auto Pw = [&] (double x) { return std::pow (x, q); }; auto Gw = [&] (double g, double p) { return g * (1 - Pw (p)) / (1 - p); };
        p0 = Pw (0.99886); p1 = Pw (0.99332); p2 = Pw (0.969); p3 = Pw (0.8665); p4 = Pw (0.55);
        g0 = Gw (0.0555179, 0.99886); g1 = Gw (0.0750759, 0.99332); g2 = Gw (0.153852, 0.969); g3 = Gw (0.3104856, 0.8665); g4 = Gw (0.5329522, 0.55);
        kb = std::pow (1 / 1.02, q); gb = (0.02 / 1.02) * (1 - kb) / (1 - 1 / 1.02);
        cP = 0.0012 * q; cK = std::pow (0.82, q); vP = 0.004 * q; vA = 0.8 / q;
    }
    NoiseGen (NoiseType t, Rng rng, double sr) : NoiseGen (t, 0u, sr) { r = rng; }
    double operator()()
    {
        const double w = (r() * 2 - 1) * wn;
        switch (type)
        {
            case NoiseType::White: return w * 0.5;
            case NoiseType::Pink: { b0 = p0 * b0 + w * g0; b1 = p1 * b1 + w * g1; b2 = p2 * b2 + w * g2; b3 = p3 * b3 + w * g3; b4 = p4 * b4 + w * g4; b5 = -0.7616 * b5 - w * 0.016898; const double pk = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926; return pk * 0.11; }
            case NoiseType::Brown: br = kb * br + gb * w; return br * 3.5;
            case NoiseType::Crackle: if (r() < cP) cr = r() * 2 - 1; else cr *= cK; return cr * 0.9;
            case NoiseType::Velvet: return r() < vP ? (r() < 0.5 ? -vA : vA) : 0;
        }
        return 0;
    }
};

void Engine::fillGenerator (std::vector<float>& L, std::vector<float>& R, const Layer& lay, Rng& r, const Genome& g, const Mods& m) const
{
    const int N = (int) L.size(), startS = (int) std::floor (lay.start * SR / 1000); const double decayS = effDecay (lay, g) * SR / 1000;
    const int lastStrike = m.strikes.empty() ? -1 : m.strikes.back().t;
    const int endS = std::min (N, std::max (startS + lifeS (decayS), lastStrike + (int) std::floor (decayS * 0.8)));
    const double ka = SR / REFSR;
    if (lay.kind == Kind::Click) for (const auto& s : m.strikes) if (s.t < N && s.t >= 0) { L[(size_t) s.t] += (float) (0.9 * s.level * ka); R[(size_t) s.t] += (float) (0.9 * s.level * ka); }
    const double ang = (lay.pan + 1) * PI / 4, gL = std::cos (ang), gR = std::sin (ang);
    if (lay.kind == Kind::Noise)
    {
        NoiseGen a (P.noiseType, lay.seed, SR), b (P.noiseType, lay.seed ^ 0x5bd1e995u, SR);
        for (int i = startS; i < endS; ++i) { const double x = a(), y = b(); L[(size_t) i] += (float) x; R[(size_t) i] += (float) lerp (x, y, lay.width); }
    }
    else if (lay.kind == Kind::Click)
    {
        if (startS < N) { L[(size_t) startS] += (float) (0.9 * ka); R[(size_t) startS] += (float) (0.9 * ka); }
        const int bl = (int) std::floor (SR * 0.0015 * (0.6 + r()));
        const double wn = std::sqrt (SR / REFSR);
        for (int i = 0; i < bl && startS + i < N; ++i) { const double e = std::exp (-i / (bl * 0.25)); L[(size_t) (startS + i)] += (float) ((r() * 2 - 1) * 0.7 * e * wn); R[(size_t) (startS + i)] += (float) ((r() * 2 - 1) * 0.7 * e * wn); }
    }
    else if (lay.kind == Kind::Blip)
    {
        const double f0 = lay.f0 * std::pow (2.0, lay.pitch / 12); double ph = 0;
        for (int i = startS; i < endS; ++i)
        {
            const double t = (double) (i - startS) / SR;
            ph += 2 * PI * f0 * (1 + 2.5 * std::exp (-t / 0.012)) / SR;
            const double s = std::sin (ph + 3 * std::exp (-t / 0.05) * std::sin (ph * 1.5)) * 0.5;
            L[(size_t) i] += (float) (s * gL); R[(size_t) i] += (float) (s * gR);
        }
    }
}

/* ---------- low pass gate ---------- */
void Engine::applyLPG (std::vector<float>& L, std::vector<float>& R, const Layer& lay, Rng& r, const Genome& g, const Mods& m) const
{
    const double I = P.instability; const int N = (int) L.size();
    const int startS = (int) std::floor (lay.start * SR / 1000);
    const auto& st = m.strikes; const int lastS = st.empty() ? -1 : st.back().t;
    const int openS = st.empty() ? startS : std::min (startS, st.front().t);
    const double tau = effDecay (lay, g) / 1000 / 6.9;
    const double a = lay.curve, tf = tau * 0.35, ts = tau * 1.5, ta = lerpd (0.0008 + r() * 0.002, std::max (0.0008, P.shA / 1000 / 3), P.shFollow);
    double beL, beT; bodyEff (beL, beT);
    const double bodyL = lay.role == Role::Transient || lay.kind == Kind::Click ? 0 : beL, bodyTau = beT / 6.9;
    const double fbase = (150 + 17000 * lay.bright * lay.bright) * P.bright;
    auto fmaxAt = [&] (double i) { return clampd (fbase * std::pow (2.0, Hv (g, Tilt, i) * I * 1.5 + glideMod (g, lay.key, GBright, i) * 1.5), 200, 0.45 * SR); };
    auto openOf = [] (double gg) { const double fc = std::atan (gg) / PI; const double u = clampd ((fc - 0.32) / 0.13, 0, 1); return u * u * (3 - 2 * u); };
    const double fmin = 40, res = clampd (0.5 * lay.reso + P.reso * 0.8, 0, 0.96), k = 2 - 2 * res;
    const double wob = (0.5 + 0.5 * Hv (g, Wobble, startS)) * I;
    std::vector<double> pts; for (int j = 0; j < (int) std::ceil ((double) N / SR / 0.03) + 3; ++j) pts.push_back (r() * 2 - 1);
    auto smooth = [&] (double t) { const double x = t / 0.03; const int j = (int) x; const double f = x - j, c = 0.5 - 0.5 * std::cos (PI * f); return lerp (pts[(size_t) j], pts[(size_t) j + 1], c); };
    auto envAt = [&] (int i)
    {
        const double t = (double) (i - startS) / SR;
        double v = t >= 0 ? std::max (a * std::exp (-t / tf) + (1 - a) * std::exp (-t / ts), bodyL * std::exp (-t / bodyTau)) * (1 - std::exp (-t / ta)) : 0;
        for (const auto& s : st) { if (s.t > i) break; const double u = (double) (i - s.t) / SR; const double vs = s.level * (a * std::exp (-u / (tf * 0.6)) + (1 - a) * std::exp (-u / (ts * 0.6))) * (1 - std::exp (-u / ta)); if (vs > v) v = vs; }
        return v;
    };
    auto gOf = [&] (double v, int i) { return std::tan (PI * std::min (0.45 * SR, fmin * std::pow (fmaxAt (i) / fmin, v) * std::pow (2.0, wob * 0.6 * smooth (std::max (0.0, (double) (i - startS) / SR)) + modAt (m.bright, i) * 2)) / SR); };
    for (int i = 0; i < std::min (N, openS); ++i) { L[(size_t) i] = 0; R[(size_t) i] = 0; }
    double ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0;
    double v0 = envAt (openS), g0 = gOf (v0, openS), am0 = std::pow (v0, 1.15), op0 = openOf (g0);
    for (int i = std::max (0, openS); i < N; i += 8)
    {
        const double v1 = envAt (i + 8), g1 = gOf (v1, i + 8), am1 = std::pow (v1, 1.15), op1 = openOf (g1);
        if (v0 < 2e-5 && v1 < 2e-5 && (double) (i - startS) / SR > 0.05 && i > lastS) { std::fill (L.begin() + i, L.end(), 0.f); std::fill (R.begin() + i, R.end(), 0.f); break; }
        const int end = std::min (8, N - i);
        for (int s = 0; s < end; ++s)
        {
            const double f = s / 8.0, gg = g0 + (g1 - g0) * f, amp = am0 + (am1 - am0) * f, op = op0 + (op1 - op0) * f; const int j = i + s;
            const double xl = L[(size_t) j], xr = R[(size_t) j];
            const double a1 = 1 / (1 + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
            double v3 = xl - ic2L, w1 = a1 * ic1L + a2 * v3, w2 = ic2L + a2 * ic1L + a3 * v3;
            ic1L = 2 * w1 - ic1L; ic2L = 2 * w2 - ic2L; L[(size_t) j] = (float) ((w2 + (xl - w2) * op) * amp);
            v3 = xr - ic2R; w1 = a1 * ic1R + a2 * v3; w2 = ic2R + a2 * ic1R + a3 * v3;
            ic1R = 2 * w1 - ic1R; ic2R = 2 * w2 - ic2R; R[(size_t) j] = (float) ((w2 + (xr - w2) * op) * amp);
        }
        v0 = v1; g0 = g1; am0 = am1; op0 = op1;
    }
    if (! m.duck.empty()) for (int i = 0; i < N; ++i) { const double kk = 1 - clampd (modAt (m.duck, i), 0, 0.95); L[(size_t) i] = (float) (L[(size_t) i] * kk); R[(size_t) i] = (float) (R[(size_t) i] * kk); }
}

/* ---------- pitch envelope ---------- */
std::vector<float> Engine::applyWarp (const std::vector<float>& X, const std::vector<double>& pos, const std::vector<float>& rateV) const
{
    const int N = (int) X.size(); std::vector<float> Y ((size_t) N); Mip m (X.data(), N);
    std::map<int, std::pair<double, double>> span;
    for (int i = 0; i < N; ++i)
    {
        const double p = pos[(size_t) i]; if (p >= N - 3) break; const double r = rateV[(size_t) i]; if (r <= 1) continue;
        const double t = 4 * std::log2 (r); const int h = (int) std::floor (t);
        auto add = [&] (int k) { auto it = span.find (k); if (it == span.end()) span[k] = { p, p }; else { if (p < it->second.first) it->second.first = p; if (p > it->second.second) it->second.second = p; } };
        add (h); if (t - h > 0.85) add (h + 1);
    }
    for (const auto& s : span) { const double D = std::pow (2.0, std::floor (s.first / 4.0)); m.level (s.first, s.second.first - 40 * D, s.second.second + 40 * D); }
    for (int i = 0; i < N; ++i) { const double p = pos[(size_t) i]; if (p >= N - 3) break; Y[(size_t) i] = (float) m.read (p, rateV[(size_t) i]); }
    return Y;
}

/* ---------- resonator ---------- */
void Engine::applyResonator (std::vector<float>& ML, std::vector<float>& MR, const Genome& g) const
{
    if (P.resType == ResType::Off || P.resMix <= 0) return;
    const int N = (int) ML.size(); const double note = P.resPitch + g.resJit * P.resRand;
    const double f0 = 440 * std::pow (2.0, (note - 69) / 12), T60 = P.resT60(); const int voices = (int) jround (P.resVoices);
    std::vector<float> WL ((size_t) N), WR ((size_t) N);
    double dryPk = 0; for (int i = 0; i < N; ++i) dryPk = std::max ({ dryPk, (double) std::abs (ML[(size_t) i]), (double) std::abs (MR[(size_t) i]) });
    if (dryPk <= 0) return;
    if (P.resType == ResType::Modal)
    {
        std::vector<double> q;
        switch (P.resStruct)
        {
            case ResStruct::Harmonic: q = { 1, 2, 3, 4, 5, 6 }; break;
            case ResStruct::Bar: q = { 1, 2.756, 5.404, 8.933, 13.344, 18.64 }; break;
            case ResStruct::Bell: q = { 0.5, 1, 1.183, 1.506, 2, 2.514 }; break;
            case ResStruct::Plate: q = { 1, 1.59, 2.14, 2.3, 2.65, 2.92 }; break;
            case ResStruct::Random: { Rng r (g.seed ^ 0x2f6b1du); q = { 1 }; for (int j = 1; j < 6; ++j) q.push_back (1 + r() * 7); std::sort (q.begin(), q.end()); break; }
        }
        for (int j = 0; j < voices; ++j)
        {
            const double ratio = q[(size_t) j % q.size()] * (j >= (int) q.size() ? 2 : 1), amp = 1 / (1 + j * 0.35);
            const double t60 = T60 / (1 + j * P.resDamp * 1.5), rr0 = std::pow (10.0, -3 / (SR * t60));
            for (int side = -1; side <= 1; side += 2)
            {
                auto& X = side < 0 ? ML : MR; auto& W = side < 0 ? WL : WR;
                const double f = f0 * ratio * std::pow (2.0, side * (j % 2 ? 1 : -1) * P.resSpread / 1200);
                if (f > SR * 0.45) continue;
                const double w = 2 * PI * f / SR, c1 = 2 * rr0 * std::cos (w), c2 = -rr0 * rr0, gn = (1 - rr0) * amp;
                double y1 = 0, y2 = 0;
                for (int i = 0; i < N; ++i) { const double y = gn * X[(size_t) i] + c1 * y1 + c2 * y2; y2 = y1; y1 = y; W[(size_t) i] += (float) y; }
            }
        }
    }
    else
    {
        const double damp = 1 - P.resDamp * 0.85;
        for (int v = 0; v < voices; ++v)
        {
            const double det = voices > 1 ? ((double) v / (voices - 1) - 0.5) * 2 * P.resSpread : 0;
            const double f = f0 * std::pow (2.0, det / 1200), period = SR / f;
            const double dl = period - (1 - damp) / damp;
            if (dl < 3) continue;
            const int M = (int) std::floor (dl - 0.5); const double eta = dl - M, ac = (1 - eta) / (1 + eta);
            const double fb = std::pow (10.0, -3 * period / (SR * T60));
            const int size = M + 2; const double pan = voices > 1 ? (double) v / (voices - 1) : 0.5;
            for (int side = 0; side < 2; ++side)
            {
                auto& X = side == 0 ? ML : MR; auto& W = side == 0 ? WL : WR; const double gain = side == 0 ? 1 - pan * 0.7 : 0.3 + pan * 0.7;
                std::vector<double> line ((size_t) size); int wp = 0; double lp = 0, ax = 0, ay = 0;
                for (int i = 0; i < N; ++i)
                {
                    int rp = wp - M; if (rp < 0) rp += size;
                    const double del = line[(size_t) rp];
                    const double ap = ac * del + ax - ac * ay; ax = del; ay = ap;
                    lp += damp * (ap - lp);
                    const double y = X[(size_t) i] * 0.3 + fb * lp;
                    line[(size_t) wp] = y; wp = (wp + 1) % size;
                    W[(size_t) i] += (float) (y * gain);
                }
            }
        }
    }
    double wetPk = 0; for (int i = 0; i < N; ++i) wetPk = std::max ({ wetPk, (double) std::abs (WL[(size_t) i]), (double) std::abs (WR[(size_t) i]) });
    if (wetPk <= 0) return;
    const double kk = dryPk / wetPk, mx = P.resMix;
    for (int i = 0; i < N; ++i) { ML[(size_t) i] = (float) (ML[(size_t) i] * (1 - mx) + WL[(size_t) i] * kk * mx); MR[(size_t) i] = (float) (MR[(size_t) i] * (1 - mx) + WR[(size_t) i] * kk * mx); }
}

/* ---------- distortion (2× oversampled) ---------- */
std::function<double (double)> Engine::distFn() const { return distFnOf (P); }
void Engine::phaseDistort (std::vector<float>& u, double SR2) const { phaseDistortOf (P, u, SR2); }

std::function<double (double)> distFnOf (const Settings& P)
{
    const auto t = P.distType; const double gIn = std::pow (10.0, P.distDrive / 20);
    if (t == DistType::Sat)
    {
        const double n = 1 + P.satKnee * 9, b = P.satAsym * 0.6;
        auto sc = [n] (double v) { return v / std::pow (1 + std::pow (std::abs (v), n), 1 / n); };
        const double off = sc (b);
        return [=] (double x) { return sc (x * gIn + b) - off; };
    }
    if (t == DistType::Shape)
    {
        const double e = std::pow (2.0, -P.shCurve * 2), h2 = P.shH2, h3 = P.shH3, h5 = P.shH5, norm = 1 + h2 + h3 + h5; const int st = (int) jround (P.shSteps);
        return [=] (double x)
        {
            double u = std::tanh (x * gIn);
            if (st >= 2) u = jround (u * st) / st;
            const double a = sgn (u) * std::pow (std::abs (u), e), u2 = u * u;
            return (a + h2 * 2 * u2 + h3 * (4 * u2 * u - 3 * u) + h5 * (16 * u2 * u2 * u - 20 * u2 * u + 5 * u)) / norm;
        };
    }
    if (t == DistType::Fold)
    {
        const double k = P.fdFolds, b = P.fdBias, sh = P.fdShape;
        const double off = (1 - sh) * (2 / PI) * std::asin (std::sin (PI / 2 * b * k)) + sh * std::sin (PI / 2 * b * k);
        return [=] (double x) { const double v = (x * gIn * 0.5 + b) * k; return (1 - sh) * (2 / PI) * std::asin (std::sin (PI / 2 * v)) + sh * std::sin (PI / 2 * v) - off; };
    }
    return [] (double x) { return x; };
}

void phaseDistortOf (const Settings& P, std::vector<float>& u, double SR2)
{
    const int M = (int) u.size(); const double depth = P.pdDepth * 0.003 * SR2 * std::pow (10.0, P.distDrive / 20) / 4, D0 = depth + 4;
    const int size = (int) std::ceil (2 * depth + 16); std::vector<float> line ((size_t) size); const double fb = P.pdFb;
    const double al = 1 - std::exp (-2 * PI * (40 * std::pow (200.0, P.pdSmooth)) / SR2);
    int w = 0; double m = 0, last = 0;
    for (int i = 0; i < M; ++i)
    {
        m += al * (u[(size_t) i] - m);
        line[(size_t) w] = (float) (u[(size_t) i] + fb * last);
        double rp = w - (D0 + depth * m); while (rp < 0) rp += size;
        const int ri = (int) rp; const double f = rp - ri;
        const double a0 = line[(size_t) ((ri - 1 + size) % size)], a1 = line[(size_t) (ri % size)], a2 = line[(size_t) ((ri + 1) % size)], a3 = line[(size_t) ((ri + 2) % size)];
        const double c1 = 0.5 * (a2 - a0), c2 = a0 - 2.5 * a1 + 2 * a2 - 0.5 * a3, c3 = 0.5 * (a3 - a0) + 1.5 * (a1 - a2);
        last = ((c3 * f + c2) * f + c1) * f + a1;
        u[(size_t) i] = (float) last; w = (w + 1) % size;
    }
}

void Engine::applyDistortion (std::vector<float>& ML, std::vector<float>& MR) const
{
    if (P.distType == DistType::Off || P.distMix <= 0) return;
    const int N = (int) ML.size();
    double pk = 0; for (int i = 0; i < N; ++i) pk = std::max ({ pk, (double) std::abs (ML[(size_t) i]), (double) std::abs (MR[(size_t) i]) });
    if (pk <= 0) return;
    const double pre = 0.9 / pk;
    const auto fn = distFn();
    for (auto* X : { &ML, &MR })
    {
        std::vector<float> xin ((size_t) N); for (int i = 0; i < N; ++i) xin[(size_t) i] = (float) ((*X)[(size_t) i] * pre);
        auto u = upsample2 (xin);
        if (P.distType == DistType::Phase) phaseDistort (u, SR * 2);
        else for (auto& v : u) v = (float) fn (v);
        const auto y = downsample2 (u);
        const double fc = 600 * std::pow (36.0, P.distTone), al = 1 - std::exp (-2 * PI * std::min (fc, 0.45 * SR) / SR), mx = P.distMix;
        double lp = 0;
        for (int i = 0; i < N; ++i)
        {
            const double yi = i < (int) y.size() ? y[(size_t) i] : 0;
            lp += al * (yi - lp); const double wet = P.distTone >= 0.999 ? yi : lp;
            (*X)[(size_t) i] = (float) ((xin[(size_t) i] * (1 - mx) + wet * mx) / pre);
        }
    }
}

/* ---------- rendering ---------- */
double Engine::tpNormalize (std::vector<float>& bl, std::vector<float>& br, int end) const
{
    // true peak: every sample, plus 3 interpolated points between samples. Interpolation is only evaluated where
    // the samples around it are loud enough to beat the peak found so far (an exact shortcut: the sinc can't add more
    // than the sum of its tap magnitudes, under 2.3×), so the result is the same as checking everywhere.
    double tp = 0;
    for (auto* ch : { &bl, &br }) for (int i = 0; i < end; ++i) tp = std::max (tp, (double) std::abs ((*ch)[(size_t) i]));
    constexpr int B = 64; const double gainBound = 2.35;
    for (auto* ch : { &bl, &br })
        for (int b0 = 0; b0 < end - 2; b0 += B)
        {
            double lm = 0; for (int i = std::max (0, b0 - 16); i < std::min (end, b0 + B + 18); ++i) lm = std::max (lm, (double) std::abs ((*ch)[(size_t) i]));
            if (lm * gainBound <= tp) continue;
            for (int i = b0; i < std::min (b0 + B, end - 2); ++i)
                for (double q : { 0.25, 0.5, 0.75 }) tp = std::max (tp, std::abs (sread (ch->data(), end, i + q)));
        }
    const double k = 0.89 / (tp > 0 ? tp : 1e-9);
    for (int i = 0; i < end; ++i) { bl[(size_t) i] = (float) (bl[(size_t) i] * k); br[(size_t) i] = (float) (br[(size_t) i] * k); }
    return k;
}

Features Engine::features (const std::vector<float>& L, const std::vector<float>& R, int n) const
{
    const int w = (int) std::floor (SR * 0.005); std::vector<double> wr; double pw = 0;
    for (int o = 0; o + w <= n; o += w) { double s = 0; for (int i = o; i < o + w; ++i) { const double v = ((double) L[(size_t) i] + R[(size_t) i]) * 0.5; s += v * v; } const double x = std::sqrt (s / w); wr.push_back (x); pw = std::max (pw, x); }
    double early = 0; for (int j = 0; j < std::min (6, (int) wr.size()); ++j) early = std::max (early, wr[(size_t) j]);
    double e = 0, d = 0, side = 0, prev = 0, tailE = 0, tot = 0; const int t300 = (int) std::floor (0.3 * SR);
    for (int i = 0; i < n; ++i)
    {
        const double mo = ((double) L[(size_t) i] + R[(size_t) i]) * 0.5, sd = ((double) L[(size_t) i] - R[(size_t) i]) * 0.5;
        e += mo * mo; d += (mo - prev) * (mo - prev); prev = mo; side += sd * sd;
        const double q = (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; tot += q; if (i > t300) tailE += q;
    }
    int rises = 0; for (int j = 1; j < (int) wr.size(); ++j) if (wr[(size_t) j] > wr[(size_t) j - 1] * 1.5 && wr[(size_t) j] > pw * 0.1) rises++;
    Features f; f.attack = pw > 0 ? early / pw : 0; f.len = n / SR; f.bright = e > 0 ? std::sqrt (d / e) : 0; f.irreg = rises; f.stereo = e > 0 ? side / e : 0; f.tail = tot > 0 ? tailE / tot : 0;
    f.shape = shapeScore (L, R, n);
    return f;
}

std::shared_ptr<const Out> Engine::render (Genome& g)
{
    syncGenome (g);
    const int N = (int) std::ceil (P.maxLen * SR);
    // pitch envelope as a precomputed read position
    std::vector<double> wpos; std::vector<float> wrate; const bool warp = std::abs (P.pitchAmt) >= 0.05;
    if (warp)
    {
        wpos.resize ((size_t) N); wrate.resize ((size_t) N);
        const double tau = P.pitchTime / 1000; double p = 0;
        for (int i = 0; i < N; ++i) { wpos[(size_t) i] = p; const double e = std::pow (std::exp (-((double) i / SR) / tau), P.pitchCurve); wrate[(size_t) i] = (float) std::pow (2.0, P.pitchAmt * e / 12); p += wrate[(size_t) i]; }
    }
    chaosTrack (g, N);

    const auto avail = sources();
    struct Done { std::vector<float> L, R; std::vector<float> fol; bool hasFol = false; };
    std::vector<float> ML, MR; std::vector<double> wr; double pw = 0, peak = 0; std::vector<int> skipped; int nLayers = 0; bool ok = false, empty = false;
    int attempt = 0;
    for (attempt = 0; attempt < 6; ++attempt)
    {
        skipped.clear(); nLayers = 0;
        std::map<int, Done> done; std::vector<std::pair<int, double>> outs;   // key, gain
        auto layers = g.layers;
        auto oidx = [&] (int k) { auto it = std::find (g.order.begin(), g.order.end(), k); return it == g.order.end() ? -1 : (int) (it - g.order.begin()); };
        std::stable_sort (layers.begin(), layers.end(), [&] (const Layer& a, const Layer& b) { return oidx (a.key) < oidx (b.key); });
        for (const auto& lay : layers)
        {
            if (abort && abort->load()) return nullptr;
            const auto sit = std::find_if (avail.begin(), avail.end(), [&] (const Src& s) { return s.key == lay.key; });
            if (sit == avail.end()) continue;
            Rng r ((uint32_t) (lay.seed + (uint32_t) attempt * 0x9E3779B1u));
            // modulation from layers already rendered
            Mods m; const double fac = std::min (1.0, P.interact * 1.6);
            for (const auto& l : g.links)
            {
                if (l.to != lay.key) continue; auto dit = done.find (l.from); if (dit == done.end()) continue;
                auto& d = dit->second; if (! d.hasFol) { d.fol = follower (d.L, d.R, SR); d.hasFol = true; }
                if (l.type == LinkType::Clock) { for (int j : onsetsOf (d.fol, SR)) m.strikes.push_back ({ j * FSTEP, clampd (std::abs (l.amount) * fac, 0.05, 1) * d.fol[(size_t) j] }); }
                else
                {
                    auto* list = l.dest == LDensity ? &m.density : l.dest == LPosition ? &m.position : l.dest == LPitch ? &m.pitch : l.dest == LBright ? &m.bright : &m.duck;
                    list->push_back ({ &d.fol, l.amount * fac });
                }
            }
            std::stable_sort (m.strikes.begin(), m.strikes.end(), [] (const Mods::Strike& a, const Mods::Strike& b) { return a.t < b.t; });
            { std::vector<Mods::Strike> f; for (size_t i = 0; i < m.strikes.size(); ++i) if (i == 0 || m.strikes[i].t - m.strikes[i - 1].t > SR * 0.015) f.push_back (m.strikes[i]); m.strikes = f; }
            if (lay.kind != Kind::Click) addGasps (m, lay, g);
            std::vector<float> L ((size_t) N), R ((size_t) N); double gm = lay.gain;
            if (lay.kind == Kind::Slot)
            {
                auto& sl = slots[(size_t) lay.key];
                const auto valid = validChain (sl.an, lay.role);
                if (valid.empty()) { skipped.push_back (lay.key); continue; }
                fillGrains (L, R, lay, sl, valid, r, g, m);
            }
            else
            {
                fillGenerator (L, R, lay, r, g, m);
                if (lay.kind == Kind::Noise) gm *= P.noiseLevel * 2;
                if (lay.kind == Kind::Click) gm *= 0.7;
            }
            // a slow attack asks for no hard hit: transients step back
            if ((lay.role == Role::Transient || lay.kind == Kind::Click) && P.shA > 15) gm *= lerpd (1, std::max (0.12, 15 / P.shA), P.shFollow);
            applyLPG (L, R, lay, r, g, m);
            done[lay.key] = { std::move (L), std::move (R), {}, false };
            outs.push_back ({ lay.key, gm });
        }
        if (outs.empty()) { empty = true; break; }
        nLayers = (int) outs.size();
        ML.assign ((size_t) N, 0.f); MR.assign ((size_t) N, 0.f);
        for (auto& o : outs) { const auto& d = done[o.first]; for (int i = 0; i < N; ++i) { ML[(size_t) i] += (float) (d.L[(size_t) i] * o.second); MR[(size_t) i] += (float) (d.R[(size_t) i] * o.second); } }
        if (warp) { ML = applyWarp (ML, wpos, wrate); MR = applyWarp (MR, wpos, wrate); }
        applyResonator (ML, MR, g);
        applyDistortion (ML, MR);
        double xl = 0, yl = 0, xr = 0, yr = 0; peak = 0;
        for (int i = 0; i < N; ++i)
        {
            const double a = ML[(size_t) i], b = MR[(size_t) i];
            yl = a - xl + DC_R * yl; xl = a; ML[(size_t) i] = (float) yl;
            yr = b - xr + DC_R * yr; xr = b; MR[(size_t) i] = (float) yr;
            peak = std::max ({ peak, std::abs (yl), std::abs (yr) });
        }
        const int w5 = (int) std::floor (SR * 0.005); pw = 0; wr.clear();
        for (int o = 0; o + w5 <= N; o += w5) { double s = 0; for (int i = 0; i < w5; ++i) { const double v = ((double) ML[(size_t) (o + i)] + MR[(size_t) (o + i)]) * 0.5; s += v * v; } const double x = std::sqrt (s / w5); wr.push_back (x); pw = std::max (pw, x); }
        int active = 0; for (double x : wr) if (x > pw * 0.1) active++;
        ok = peak > 0.01 && active * 5 >= 20;
        if (ok) break;
    }
    if (empty) return nullptr;
    shapeCorrect (ML, MR, peak, wr, pw);
    if (attempt > 5) attempt = 5;
    if (peak <= 0) peak = 1e-9;
    // trim the dead tail; a sound still loud where it ends gets a long fade, one that already died gets 5 ms
    const double floorV = peak * std::pow (10.0, -66.0 / 20); int last = 0;
    for (int i = N - 1; i >= 0; --i) if (std::abs (ML[(size_t) i]) > floorV || std::abs (MR[(size_t) i]) > floorV) { last = i; break; }
    double tailPk = 0; for (int i = std::max (0, last - (int) std::floor (SR * 0.01)); i <= last; ++i) tailPk = std::max ({ tailPk, (double) std::abs (ML[(size_t) i]), (double) std::abs (MR[(size_t) i]) });
    // never shorter than 50 ms: a sound that is little more than its click still gets a body to play (and to see)
    const int minLen = std::min (N, (int) std::floor (0.05 * SR));
    const bool cut = tailPk > peak * 0.01 && last + 1 >= minLen;
    const int end = cut ? last + 1 : std::max (minLen, std::min (N, last + (int) std::floor (0.01 * SR)));
    const int fade = cut ? (int) std::floor (std::min (SR * 0.25, end * 0.25)) : (int) std::floor (SR * 0.005);
    auto o = std::make_shared<Out>();
    o->L.resize ((size_t) end); o->R.resize ((size_t) end);
    for (int i = 0; i < end; ++i)
    {
        const int k = end - i; const double f = k < fade ? 0.5 - 0.5 * std::cos (PI * k / fade) : 1;
        o->L[(size_t) i] = (float) ((i < N ? ML[(size_t) i] : 0) * f); o->R[(size_t) i] = (float) ((i < N ? MR[(size_t) i] : 0) * f);
    }
    tpNormalize (o->L, o->R, end);
    if (P.rot > 0)
    {
        applyRot (o->L, o->R, end, g);
        const int fz = (int) std::floor (SR * 0.005);
        for (int i = std::max (0, end - fz); i < end; ++i) { const double f = 0.5 - 0.5 * std::cos (PI * (end - i) / fz); o->L[(size_t) i] = (float) (o->L[(size_t) i] * f); o->R[(size_t) i] = (float) (o->R[(size_t) i] * f); }
        tpNormalize (o->L, o->R, end);
    }
    const int cols = 600; const double per = (double) end / cols;
    o->mixMin.assign (cols, 0.f); o->mixMax.assign (cols, 0.f);
    for (int c = 0; c < cols; ++c)
    {
        double mn = 0, mx = 0; const int a = (int) std::floor (c * per), b = std::max (a + 1, (int) std::floor ((c + 1) * per));
        for (int i = a; i < b && i < end; ++i) { const double v = ((double) o->L[(size_t) i] + o->R[(size_t) i]) * 0.5; mn = std::min (mn, v); mx = std::max (mx, v); }
        o->mixMin[(size_t) c] = (float) mn; o->mixMax[(size_t) c] = (float) mx;
    }
    int pi = 0; for (int j = 0; j < (int) wr.size(); ++j) if (wr[(size_t) j] == pw) { pi = j; break; }
    o->thalf = -1; for (int j = pi + 1; j < (int) wr.size(); ++j) if (wr[(size_t) j] < pw * 0.5) { o->thalf = (j - pi) * 5; break; }
    o->feat = features (o->L, o->R, end); o->end = end; o->sampleRate = SR; o->peakT = pi * 0.005; o->len = end / SR;
    o->rerolls = std::min (attempt, 5); o->skipped = skipped; o->layers = nLayers;
    return o;
}

/* ---------- deciding between candidates ---------- */
const Out* Engine::decideIdx (const std::vector<std::shared_ptr<const Out>>& cands, int& idx) const
{
    idx = 0;
    if (cands.size() == 1 || (P.target == Target::Any && P.shFollow < 0.01)) return cands[0].get();
    const Features* prev = current && current->out ? &current->out->feat : nullptr;
    auto fv = [] (const Features& f, int k) { const double v[] = { f.attack, f.len, f.bright, f.irreg, f.stereo, f.tail, f.nov, f.shape }; return v[k]; };
    std::vector<Features> F; for (auto& c : cands) F.push_back (c->feat);
    for (auto& f : F) { f.nov = 0; if (prev) for (int k = 0; k < 6; ++k) f.nov += std::abs (fv (f, k) - fv (*prev, k)) / (std::abs (fv (*prev, k)) + 0.05); }
    // weights: attack len bright irreg stereo tail nov shape
    double W[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    switch (P.target)
    {
        case Target::Punchy: W[0] = 2; W[1] = -1; W[5] = -1; break;
        case Target::Long: W[1] = 1.5; W[5] = 1.5; break;
        case Target::Bright: W[2] = 2; break;
        case Target::Dark: W[2] = -2; break;
        case Target::Wrong: W[3] = 1.5; W[4] = 1; W[6] = 1.5; break;
        default: break;
    }
    W[6] += 0.4;
    if (P.shFollow >= 0.01) W[7] = 2.5 * P.shFollow;   // closest to the Shape
    bool used[8]; for (int k = 0; k < 8; ++k) used[k] = W[k] != 0 || k == 6;
    double mu[8] {}, sd[8] {};
    for (int k = 0; k < 8; ++k)
    {
        if (! used[k]) continue;
        double s = 0; for (auto& f : F) s += fv (f, k); mu[k] = s / F.size();
        double v = 0; for (auto& f : F) v += (fv (f, k) - mu[k]) * (fv (f, k) - mu[k]); sd[k] = std::sqrt (v / F.size()); if (sd[k] == 0) sd[k] = 1;
    }
    double bs = -1e300;
    for (int c = 0; c < (int) F.size(); ++c) { double s = 0; for (int k = 0; k < 8; ++k) if (used[k]) s += W[k] * (fv (F[(size_t) c], k) - mu[k]) / sd[k]; if (s > bs) { bs = s; idx = c; } }
    return cands[(size_t) idx].get();
}

void Engine::commit (Genome g, const std::string& how, std::shared_ptr<const Out> out, int picked)
{
    auto e = std::make_shared<Entry>(); e->id = ++hid; e->genome = std::move (g); e->out = out; e->how = how;
    current = e; history.insert (history.begin(), e); if (history.size() > 16) history.pop_back();
    std::string msg = (how == "fresh" ? "Fresh" : how == "mutate" ? "Mutated" : "Links rerolled") + std::string (" · ") + std::to_string (out->layers) + " layers";
    if (picked > 1) { msg += " · picked 1 of " + std::to_string (picked); if (P.target != Target::Any) msg += std::string (" for ") + targetName (P.target); }
    if (out->rerolls) msg += " · rerolled " + std::to_string (out->rerolls) + "x (too quiet)";
    if (! out->skipped.empty()) { msg += " · skipped "; for (size_t i = 0; i < out->skipped.size(); ++i) msg += (i ? ", " : "") + keyName (out->skipped[i]); }
    status = msg;
}

bool Engine::generate (bool fresh)
{
    const int K = std::max (1, P.cands);
    std::vector<Genome> gs; std::vector<std::shared_ptr<const Out>> outs;
    for (int k = 0; k < K; ++k)
    {
        Genome g = fresh || ! current ? freshGenome() : mutGenome();
        auto out = render (g);
        if (out) { gs.push_back (std::move (g)); outs.push_back (out); }
    }
    if (outs.empty()) { status = "Nothing to render. Load a sample or turn on a generator."; return false; }
    int idx = 0; decideIdx (outs, idx);
    commit (gs[(size_t) idx], current || fresh ? (fresh ? "fresh" : "mutate") : "fresh", outs[(size_t) idx], (int) outs.size());
    return true;
}

bool Engine::relink()
{
    if (! current) return false;
    Genome g = current->genome; Rng r (newSeed());
    std::vector<int> keys; for (const auto& s : sources()) keys.push_back (s.key);
    auto net = makeLinks (keys, r); g.order = net.first; g.links = net.second;
    auto out = render (g);
    if (! out) return false;
    commit (g, "links", out, 1);
    return true;
}

bool Engine::rerender()
{
    if (! current) return false;
    auto e = std::make_shared<Entry> (*current);
    auto out = render (e->genome);
    if (! out) return false;
    e->out = out;
    for (auto& h : history) if (h == current) h = e;
    current = e;
    return true;
}

void Engine::tick (double dt)
{
    advanceNet ({ &HNET[0], &HNET[1], &HNET[2], &HNET[3] }, dt * P.chaosSpeed, P.chaosCouple);
    live = hvalsOf (HNET, HROUTE);
    advanceNet ({ &slots[0].glide.c, &slots[1].glide.c, &slots[2].glide.c, &slots[3].glide.c }, dt * P.chaosSpeed, P.chaosCouple);
    for (auto& s : slots) readGlide (s.glide);
}

void Engine::dcBlockNow (std::vector<float>& d) const { dcBlock (d, DC_R); }

void Engine::boot()
{
    Genome g; g.seed = 0x3F1A9C2Eu; Rng r (g.seed);
    std::vector<int> keys; for (const auto& s : sources()) keys.push_back (s.key);
    auto net = makeLinks (keys, r);
    g.hidden = live; g.glide = snapGlide(); g.chaos = snapChaos();
    for (const auto& s : sources()) g.layers.push_back (buildLayer (s, r));
    g.order = net.first; g.links = net.second; g.resJit = 0;
    { Rng rr0 (g.seed ^ 0x777u); g.rot = makeRot (rr0); }
    auto out = render (g);
    if (! out) return;
    auto e = std::make_shared<Entry>(); e->id = ++hid; e->genome = g; e->out = out; e->how = "fresh";
    current = e; history.insert (history.begin(), e);
}

void Engine::rerouteGlide (int i) { auto g = makeGlide (i); g.c = slots[(size_t) i].glide.c; readGlide (g); slots[(size_t) i].glide = g; }
void Engine::rerouteChaos() { HROUTE = makeHRoute (rnd); for (int i = 0; i < 4; ++i) rerouteGlide (i); }

void Engine::perishSources (bool poolUsable, const std::array<SlotRole, 4>&)
{
    auto pool = libNames();
    for (int k = (int) pool.size() - 1; k > 0; --k) std::swap (pool[(size_t) k], pool[(size_t) (int) (rnd() * (k + 1))]);
    for (int i = 0; i < 4; ++i)
    {
        auto& sl = slots[(size_t) i];
        if (! sl.pinned && ! poolUsable && (! sl.has() || sl.example)) loadBuiltin (i, pool[(size_t) i], false);
        rerouteGlide (i);
    }
    HROUTE = makeHRoute (rnd);
    locks.clear();
}

/* ---------- built-in library: procedural test sources ---------- */
const std::vector<std::string>& Engine::libNames()
{
    static const std::vector<std::string> n = { "Struck plate", "Metal clang", "Pipe hit", "Bell", "Noise burst", "White hiss", "Brown rumble", "Dust and crackle",
                                                "Radio static", "Gravel", "Glass drone", "Wood ticks", "Kick thump", "Snare", "Hats", "Rubber pluck" };
    return n;
}

std::vector<float> Engine::makeBuiltin (const std::string& name, double SRb) const
{
    Rng r ((uint32_t) (name.size() * 7919 + 13));
    auto buf = [&] (double s) { return std::vector<float> ((size_t) std::floor (s * SRb)); };
    const double WN = std::sqrt (SRb / REFSR);
    auto modal = [&] (std::vector<float>& d, double t0, double f, std::vector<double> ratios, double tau, double amp)
    {
        const int s = (int) std::floor (t0 * SRb), n = (int) d.size();
        for (size_t j = 0; j < ratios.size(); ++j)
        {
            const double fq = f * ratios[j], tj = tau / (1 + j * 0.55), a = amp / (1 + j * 0.45), ph = r() * 6.28;
            if (fq > SRb * 0.45) continue;
            for (int i = 0; s + i < n; ++i) { const double t = i / SRb, e = std::exp (-t / tj); if (e < 1e-4) break; d[(size_t) (s + i)] += (float) (a * e * std::sin (2 * PI * fq * t + ph)); }
        }
        const int cl = (int) std::floor (0.002 * SRb);
        for (int i = 0; i < cl && s + i < n; ++i) d[(size_t) (s + i)] += (float) (amp * 0.8 * (r() * 2 - 1) * (1 - (double) i / cl) * WN);
    };
    auto bp = [&] (std::vector<float>& d, double f, double q)
    {
        const double g = std::tan (PI * f / SRb), k = 1 / q, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2; double c1 = 0, c2 = 0;
        for (auto& x : d) { const double v3 = x - c2, v1 = a1 * c1 + a2 * v3, v2 = c2 + a2 * c1 + a3 * v3; c1 = 2 * v1 - c1; c2 = 2 * v2 - c2; x = (float) v1; }
    };
    std::vector<float> d;
    if (name == "Struck plate") { d = buf (1.6); std::vector<double> q { 1, 2.76, 5.4, 8.93, 13.34, 17.6, 23.1 }; modal (d, 0.05, 180, q, 0.18, 0.6); modal (d, 0.95, 180, q, 0.18, 0.3); }
    else if (name == "Metal clang") { d = buf (2.2); std::vector<double> q { 1, 1.59, 2.14, 2.3, 2.65, 2.92, 3.16, 3.5, 4.1 }; modal (d, 0.03, 230, q, 0.5, 0.45); const double ts[] = { 0.42, 0.51, 0.58 }; for (int i = 0; i < 3; ++i) modal (d, ts[i], 230 * (1 + i * 0.03), q, 0.12, 0.12); }
    else if (name == "Pipe hit") { d = buf (1.4); modal (d, 0.02, 410, { 1, 2.76, 5.4, 8.93 }, 0.35, 0.6); }
    else if (name == "Bell") { d = buf (3); for (size_t i = 0; i < d.size(); ++i) { const double t = i / SRb; d[i] = (float) (std::sin (2 * PI * 440 * t + 4 * std::exp (-t / 0.6) * std::sin (2 * PI * 440 * 3.5 * t)) * std::exp (-t / 0.9) * 0.45 * (t > 0.02 ? 1 : t / 0.02)); } }
    else if (name == "Noise burst")
    {
        d = buf (1.6); auto ng = NoiseGen (NoiseType::Pink, 0u, SRb); ng.r = r;
        const double ev[3][2] = { { 0.05, 0.25 }, { 0.6, 0.18 }, { 1.1, 0.35 } };
        for (auto& e : ev) { const int s = (int) std::floor (e[0] * SRb); for (int i = 0; s + i < (int) d.size(); ++i) { const double t = i / SRb, en = std::exp (-t / (e[1] / 5)); if (en < 1e-4) break; d[(size_t) (s + i)] += (float) (ng() * 0.9 * en); } }
        r = ng.r;
    }
    else if (name == "White hiss") { d = buf (2); const int per = (int) jround (0.05 * SRb); double gate = 1; for (size_t i = 0; i < d.size(); ++i) { if (i % (size_t) per == 0) gate = r() < 0.3 ? 0 : 0.2 + 0.8 * r(); d[i] = (float) ((r() * 2 - 1) * 0.35 * gate * WN); } }
    else if (name == "Brown rumble") { d = buf (2.4); auto ng = NoiseGen (NoiseType::Brown, 0u, SRb); ng.r = r; for (size_t i = 0; i < d.size(); ++i) { const double t = i / SRb; d[i] = (float) (ng() * (0.5 + 0.5 * std::sin (2 * PI * 0.9 * t)) * std::min (1.0, t / 0.2) * std::min (1.0, (2.4 - t) / 0.3)); } }
    else if (name == "Dust and crackle")
    {
        d = buf (2); auto ng = NoiseGen (NoiseType::Pink, 0u, SRb); ng.r = r;
        const double seg[3][2] = { { 0.1, 0.6 }, { 0.9, 1.2 }, { 1.5, 1.9 } };
        for (auto& sg : seg) { const int s = (int) std::floor (sg[0] * SRb), e = (int) std::floor (sg[1] * SRb); for (int i = s; i < e; ++i) { const double t = (double) (i - s) / (e - s); double v = ng() * 0.25 * std::sin (PI * t); if (ng.r() < 0.002 * REFSR / SRb) v += (ng.r() * 2 - 1) * 0.7 * SRb / REFSR; d[(size_t) i] += (float) v; } }
    }
    else if (name == "Radio static")
    {
        d = buf (2.2); const int per = (int) jround (0.025 * SRb);
        for (auto& x : d) x = (float) ((r() * 2 - 1) * WN);
        bp (d, 2400, 2); double g = 1;
        for (size_t i = 0; i < d.size(); ++i) { if (i % (size_t) per == 0) g = r() < 0.35 ? 0 : r(); const double t = i / SRb; d[i] = (float) (d[i] * 0.6 * g + (g > 0.8 ? std::sin (2 * PI * (800 + 3000 * std::fmod (t * 3, 1.0)) * t) * 0.15 : 0)); }
    }
    else if (name == "Gravel") { d = buf (1.8); for (int k = 0; k < 160; ++k) { const int s = (int) std::floor (r() * 1.7 * SRb); const double f = 600 + r() * 4000, a = 0.1 + r() * 0.4; const int len = (int) std::floor (SRb * 0.012); for (int i = 0; i < len && s + i < (int) d.size(); ++i) { const double t = i / SRb; d[(size_t) (s + i)] += (float) (std::sin (2 * PI * f * t) * std::exp (-t / 0.002) * a); } } }
    else if (name == "Glass drone") { d = buf (2.5); for (size_t i = 0; i < d.size(); ++i) { const double t = i / SRb; const double env = t < 0.3 ? 0 : t < 0.9 ? (t - 0.3) / 0.6 : t < 1.8 ? 1 : t < 2.3 ? 1 - (t - 1.8) / 0.5 : 0; const double idx = 1.5 + 1.2 * std::sin (2 * PI * 0.7 * t); d[i] = (float) (std::sin (2 * PI * 330 * t + idx * std::sin (2 * PI * 330 * 1.41 * t)) * 0.4 * env); } }
    else if (name == "Wood ticks") { d = buf (1.8); const double ts[] = { 0.08, 0.21, 0.27, 0.55, 0.62, 0.9, 1.13, 1.19, 1.5 }; for (double t0 : ts) { const int s = (int) std::floor (t0 * SRb); const double f = 900 + r() * 900; const int len = (int) std::floor (0.05 * SRb); for (int i = 0; i < len && s + i < (int) d.size(); ++i) { const double t = i / SRb; d[(size_t) (s + i)] += (float) (std::sin (2 * PI * f * t) * std::exp (-t / 0.008) * 0.7 + (i < SRb * 0.002 ? (r() * 2 - 1) * 0.5 * WN : 0)); } } }
    else if (name == "Kick thump") { d = buf (0.9); double ph = 0; for (size_t i = 0; i < d.size(); ++i) { const double t = i / SRb; ph += 2 * PI * (45 + 110 * std::exp (-t / 0.04)) / SRb; d[i] = (float) (std::sin (ph) * std::exp (-t / 0.25) * 0.9 + (i < SRb * 0.002 ? (r() * 2 - 1) * 0.4 * WN : 0)); } }
    else if (name == "Snare") { d = buf (0.8); std::vector<float> nz (d.size()); for (auto& x : nz) x = (float) ((r() * 2 - 1) * WN); bp (nz, 3500, 0.8); for (size_t i = 0; i < d.size(); ++i) { const double t = i / SRb; d[i] = (float) (std::sin (2 * PI * 190 * t) * std::exp (-t / 0.05) * 0.5 + nz[i] * std::exp (-t / 0.12) * 0.8); } }
    else if (name == "Hats") { d = buf (1.6); for (int k = 0; k < 8; ++k) { const int s = (int) std::floor ((0.05 + k * 0.19 + r() * 0.02) * SRb); const double dec = k % 4 == 3 ? 0.12 : 0.025; double p = 0; for (int i = 0; s + i < (int) d.size(); ++i) { const double t = i / SRb, e = std::exp (-t / dec); if (e < 1e-4) break; const double w = (r() * 2 - 1) * WN; d[(size_t) (s + i)] += (float) ((w - p) * 0.5 * e); p = w; } } }
    else if (name == "Rubber pluck") { d = buf (1.5); const int per = (int) std::floor (SRb / 110); std::vector<double> line ((size_t) per); for (auto& x : line) x = r() * 2 - 1; int idx = 0; for (size_t i = 0; i < d.size(); ++i) { const double t = i / SRb; const int nx = (idx + 1) % per; const double v = (line[(size_t) idx] + line[(size_t) nx]) * 0.498; line[(size_t) idx] = v; idx = nx; d[i] = (float) (v * 0.7 * (1 + 0.3 * std::sin (2 * PI * 6 * t))); } }
    return d;
}
}
