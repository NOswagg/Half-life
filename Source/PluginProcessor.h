#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "Params.h"

/* Half Life — processor.
   Three threads, three jobs:
   - the render thread owns the sound engine: Fresh / Mutate / Perish / pool draws / re-renders / export / saving takes.
     The window only posts commands to it and reads the snapshots it publishes.
   - the audio thread plays finished sounds: MIDI and the window's keys, an envelope per voice, the take recorder.
     It never allocates, locks or waits.
   - the message thread runs the window.
   A finished sound reaches the audio thread through one atomic pointer; old sounds are kept alive by the render
   thread for a while, so the audio thread never frees memory. */

/* a sound ready to play: the audio plus band-limited copies for playing it pitched up (built on demand) */
struct PlayBuf
{
    std::shared_ptr<const hl::Out> out;
    int id = 0;
    static constexpr int NL = 17;                       // up to +4 octaves
    std::array<std::unique_ptr<hl::Level>, NL> lvL, lvR;
    std::array<std::atomic<bool>, NL> ready {};
    std::array<std::atomic<bool>, NL> wanted {};
    std::atomic<int> users { 0 };                       // voices playing it (audio thread counts)
};

/* what the window shows, rebuilt by the render thread after every change (never edited after publishing) */
struct UiState
{
    struct SlotUi
    {
        bool has = false, example = false, fromPool = false, pinned = false, missing = false;
        juce::String name, path, meta; bool metaBad = false;
        std::vector<float> db; std::vector<char> on; std::vector<int> onsets;
        juce::String glideKind, glideRoutes;
    };
    struct LayerUi { int key; juce::String name, role, params; bool locked, skipped; };
    struct LinkUi { int from, to; bool clock; juce::String label, text; double amount; };
    struct HistUi { int id; juce::String how; std::shared_ptr<const hl::Out> out; bool cur; };

    std::array<SlotUi, 4> slots;
    std::shared_ptr<const hl::Out> out; int id = 0; uint32_t seed = 0;
    std::vector<LayerUi> layers;
    std::vector<int> order; std::vector<LinkUi> links; int nLinks = 0;
    std::vector<HistUi> history;
    std::array<double, hl::NROT> rot {};
    juce::String poolInfo; int poolFiles = 0; bool poolUsable = false;
    juce::String preset; bool presetEdited = false;   // the preset the sound came from ("" = none), and whether it changed since
};

class HalfLifeProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::Thread, private juce::AudioProcessorValueTreeState::Listener
{
public:
    HalfLifeProcessor();
    ~HalfLifeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Half Life"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- commands from the window (message thread) ----
    void fresh();
    void mutate();
    void perish();
    void relink();
    void trigger();
    void recall (int entryId);
    void keyDown (int note);           // the window's keyboard
    void keyUp (int note);
    void loadFile (int slot, const juce::File& f);
    void loadBuiltin (int slot, const juce::String& name);
    void clearSlot (int slot);
    void togglePin (int slot);
    void toggleLock (int key);
    void rerouteGlide (int slot);
    void rerouteChaos();
    void poolAdd (const juce::Array<juce::File>& filesOrFolders);
    void poolClear();
    juce::File makeDragFile();   // message thread: the current sound as a WAV at the last played note, for dragging out
    // ---- presets: one .hlpreset file each (a zip: preset.xml + the slots' audio as FLAC) ----
    struct PresetRef { juce::String name; juce::File file; int factory = -1; };   // factory: index of a built-in preset
    std::vector<PresetRef> listPresets() const;            // factory first, then Documents/Half Life/Presets, by name
    juce::File presetFolder() const { return outDir ("Presets"); }
    void savePreset (const juce::String& name);            // written by the render thread; the status line reports it
    juce::String loadPreset (const PresetRef& p);          // message thread; returns an error text, or "" when loading started
    juce::String loadPresetFile (const juce::File& f) { return loadPreset ({ f.getFileNameWithoutExtension(), f, -1 }); }
    void setOctave (int o) { octave = juce::jlimit (-3, 3, o); }
    int getOctave() const { return octave.load(); }

    // ---- what the window reads ----
    std::shared_ptr<const UiState> ui() const { return std::atomic_load (&uiShared); }
    float playheadFraction() const { return playhead.load(); }
    bool isBusy() const { return busy.load(); }
    int getLastNote() const { return lastNote.load(); }
    juce::String statusText() const { const juce::SpinLock::ScopedLockType l (statusLock); return status; }
    int statusSerial() const { return statusN.load(); }
    bool presetIsDirty() const { return presetDirty.load(); }   // a knob moved since the preset (seen before the next render publishes)
    // the live chaos, for the background and Under the hood: 4 global circuits (x,y,z), 4 slot circuits, 8 hidden values
    struct Live { float h[4][3]; float s[4][3]; float hv[hl::NH]; float g[4]; };
    Live live() const { Live l; const juce::SpinLock::ScopedLockType k (liveLock); l = liveV; return l; }
    std::function<double (double)> distCurve() const;   // for the distortion display
    hl::Settings settings() { return hlp::readSettings (apvts); }

private:
    // render thread
    void run() override;
    void post (std::function<void()> fn, bool changesState = true);
    void publish();
    void setStatus (const juce::String& s);
    void audition();
    void syncSettings();
    void parameterChanged (const juce::String&, float) override;
    void publishSound();
    void buildWanted();
    void drainTake (bool finishing);

