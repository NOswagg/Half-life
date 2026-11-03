#pragma once
/* Half Life — the sound engine, ported from the prototype (half-life.html, "genome", "DSP", "state" sections).
   Plain C++ (no JUCE). One Engine lives on the render thread; nothing in here is touched by the audio thread. */
#include "Dsp.h"
#include <array>
#include <string>
#include <map>
#include <set>
#include <functional>
#include <cstdio>
#include <atomic>

namespace hl
{
/* ---------- settings: every knob and selector (the prototype's P and G) ---------- */
enum class Role { Transient, Body, Tail, Texture };
enum class SlotRole { Auto, Transient, Body, Tail, Texture };
enum class NoiseType { White, Pink, Brown, Crackle, Velvet };
enum class DistType { Off, Sat, Shape, Fold, Phase };
enum class ResType { Off, Modal, Comb };
enum class ResStruct { Harmonic, Bar, Bell, Plate, Random };
enum class Target { Any, Punchy, Long, Bright, Dark, Wrong };
enum class FreshDraw { Keep, One, All };

const char* roleName (Role r);
struct Settings;
std::function<double (double)> distFnOf (const Settings& P);
void phaseDistortOf (const Settings& P, std::vector<float>& u, double SR2);
const char* targetName (Target t);

struct Settings
{
    double instability = 0.45, mutation = 0.35, glide = 0.6;
    double A = 1, D = 400, S = 1, R = 60;
    double pitchAmt = 0, pitchTime = 80, pitchCurve = 1;
    double lpgResp = 1, reso = 0.2, bright = 1;
    double maxLen = 3, threshold = -30, noiseLevel = 0.35, poolFit = 0, poolGate = -30, scatter = 0;
    double shA = 2, shD = 500, shS = 0, shR = 300, shFollow = 0.6;   // Shape: the envelope the sound is built toward (ms, ms, 0..1, ms, 0..1)
    double interact = 0.6; int cands = 4;
    double body = 0.15, bodyTime = 2.5, rot = 0, rotOnset = 0.3;
    double chaosSpeed = 0.12, chaosTurb = 0.6, chaosCouple = 0.3;
    DistType distType = DistType::Off; double distDrive = 6, distMix = 1, distTone = 1, satAsym = 0.2, satKnee = 0.3, shCurve = 0, shH2 = 0.1, shH3 = 0.2, shH5 = 0, shSteps = 0,
        fdFolds = 2, fdBias = 0, fdShape = 0.5, pdDepth = 0.6, pdSmooth = 0.5, pdFb = 0.2;
    ResType resType = ResType::Modal; ResStruct resStruct = ResStruct::Bar; double resMix = 0.3, resPitch = 48, resRand = 5, resAmt = 0.6, resDamp = 0.4, resVoices = 4, resSpread = 6;
    // generators
    bool noiseOn = true, clickOn = true, blipOn = false; NoiseType noiseType = NoiseType::Pink; Role noiseRole = Role::Texture;
    // deciding
    Target target = Target::Any; bool lockLinks = false; FreshDraw fresh = FreshDraw::One;
    std::array<SlotRole, 4> slotRole { SlotRole::Auto, SlotRole::Auto, SlotRole::Auto, SlotRole::Auto };
    double resT60() const { return 0.05 * std::pow (160.0, resAmt / 0.9); }
};

/* ---------- chaos: coupled Chua circuits ---------- */
struct Circuit { double a = 15.6, sp = 1; double s[3] {}, m[3] {}, d[3] { 1, 0.25, 1.6 }; };
Circuit newCircuit (double a, double sp, const std::function<double()>& r);
void advanceNet (std::vector<Circuit*> net, double units, double k);
double chOut (const Circuit& c, int j);
constexpr int NH = 8;   // hidden values
extern const char* HKEYS[NH];
extern const char* HLABEL[NH];
enum H { Jitter, LpgTime, Tilt, Drift, Density, Reverse, Width, Wobble };
struct RouteT { int a = 0, b = 1; double w = 0.7, sa = 1, sb = 1; };
using HRoute = std::array<RouteT, NH>;
using HVals = std::array<double, NH>;
HRoute makeHRoute (const std::function<double()>& r);
HVals hvalsOf (const std::array<Circuit, 4>& net, const HRoute& route);

/* per-slot glide LFOs: each slot's own circuit, routed to 2-3 of its grain parameters */
enum GDest { GPosition, GGrain, GPitch, GDensity, GGate, GBright, GPan, GReverse, NG };
extern const char* GLABEL[NG];
struct GRoute { int dest = 0; double depth = 0; int comp = 0; };
struct Glide { Circuit c; std::vector<GRoute> routes; double vals[3] {}; double val = 0; };
Glide makeGlide (int i);
void readGlide (Glide& g);
struct GlideSnap { double val = 0; double vals[3] {}; std::vector<GRoute> routes; };

/* ---------- sources ---------- */
struct Analysis { int win = 1, n = 1; std::vector<float> rms, db, abs; std::vector<int> onsets; double ref = 0; };

struct Slot
{
    int idx = 0;
    std::string name, path;              // path: the file it came from ("" for built-ins)
    std::vector<float> data;             // mono, at the engine's rate
    std::unique_ptr<Mip> mip;
    Analysis an;
    bool example = false, fromPool = false, pinned = false;
    double regionA = -1, regionB = -1;   // seconds within the file, when a pool draw cut a region
    int poolFile = -1;                   // index into the pool's file list
    Glide glide;
    bool has() const { return ! data.empty(); }
    void clear() { data.clear(); mip.reset(); an = {}; name.clear(); path.clear(); example = fromPool = false; regionA = regionB = -1; poolFile = -1; }
};

/* layer keys: 0-3 = slots A-D, 4 = noise, 5 = click, 6 = blip */
enum Key { KA, KB, KC, KD, KNoise, KClick, KBlip };
enum class Kind { Slot, Noise, Click, Blip };
inline Kind kindOf (int key) { return key < 4 ? Kind::Slot : key == KNoise ? Kind::Noise : key == KClick ? Kind::Click : Kind::Blip; }
std::string keyName (int key);   // "A", "Noise", ...

struct Layer
{
    int key = 0; Kind kind = Kind::Slot; Role role = Role::Body; uint32_t seed = 0;
    double pos = 0, spread = 0, grain = 0, dens = 0; int count = 1;
    double pitch = 0, pjit = 0, rev = 0, pan = 0, width = 0, gain = 0, start = 0, decay = 0, curve = 0, bright = 0, reso = 0, f0 = 0;
};

enum class LinkType { Follow, Clock };
enum LDest { LDensity, LPosition, LPitch, LBright, LDuck, LClock };
struct Link { int from = 0, to = 0; LinkType type = LinkType::Follow; int dest = 0; double amount = 0; };

constexpr int NROT = 9;
enum RotK { Droop, Flutter, Erode, Crush, Bits, Drop, Dust, Starve, Gasp };
extern const char* ROTLABEL[NROT];

struct ChaosSnap { std::array<Circuit, 4> h; std::array<Circuit, 4> sl; HRoute route; double k = 0.3; };

struct Genome
{
    uint32_t seed = 0;
    HVals hidden {};
    std::array<GlideSnap, 4> glide;
    ChaosSnap chaos;
    std::vector<Layer> layers;
    std::vector<int> order;
    std::vector<Link> links;
    double resJit = 0;
    std::array<double, NROT> rot {};
};

/* ---------- a rendered sound ---------- */
/* the Shape's target level 0..1 at time t (s); used by the engine and drawn by the window */
inline double shapeLevel (const Settings& P, double t)
{
    const double A = std::max (0.0005, P.shA / 1000), D = std::max (0.005, P.shD / 1000), S = P.shS, R = std::max (0.005, P.shR / 1000);
    double v = t < A ? t / A : S + (1 - S) * std::exp (-(t - A) / (D / 4));
    if (S > 0.001) { const double rs = std::max (A, P.maxLen - R); if (t > rs) v *= std::exp (-(t - rs) / (R / 4)); }
    return v;
}

struct Features { double attack = 0.5, len = 1, bright = 0.3, irreg = 0, stereo = 0, tail = 0.2, nov = 0, shape = 0; };
struct Out
{
    std::vector<float> L, R; int end = 0; double sampleRate = 48000;
    Features feat; std::vector<float> mixMin, mixMax;
    double peakT = 0, thalf = -1, len = 0; int rerolls = 0, layers = 0;
    std::vector<int> skipped;
};

struct Entry { int id = 0; Genome genome; std::shared_ptr<const Out> out; std::string how; };

/* ---------- the engine ---------- */
class Engine
{
public:
    Engine();
    void setRate (double sr);
    double rate() const { return SR; }

