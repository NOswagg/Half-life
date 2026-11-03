#include "Params.h"

namespace hlp
{
static juce::String noteName (float v)
{
    static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = juce::roundToInt (v);
    return juce::String (N[((n % 12) + 12) % 12]) + juce::String (std::floor (n / 12.0) - 2) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (juce::roundToInt (440 * std::pow (2.0, (v - 69) / 12))) + " Hz";
}

juce::String fmtValue (const FloatDef& d, float v)
{
    const juce::String id (d.id);
    auto sgn = [] (float x, int dp) { return (x > 0 ? "+" : "") + juce::String (x, dp); };
    if (id == "rotOnset") return v < 0.15f ? "from the start" : v < 0.5f ? "mid-decay" : "only at the end";
    if (id == "poolFit") return std::abs (v) < 0.02f ? "random" : juce::String (v > 0 ? "match " : "mismatch ") + juce::String (juce::roundToInt (std::abs (v) * 100)) + "%";
    if (id == "poolGate") return juce::String (juce::roundToInt (v)) + " dBFS";
    if (id == "chaosSpeed") return v < 0.05f ? "glacial" : v < 0.2f ? "slow" : v < 0.7f ? "restless" : "frantic";
    if (id == "chaosTurb") return v < 0.05f ? "still" : juce::String (v, 1);
    if (id == "chaosCouple") return v < 0.15f ? "independent" : v < 0.9f ? "loose" : v < 2 ? "tangled" : "bound";
    if (id == "distDrive") return "+" + juce::String (v, 1) + " dB";
    if (id == "distTone") return v >= 0.999f ? juce::String ("open") : juce::String (juce::roundToInt (600 * std::pow (36.0, v))) + " Hz";
    if (id == "shCurve" || id == "fdBias") return sgn (v, 2);
    if (id == "shSteps") return v < 2 ? juce::String ("smooth") : juce::String (juce::roundToInt (v)) + " steps";
    if (id == "fdFolds") return juce::String (v, 1) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97"));
    if (id == "pdDepth") return juce::String (v * 3, 2) + " ms";
    if (id == "pdSmooth") return juce::String (juce::roundToInt (40 * std::pow (200.0, v))) + " Hz";
    if (id == "resAmt") return juce::String (juce::roundToInt (v * 100)) + "% " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + juce::String (0.05 * std::pow (160.0, v / 0.9), 1) + " s";
    if (id == "resPitch") return noteName (v);
    if (id == "resRand") return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (v, 1) + " st";
    if (id == "resSpread") return juce::String (juce::roundToInt (v)) + " cents";
    if (id == "volume") return v <= -60 ? juce::String ("off") : sgn (v, 1) + " dB";
    switch (d.fmt)
    {
        case Fmt::Pct: return juce::String (juce::roundToInt (v * 100)) + "%";
        case Fmt::Ms: return v < 10 ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms";
        case Fmt::St: return sgn (v, 1) + " st";
        case Fmt::Db: return juce::String (juce::roundToInt (v)) + " dB";
        case Fmt::S: return juce::String (v, 2) + " s";
        case Fmt::X: return juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (v, 2);
        case Fmt::N: return juce::String (v, 2);
        case Fmt::Int: return juce::String (juce::roundToInt (v));
        case Fmt::Raw: default: return juce::String (v, 2);
    }
}

const FloatDef* findFloat (const juce::String& id) { for (auto& d : floats()) if (id == d.id) return &d; return nullptr; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    for (const auto& d : floats())
    {
        juce::NormalisableRange<float> r;
        if (d.log)
        {
            const float lo = d.lo, hi = d.hi;
            r = juce::NormalisableRange<float> (lo, hi,
                [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                [] (float, float, float v) { return v; });
        }
        else r = juce::NormalisableRange<float> (d.lo, d.hi, d.step);
        const FloatDef def = d;
        L.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { d.id, 1 }, d.name, r, d.def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([def] (float v, int) { return fmtValue (def, v); })));
    }
    for (const auto& c : choices()) L.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { c.id, 1 }, c.name, c.items, c.def));
    // switches are two-step choices: they store a clean 0 or 1 whatever value a host sends (JUCE's bool keeps the raw value)
    for (const auto& b : bools()) L.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { b.id, 1 }, b.name, juce::StringArray { "Off", "On" }, b.def ? 1 : 0));
    return L;
}