    hl::Engine engine;
    std::mutex qLock; std::deque<std::pair<std::function<void()>, bool>> queue;
    juce::WaitableEvent wake;
    std::atomic<bool> busy { false };
    std::atomic<juce::uint32> rerenderAt { 0 };     // ms counter time a debounced re-render is due (0 = none)
    std::atomic<bool> suppressRerender { false };
    std::shared_ptr<const UiState> uiShared;
    mutable juce::SpinLock statusLock; juce::String status; std::atomic<int> statusN { 0 };
    mutable juce::SpinLock liveLock; Live liveV {};
    juce::uint32 lastTick = 0;

    // sources from files
    juce::AudioFormatManager formats;
    bool decodeFile (const juce::File& f, double rate, std::vector<float>& out, double maxSec, double regionA = -1, double regionB = -1, double* peakDb = nullptr, double* dur = nullptr, const std::atomic<bool>* stop = nullptr);
    void loadSlotFromFile (int i, const juce::File& f, bool fromPool, double ra, double rb, int poolIdx);
    // pool
    struct PoolFile { juce::File file; juce::String path, name; int st = 0; };   // st: 0 new, 1 ok, 2 quiet, 3 bad
    std::vector<PoolFile> pool;
    struct Decoded { std::vector<float> d; double peakDb = -200, dur = 0; };
    std::list<std::pair<int, std::shared_ptr<Decoded>>> poolCache;
    std::shared_ptr<Decoded> poolDecode (int idx);
    bool poolUsable() const;
    int drawInto (int slot, std::set<int>& taken);
    void drawPool (bool all);
    juce::String poolInfoText() const;

    // playback (audio thread)
    std::atomic<PlayBuf*> playing { nullptr };
    std::deque<std::shared_ptr<PlayBuf>> keepAlive;       // render thread only
    std::shared_ptr<PlayBuf> currentBuf;                  // render thread only
    struct Voice
    {
        PlayBuf* buf = nullptr; double pos = 0, rate = 1, endPos = 0; int note = -1, src = 0, h = 0, hWant = 0; bool on = false, released = false; uint64_t age = 0;
        // envelope (prototype: linear attack, exponential approach to sustain, exponential release, stopped after 2.6 x release)
        int stage = 0; double env = 0, aInc = 0, S = 1, dK = 0, dE = 1, rK = 0, rLeft = 0, autoRel = -1, t = 0;
    };
    std::array<Voice, 12> voices;
    uint64_t voiceClock = 0;
    double hostRate = 48000;
    std::atomic<float> playhead { -1.0f };
    std::atomic<int> lastNote { 60 };
    std::atomic<int> octave { 0 };
    std::atomic<int> auditionReq { 0 };                    // play the newest sound once (bumped by the render thread)
    std::atomic<int> triggerReq { 0 };                     // the Trigger button
    int auditionSeen = 0, triggerSeen = 0;
    juce::AbstractFifo keyFifo { 256 }; std::array<int, 256> keyEvents {};   // +note+1 = down, -(note+1) = up
    float gainNow = 0.8f;
    void startVoice (int note, int src, bool oneShot);
    void releaseVoice (Voice& v);

    // state
    std::unique_ptr<juce::XmlElement> coreEl;              // engine state as XML, rebuilt by the render thread
    mutable std::mutex stateLock;
    std::array<juce::MemoryBlock, 4> slotFlac; std::array<double, 4> slotFlacRate {}, slotFlacGain {}; std::array<juce::String, 4> slotFlacKey;
    void rebuildCoreXml();
    void restoreCore (const juce::XmlElement& x, bool asPreset = false);
    juce::String presetName; int presetEntry = -1;          // render thread
    std::atomic<bool> presetDirty { false };                // a sound knob moved since the preset loaded or saved
    bool writePreset (const juce::File& f, const juce::String& name, juce::String& err);
    juce::File outDir (const juce::String& sub) const;
    void renderHi();
    std::atomic<bool> abortHi { false };
    std::shared_ptr<const hl::Out> hiFor, hiOut, hiTried; uint32_t hiSeed = 0;   // hiFor/hiOut under stateLock
    // each slot's audio at 96 kHz, read from disk once per load (not on every idle moment); render thread only
    struct HiSlot { std::string key; std::vector<float> data; std::unique_ptr<hl::Mip> mip; };
    std::array<HiSlot, 4> hiSlots;
    juce::uint32 lastWork = 0;
    void writeWav (const juce::File& f, const std::vector<float>& L, const std::vector<float>& R, double rate, int rootNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalfLifeProcessor)
};