    Settings P;                                 // copied in by the owner before each command
    std::atomic<bool>* abort = nullptr;         // set: render gives up early (returns nullptr)
    std::array<Slot, 4> slots;
    std::array<Circuit, 4> HNET;
    HRoute HROUTE;
    HVals live {};
    std::set<int> locks;

    std::vector<std::shared_ptr<Entry>> history;   // newest first, max 16
    std::shared_ptr<Entry> current;
    int hid = 0;
    std::string status;

    // sources
    void setSlotData (int i, std::vector<float> data, const std::string& name, bool example, bool dcBlock = true);
    void loadBuiltin (int i, const std::string& name, bool example);
    static const std::vector<std::string>& libNames();
    std::vector<float> makeBuiltin (const std::string& name, double sr) const;
    static Analysis analyze (const std::vector<float>& data, double sr);
    std::vector<int> validFor (const Analysis& an, Role role) const;
    std::vector<int> validChain (const Analysis& an, Role role) const;
    static Role pickRole (const Analysis& an, Rng& r);
    static double fitScore (const Analysis& an, Role role);
    struct Src { int key; Kind kind; };
    std::vector<Src> sources() const;

    // generating
    bool generate (bool fresh);                 // false: nothing to render
    bool relink();
    bool rerender();                            // re-render the current genome with the current settings
    std::shared_ptr<const Out> render (Genome& g);
    void tick (double dt);                      // the live chaos moves on
    void boot();                                // the first sound (fixed seed, not played)
    void dcBlockNow (std::vector<float>& d) const;
    double layerGateMs (const Layer& L, const Genome& g) { const bool t = trk.on; trk.on = false; const double v = effDecay (L, g); trk.on = t; return v; }