bool isRerenderParam (const juce::String& id)
{
    if (auto* f = findFloat (id)) return f->rerender;
    for (auto& c : choices()) if (id == c.id) return c.rerender;
    for (auto& b : bools()) if (id == b.id) return b.rerender;
    return false;
}

hl::Settings readSettings (juce::AudioProcessorValueTreeState& a)
{
    auto f = [&] (const char* id) { return (double) a.getRawParameterValue (id)->load(); };
    auto c = [&] (const char* id) { return juce::roundToInt (a.getRawParameterValue (id)->load()); };
    auto b = [&] (const char* id) { return a.getRawParameterValue (id)->load() > 0.5f; };
    hl::Settings P;
    P.instability = f ("instability"); P.mutation = f ("mutation"); P.glide = f ("glide");
    P.A = f ("attack"); P.D = f ("decay"); P.S = f ("sustain"); P.R = f ("release");
    P.pitchAmt = f ("pitchAmt"); P.pitchTime = f ("pitchTime"); P.pitchCurve = f ("pitchCurve");
    P.lpgResp = f ("lpgResp"); P.reso = f ("reso"); P.bright = f ("bright");
    P.maxLen = f ("maxLen"); P.scatter = f ("scatter");
    P.shA = f ("shpA"); P.shD = f ("shpD"); P.shS = f ("shpS"); P.shR = f ("shpR"); P.shFollow = f ("shpFollow"); P.threshold = f ("threshold"); P.noiseLevel = f ("noiseLevel"); P.poolFit = f ("poolFit"); P.poolGate = f ("poolGate");
    P.interact = f ("interact"); P.cands = juce::roundToInt (f ("cands"));
    P.body = f ("body"); P.bodyTime = f ("bodyTime"); P.rot = f ("rot"); P.rotOnset = f ("rotOnset");
    P.chaosSpeed = f ("chaosSpeed"); P.chaosTurb = f ("chaosTurb"); P.chaosCouple = f ("chaosCouple");
    P.distType = (hl::DistType) c ("distType"); P.distDrive = f ("distDrive"); P.distMix = f ("distMix"); P.distTone = f ("distTone");
    P.satAsym = f ("satAsym"); P.satKnee = f ("satKnee"); P.shCurve = f ("shCurve"); P.shH2 = f ("shH2"); P.shH3 = f ("shH3"); P.shH5 = f ("shH5"); P.shSteps = f ("shSteps");
    P.fdFolds = f ("fdFolds"); P.fdBias = f ("fdBias"); P.fdShape = f ("fdShape"); P.pdDepth = f ("pdDepth"); P.pdSmooth = f ("pdSmooth"); P.pdFb = f ("pdFb");
    P.resType = (hl::ResType) c ("resType"); P.resStruct = (hl::ResStruct) c ("resStruct");
    P.resMix = f ("resMix"); P.resPitch = f ("resPitch"); P.resRand = f ("resRand"); P.resAmt = f ("resAmt"); P.resDamp = f ("resDamp"); P.resVoices = f ("resVoices"); P.resSpread = f ("resSpread");
    P.noiseOn = b ("noiseOn"); P.clickOn = b ("clickOn"); P.blipOn = b ("blipOn");
    P.noiseType = (hl::NoiseType) c ("noiseType");
    { const hl::Role nr[] = { hl::Role::Texture, hl::Role::Body, hl::Role::Tail, hl::Role::Transient }; P.noiseRole = nr[juce::jlimit (0, 3, c ("noiseRole"))]; }
    P.target = (hl::Target) c ("target"); P.lockLinks = b ("lockLinks"); P.fresh = (hl::FreshDraw) c ("poolFresh");
    const char* roles[] = { "roleA", "roleB", "roleC", "roleD" };
    for (int i = 0; i < 4; ++i) P.slotRole[(size_t) i] = (hl::SlotRole) c (roles[i]);
    return P;
}
}
