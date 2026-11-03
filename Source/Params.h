#pragma once
#include <JuceHeader.h>
#include "Engine.h"

/* Every knob and selector of the prototype, as host parameters (automatable, saved with the project).
   IDs are permanent: never rename one, or old projects lose that setting. */
namespace hlp
{
enum class Fmt { Pct, Ms, St, Db, S, X, N, Int, Raw };
struct FloatDef
{
    const char* id; const char* name; float lo, hi, def, step; bool log; Fmt fmt;
    bool rerender;   // changing it re-renders the current sound (the prototype's on:rerender)
};

// fmt helpers
juce::String fmtValue (const FloatDef& d, float v);

inline const std::vector<FloatDef>& floats()
{
    static const std::vector<FloatDef> v = {
        // macros
        { "instability", "Instability", 0, 1, 0.45f, 0, false, Fmt::Pct, false },
        { "mutation", "Mutation", 0.02f, 1, 0.35f, 0, false, Fmt::Pct, false },
        { "glide", "Glide", 0, 1, 0.6f, 0, false, Fmt::Pct, false },
        { "rot", "Rot", 0, 1, 0, 0, false, Fmt::Pct, true },
        { "rotOnset", "Rot sets in", 0, 0.9f, 0.3f, 0, false, Fmt::Raw, true },
        { "interact", "Interaction", 0, 1, 0.6f, 0, false, Fmt::Pct, true },
        { "cands", "Candidates", 1, 8, 4, 1, false, Fmt::Int, false },
        // low pass gate
        { "lpgResp", "Response time", 0.25f, 12, 1, 0, true, Fmt::X, true },
        { "body", "Body", 0, 0.6f, 0.15f, 0, false, Fmt::Pct, true },
        { "bodyTime", "Body time", 0.3f, 8, 2.5f, 0, true, Fmt::S, true },
        { "reso", "Resonance", 0, 1, 0.2f, 0, false, Fmt::Pct, true },
        { "bright", "Brightness", 0.2f, 2, 1, 0, true, Fmt::X, true },
        // pitch envelope
        { "pitchAmt", "Amount", -24, 36, 0, 0.5f, false, Fmt::St, true },
        { "pitchTime", "Time", 5, 1500, 80, 0, true, Fmt::Ms, true },
        { "pitchCurve", "Curve", 0.3f, 3, 1, 0, true, Fmt::N, true },
        // amp envelope (playback only)
        { "attack", "Attack", 0.5f, 500, 1, 0, true, Fmt::Ms, false },
        { "decay", "Decay", 5, 3000, 400, 0, true, Fmt::Ms, false },
        { "sustain", "Sustain", 0, 1, 1, 0, false, Fmt::Pct, false },
        { "release", "Release", 5, 3000, 60, 0, true, Fmt::Ms, false },
        // render
        { "maxLen", "Max length", 0.2f, 10, 3, 0.05f, false, Fmt::S, true },
        { "threshold", "Scan threshold", -60, -6, -30, 1, false, Fmt::Db, true },
        { "scatter", "Scatter", 0, 1, 0, 0, false, Fmt::Pct, true },
        // shape: the envelope the sound is built toward
        { "shpA", "Attack", 0.5f, 800, 2, 0, true, Fmt::Ms, true },
        { "shpD", "Decay", 20, 6000, 500, 0, true, Fmt::Ms, true },
        { "shpS", "Sustain", 0, 1, 0, 0, false, Fmt::Pct, true },
        { "shpR", "Release", 20, 6000, 300, 0, true, Fmt::Ms, true },
        { "shpFollow", "Follow", 0, 1, 0.6f, 0, false, Fmt::Pct, true },
        // generators
        { "noiseLevel", "Level", 0, 1, 0.35f, 0, false, Fmt::Pct, true },
        // pool
        { "poolFit", "Fit", -1, 1, 0, 0, false, Fmt::Raw, false },
        { "poolGate", "Loudness gate", -60, -6, -30, 1, false, Fmt::Raw, false },
        // chaos
        { "chaosSpeed", "Speed", 0.01f, 2, 0.12f, 0, true, Fmt::Raw, false },
        { "chaosTurb", "Turbulence", 0, 8, 0.6f, 0, false, Fmt::Raw, true },
        { "chaosCouple", "Coupling", 0, 3, 0.3f, 0, false, Fmt::Raw, false },
        // distortion
        { "distDrive", "Drive", 0, 36, 6, 0.5f, false, Fmt::Raw, true },
        { "distTone", "Tone", 0, 1, 1, 0, false, Fmt::Raw, true },
        { "distMix", "Mix", 0, 1, 1, 0, false, Fmt::Pct, true },
        { "satKnee", "Knee", 0, 1, 0.3f, 0, false, Fmt::Pct, true },
        { "satAsym", "Asymmetry", 0, 1, 0.2f, 0, false, Fmt::Pct, true },
        { "shCurve", "Curve", -1, 1, 0, 0, false, Fmt::Raw, true },
        { "shH2", "2nd harmonic", 0, 1, 0.1f, 0, false, Fmt::Pct, true },
        { "shH3", "3rd harmonic", 0, 1, 0.2f, 0, false, Fmt::Pct, true },
        { "shH5", "5th harmonic", 0, 1, 0, 0, false, Fmt::Pct, true },
        { "shSteps", "Steps", 0, 24, 0, 1, false, Fmt::Raw, true },
        { "fdFolds", "Folds", 1, 10, 2, 0.1f, false, Fmt::Raw, true },
        { "fdBias", "Symmetry offset", -1, 1, 0, 0, false, Fmt::Raw, true },
        { "fdShape", "Shape", 0, 1, 0.5f, 0, false, Fmt::Pct, true },
        { "pdDepth", "Depth", 0, 1, 0.6f, 0, false, Fmt::Raw, true },
        { "pdSmooth", "Mod brightness", 0, 1, 0.5f, 0, false, Fmt::Raw, true },
        { "pdFb", "Feedback", 0, 0.8f, 0.2f, 0, false, Fmt::Pct, true },
        // resonator
        { "resAmt", "Resonance", 0, 0.9f, 0.6f, 0, false, Fmt::Raw, true },
        { "resPitch", "Pitch", 24, 96, 48, 1, false, Fmt::Raw, true },
        { "resRand", "Random tune", 0, 12, 5, 0.5f, false, Fmt::Raw, true },
        { "resDamp", "Damping", 0, 1, 0.4f, 0, false, Fmt::Pct, true },
        { "resVoices", "Voices", 1, 6, 4, 1, false, Fmt::Int, true },
        { "resSpread", "Spread", 0, 50, 6, 1, false, Fmt::Raw, true },
        { "resMix", "Mix", 0, 1, 0.3f, 0, false, Fmt::Pct, true },
        // output
        { "volume", "Volume", -60, 2, 0, 0.5f, false, Fmt::Raw, false },
    };
    return v;
}

struct ChoiceDef { const char* id; const char* name; juce::StringArray items; int def; bool rerender; };
inline const std::vector<ChoiceDef>& choices()
{
    static const std::vector<ChoiceDef> v = {
        { "distType", "Distortion type", { "Off", "Saturation", "Wave shaper", "Folder", "Phase distortion" }, 0, true },
        { "resType", "Resonator type", { "Off", "Modal bank", "Comb / string" }, 1, true },
        { "resStruct", "Modal structure", { "Harmonic", "Bar", "Bell", "Plate", "Random per sound" }, 1, true },
        { "noiseType", "Noise colour", { "White", "Pink", "Brown", "Crackle", "Velvet" }, 1, true },
        { "noiseRole", "Noise role", { "Texture", "Body", "Tail", "Transient" }, 0, true },
        { "target", "Decide by", { "any", "punchy", "long", "bright", "dark", "wrong" }, 0, false },
        { "poolFresh", "Fresh draws", { "Keep", "Swap one", "Swap all" }, 1, false },
        { "roleA", "Slot A role", { "Auto role", "Transient", "Body", "Tail", "Texture" }, 0, true },
        { "roleB", "Slot B role", { "Auto role", "Transient", "Body", "Tail", "Texture" }, 0, true },
        { "roleC", "Slot C role", { "Auto role", "Transient", "Body", "Tail", "Texture" }, 0, true },
        { "roleD", "Slot D role", { "Auto role", "Transient", "Body", "Tail", "Texture" }, 0, true },
    };
    return v;
}

struct BoolDef { const char* id; const char* name; bool def; bool rerender; };
inline const std::vector<BoolDef>& bools()
{
    static const std::vector<BoolDef> v = {
        { "noiseOn", "Noise", true, true }, { "clickOn", "Click", true, true }, { "blipOn", "Blip", false, true }, { "lockLinks", "Lock links", false, false } };
    return v;
}

const FloatDef* findFloat (const juce::String& id);
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
hl::Settings readSettings (juce::AudioProcessorValueTreeState& apvts);
bool isRerenderParam (const juce::String& id);
}