    // perish's engine side: roles are set by the owner; this rerolls built-ins, glides and routing
    void perishSources (bool poolUsable, const std::array<SlotRole, 4>& roles);
    void rerouteChaos();
    void rerouteGlide (int i);

private:
    double SR = 48000, DC_R = 0;
    // genome
    Role roleFor (const Src& s, Rng& r) const;
    Layer buildLayer (const Src& s, Rng& r, const Role* force = nullptr) const;
    void perturb (Layer& L, Rng& r, double amt) const;
    std::array<GlideSnap, 4> snapGlide() const;
    ChaosSnap snapChaos() const;
    ChaosSnap mutChaos (const ChaosSnap& pc, Rng& r, double amt) const;
    void chaosStart (Genome& g) const;
    void syncGenome (Genome& g);
    Genome freshGenome();
    Genome mutGenome();
    void addLink (std::vector<int>& order, std::vector<Link>& links, Rng& r) const;
    std::pair<std::vector<int>, std::vector<Link>> makeLinks (const std::vector<int>& keys, Rng& r) const;
    std::pair<std::vector<int>, std::vector<Link>> mutateLinks (const Genome& pg, const std::vector<int>& keys, Rng& r, double amt) const;
    void commit (Genome g, const std::string& how, std::shared_ptr<const Out> out, int picked);
    const Out* decideIdx (const std::vector<std::shared_ptr<const Out>>& cands, int& idx) const;
public:
    // the chaos inside one sound (used by the DSP)
    struct Track { bool on = false; std::array<std::vector<float>, NH> h; std::array<std::array<std::vector<float>, 3>, 4> s; };
    struct Mods { std::vector<std::pair<const std::vector<float>*, double>> density, position, pitch, bright, duck; struct Strike { int t; double level; }; std::vector<Strike> strikes; };
private:
    Track trk;
    void chaosTrack (const Genome& g, int N);
    double Hv (const Genome& g, int key, double i) const;
    double glideMod (const Genome& g, int key, int dest, double i) const;
    double effDecay (const Layer& lay, const Genome& g) const;
    int lifeS (double decayS) const;
    double shapeT (double t) const;                       // Shape's target level 0..1 at time t (s)
    void bodyEff (double& level, double& time) const;     // the LPG body stage, pulled toward the Shape's sustain
    void shapeCorrect (std::vector<float>& ML, std::vector<float>& MR, double& peak, std::vector<double>& wr, double& pw) const;
    double shapeScore (const std::vector<float>& L, const std::vector<float>& R, int n) const;
    void addGasps (Mods& m, const Layer& lay, const Genome& g) const;
    void applyRot (std::vector<float>& L, std::vector<float>& R, int n, const Genome& g) const;
    void fillGrains (std::vector<float>& L, std::vector<float>& R, const Layer& lay, Slot& slot, const std::vector<int>& valid, Rng& r, const Genome& g, const Mods& m) const;
    void fillGenerator (std::vector<float>& L, std::vector<float>& R, const Layer& lay, Rng& r, const Genome& g, const Mods& m) const;
    void applyLPG (std::vector<float>& L, std::vector<float>& R, const Layer& lay, Rng& r, const Genome& g, const Mods& m) const;
    void applyResonator (std::vector<float>& ML, std::vector<float>& MR, const Genome& g) const;
    void applyDistortion (std::vector<float>& ML, std::vector<float>& MR) const;
    std::vector<float> applyWarp (const std::vector<float>& X, const std::vector<double>& pos, const std::vector<float>& rateV) const;
    double tpNormalize (std::vector<float>& bl, std::vector<float>& br, int end) const;
    Features features (const std::vector<float>& L, const std::vector<float>& R, int n) const;
public:
    std::function<double (double)> distFn() const;   // also drawn by the window
    void phaseDistort (std::vector<float>& u, double SR2) const;
    static double modAt (const std::vector<std::pair<const std::vector<float>*, double>>& list, double i);
};
}
