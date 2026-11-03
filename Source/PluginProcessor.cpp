#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

using namespace hl;

static juce::String noteLabel (int n)
{
    static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (N[((n % 12) + 12) % 12]) + juce::String ((int) std::floor (n / 12.0) - 2);   // DAW convention: MIDI 60 = C3
}
static juce::String hex6 (uint32_t s) { return juce::String::toHexString ((juce::int64) s).paddedLeft ('0', 8).toUpperCase().substring (0, 6); }

/* ================= construction ================= */
HalfLifeProcessor::HalfLifeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      Thread ("Half Life render"),
      apvts (*this, nullptr, "HalfLife", hlp::createLayout())
{
    formats.registerBasicFormats();
    for (auto* p : getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) apvts.addParameterListener (rp->paramID, this);
    uiShared = std::make_shared<UiState>();
    post ([this]
    {
        const char* ex[] = { "Struck plate", "Noise burst", "Glass drone", "Wood ticks" };
        for (int i = 0; i < 4; ++i) engine.loadBuiltin (i, ex[i], true);
        engine.boot();
        publishSound();
        rebuildCoreXml();
    });
    startThread (juce::Thread::Priority::normal);
}

HalfLifeProcessor::~HalfLifeProcessor()
{
    signalThreadShouldExit(); wake.signal();
    stopThread (8000);
    for (auto* p : getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) apvts.removeParameterListener (rp->paramID, this);
}

bool HalfLifeProcessor::isBusesLayoutSupported (const BusesLayout& l) const { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* HalfLifeProcessor::createEditor() { return new HalfLifeEditor (*this); }

/* ================= the render thread ================= */
void HalfLifeProcessor::post (std::function<void()> fn, bool changesState)
{
    { std::lock_guard<std::mutex> l (qLock); queue.push_back ({ std::move (fn), changesState }); }
    abortHi = true;   // a command wins over the background 96 kHz render
    wake.signal();
}

void HalfLifeProcessor::syncSettings() { engine.P = hlp::readSettings (apvts); }

void HalfLifeProcessor::setStatus (const juce::String& s)
{
    { const juce::SpinLock::ScopedLockType l (statusLock); status = s; }
    statusN++;
    sendChangeMessage();
}

void HalfLifeProcessor::parameterChanged (const juce::String& id, float)
{
    if (! suppressRerender.load() && id != "volume") presetDirty = true;   // anything a preset stores, changed by hand
    if (suppressRerender.load() || ! hlp::isRerenderParam (id)) return;
    rerenderAt = juce::Time::getMillisecondCounter() + 140;   // debounced, like the prototype
    abortHi = true;
    wake.signal();
}

void HalfLifeProcessor::run()
{
    lastTick = juce::Time::getMillisecondCounter();
    juce::uint32 lastPub = 0;
    while (! threadShouldExit())
    {
        wake.wait (40);
        for (;;)
        {
            std::function<void()> fn; bool changes = true;
            { std::lock_guard<std::mutex> l (qLock); if (queue.empty()) break; fn = std::move (queue.front().first); changes = queue.front().second; queue.pop_front(); }
            busy = true; sendChangeMessage();
            syncSettings();
            fn();
            busy = false; lastWork = juce::Time::getMillisecondCounter();
            publish(); if (changes) rebuildCoreXml();   // a state the host just set is kept byte for byte until something changes
            if (threadShouldExit()) return;
        }
        const auto now = juce::Time::getMillisecondCounter();
        if (const auto due = rerenderAt.load(); due != 0 && now >= due)
        {
            rerenderAt = 0; busy = true; sendChangeMessage(); syncSettings();
            if (engine.rerender()) publishSound();
            busy = false; lastWork = juce::Time::getMillisecondCounter(); publish(); rebuildCoreXml();
        }
        buildWanted();
        // the live chaos keeps moving between sounds
        const auto t = juce::Time::getMillisecondCounter();
        if (t - lastTick >= 50)
        {
            const double dt = juce::jmin (0.5, (t - lastTick) / 1000.0); lastTick = t;
            syncSettings(); engine.tick (dt);
            Live l;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) { l.h[i][j] = (float) chOut (engine.HNET[(size_t) i], j); l.s[i][j] = (float) chOut (engine.slots[(size_t) i].glide.c, j); }
            for (int k = 0; k < NH; ++k) l.hv[k] = (float) engine.live[(size_t) k];
            for (int i = 0; i < 4; ++i) l.g[i] = (float) engine.slots[(size_t) i].glide.val;
            { const juce::SpinLock::ScopedLockType k (liveLock); liveV = l; }
        }
        // quiet for a moment: prepare the 96 kHz version for dragging out
        if (engine.current && engine.current->out && hiTried != engine.current->out && rerenderAt.load() == 0 && t - lastWork > 700)
        {
            bool idle; { std::lock_guard<std::mutex> l (qLock); idle = queue.empty(); }
            if (idle) { abortHi = false; renderHi(); }
        }
        juce::ignoreUnused (lastPub);
        // free sounds nobody plays any more
        while (keepAlive.size() > 2 && keepAlive.front() != currentBuf && keepAlive.front()->users.load() == 0) keepAlive.pop_front();
        for (auto it = keepAlive.begin(); it != keepAlive.end();) { if (*it != currentBuf && (*it)->users.load() == 0 && keepAlive.size() > 2) it = keepAlive.erase (it); else ++it; }
    }
}

/* the newest sound goes to the audio thread */
void HalfLifeProcessor::publishSound()
{
    if (! engine.current || ! engine.current->out) return;
    if (currentBuf && currentBuf->out == engine.current->out) return;
    auto pb = std::make_shared<PlayBuf>(); pb->out = engine.current->out; pb->id = engine.current->id;
    keepAlive.push_back (pb); currentBuf = pb;
    playing.store (pb.get());
}

void HalfLifeProcessor::audition() { auditionReq++; }

/* band-limited copies for keys above the root, built when a key asks for one (and a few ahead of time) */
void HalfLifeProcessor::buildWanted()
{
    auto pb = currentBuf; if (! pb) return;
    auto build = [&] (int h)
    {
        const auto& o = *pb->out;
        pb->lvL[(size_t) h] = std::make_unique<Level> (buildLevel (o.L.data(), o.end, h, 0, o.end));
        pb->lvR[(size_t) h] = std::make_unique<Level> (buildLevel (o.R.data(), o.end, h, 0, o.end));
        pb->ready[(size_t) h].store (true, std::memory_order_release);
    };
    for (int h = 1; h < PlayBuf::NL; ++h) if (pb->wanted[(size_t) h].load() && ! pb->ready[(size_t) h].load()) build (h);
    for (int h = 1; h <= 8; ++h) if (! pb->ready[(size_t) h].load()) { build (h); break; }   // one per pass, up to +2 octaves
}

/* ================= commands ================= */
void HalfLifeProcessor::fresh()
{
    post ([this]
    {
        if (poolUsable() && engine.P.fresh != FreshDraw::Keep) drawPool (engine.P.fresh == FreshDraw::All);
        if (engine.generate (true)) { publishSound(); audition(); }
        setStatus (engine.status);
    });
}

void HalfLifeProcessor::mutate()
{
    post ([this] { if (engine.generate (false)) { publishSound(); audition(); } setStatus (engine.status); });
}

void HalfLifeProcessor::relink()
{
    post ([this] { if (engine.relink()) { publishSound(); audition(); setStatus (engine.status); } });
}

void HalfLifeProcessor::perish()
{
    // the knobs: randomised on the message thread (they're host parameters), without re-rendering for each one
    auto& R = juce::Random::getSystemRandom();
    auto r = [&] { return (double) R.nextDouble(); };
    auto lin = [&] (double a, double b) { return a + (b - a) * r(); };
    auto lg = [&] (double a, double b) { return a * std::pow (b / a, r()); };
    auto set = [&] (const char* id, double v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) v)); };
    auto pick = [&] (std::initializer_list<int> l) { std::vector<int> v (l); return v[(size_t) (int) (r() * v.size())]; };
    suppressRerender = true;
    const char* roles[] = { "roleA", "roleB", "roleC", "roleD" };
    for (auto id : roles) set (id, pick ({ 0, 0, 1, 2, 3, 4 }));
    set ("noiseOn", r() < 0.7); set ("noiseType", pick ({ 0, 1, 2, 3, 4 })); set ("noiseRole", pick ({ 0, 1, 2, 3 }));
    set ("clickOn", r() < 0.6); set ("blipOn", r() < 0.35);
    set ("instability", lin (0.1, 1)); set ("mutation", lin (0.1, 0.8)); set ("glide", lin (0, 1)); set ("interact", lin (0, 1));
    set ("rot", r() < 0.5 ? lin (0, 0.3) : lin (0.3, 1)); set ("rotOnset", lin (0, 0.8));
    set ("lpgResp", lg (0.4, 8)); set ("reso", lin (0, 0.8)); set ("bright", lg (0.4, 1.6)); set ("body", lin (0, 0.5)); set ("bodyTime", lg (0.5, 6));
    set ("pitchAmt", r() < 0.5 ? 0 : std::round (lin (-12, 36) * 2) / 2); set ("pitchTime", lg (10, 600)); set ("pitchCurve", lg (0.5, 2.5));
    set ("attack", lg (0.5, 20)); set ("decay", lg (100, 3000)); set ("sustain", lin (0, 1)); set ("release", lg (20, 800));
    set ("maxLen", lin (1, 5)); set ("noiseLevel", lin (0, 0.7)); set ("scatter", r() < 0.5 ? 0.0 : lin (0, 0.6));
    set ("shpA", r() < 0.75 ? lg (0.5, 15) : lg (15, 600)); set ("shpD", lg (80, 3000)); set ("shpS", r() < 0.7 ? 0.0 : lin (0.1, 0.9)); set ("shpR", lg (50, 2000)); set ("shpFollow", lin (0.3, 1));
    set ("resType", pick ({ 0, 1, 1, 2 })); set ("resStruct", pick ({ 0, 1, 2, 3, 4 }));
    set ("resAmt", lin (0, 0.85)); set ("resPitch", std::round (lin (30, 84))); set ("resRand", lin (0, 12)); set ("resDamp", lin (0, 1));
    set ("resVoices", std::round (lin (1, 6))); set ("resSpread", std::round (lin (0, 30))); set ("resMix", r() < 0.3 ? 0 : lin (0.1, 0.7));
    set ("distType", pick ({ 0, 1, 1, 2, 3, 4 })); set ("distDrive", lin (0, 24)); set ("distMix", lin (0.4, 1)); set ("distTone", r() < 0.5 ? 1 : lin (0.3, 1));
    set ("satAsym", lin (0, 0.8)); set ("satKnee", lin (0, 1)); set ("shCurve", lin (-1, 1)); set ("shH2", lin (0, 0.6)); set ("shH3", lin (0, 0.6)); set ("shH5", lin (0, 0.4));
    set ("shSteps", r() < 0.7 ? 0 : std::round (lin (3, 16)));
    set ("fdFolds", lin (1, 6)); set ("fdBias", lin (-0.5, 0.5)); set ("fdShape", lin (0, 1)); set ("pdDepth", lin (0.1, 0.9)); set ("pdSmooth", lin (0, 1)); set ("pdFb", lin (0, 0.6));
    set ("chaosSpeed", lg (0.03, 0.8)); set ("chaosTurb", r() < 0.2 ? 0 : lg (0.3, 6)); set ("chaosCouple", r() < 0.3 ? lin (1, 3) : lin (0, 0.8));
    set ("target", pick ({ 0, 1, 2, 3, 4, 5 })); set ("lockLinks", 0);
    suppressRerender = false;
    rerenderAt = 0;
    post ([this]
    {
        engine.perishSources (poolUsable(), engine.P.slotRole);
        if (poolUsable()) drawPool (true);
        if (engine.generate (true)) { publishSound(); audition(); }
        setStatus ("Perished. Everything rerolled. " + juce::String (engine.status));
    });
}

void HalfLifeProcessor::trigger() { triggerReq++; }

void HalfLifeProcessor::recall (int entryId)
{
    post ([this, entryId]
    {
        for (auto& e : engine.history) if (e->id == entryId && e->out) { engine.current = e; publishSound(); audition(); return; }
    });
}

void HalfLifeProcessor::keyDown (int note) { const auto w = keyFifo.write (1); if (w.blockSize1 > 0) keyEvents[(size_t) w.startIndex1] = note + 1; }
void HalfLifeProcessor::keyUp (int note) { const auto w = keyFifo.write (1); if (w.blockSize1 > 0) keyEvents[(size_t) w.startIndex1] = -(note + 1); }

void HalfLifeProcessor::loadFile (int slot, const juce::File& f)
{
    post ([this, slot, f]
    {
        const auto before = engine.slots[(size_t) slot].data.size();
        loadSlotFromFile (slot, f, false, -1, -1, -1);
        auto& sl = engine.slots[(size_t) slot];
        if (! sl.has() || (sl.data.size() == before && sl.path != f.getFullPathName().toStdString())) { setStatus ("Couldn't read " + f.getFileName() + ". Try a WAV, AIFF, FLAC, MP3 or OGG file."); return; }
        const auto role = engine.P.slotRole[(size_t) slot];
        const auto v = engine.validChain (sl.an, role == SlotRole::Auto ? Role::Body : (Role) ((int) role - 1));
        setStatus (v.empty() ? juce::String (sl.name) + " is almost entirely below the scan threshold."
                             : "Loaded " + juce::String (sl.name) + " into " + juce::String (keyName (slot)) + ". Press Fresh to hear it in a new sound.");
        if (engine.rerender()) publishSound();
    });
}

void HalfLifeProcessor::loadBuiltin (int slot, const juce::String& name)
{
    post ([this, slot, name]
    {
        engine.loadBuiltin (slot, name.toStdString(), false);
        setStatus (name + " loaded into " + juce::String (keyName (slot)) + ". Press Fresh to hear it in a new sound.");
        if (engine.rerender()) publishSound();
    });
}

void HalfLifeProcessor::clearSlot (int slot) { post ([this, slot] { engine.slots[(size_t) slot].clear(); if (engine.rerender()) publishSound(); }); }

void HalfLifeProcessor::togglePin (int slot)
{
    post ([this, slot]
    {
        auto& s = engine.slots[(size_t) slot]; s.pinned = ! s.pinned;
        setStatus (s.pinned ? "Slot " + juce::String (keyName (slot)) + " pinned: Perish and Fresh keep its sample." : "Slot " + juce::String (keyName (slot)) + " unpinned.");
    });
}

void HalfLifeProcessor::toggleLock (int key) { post ([this, key] { if (engine.locks.count (key)) engine.locks.erase (key); else engine.locks.insert (key); }); }

void HalfLifeProcessor::rerouteGlide (int slot)
{
    post ([this, slot] { engine.rerouteGlide (slot); setStatus ("Slot " + juce::String (keyName (slot)) + " glide rerouted. Takes effect on the next Fresh or Mutate."); });
}

void HalfLifeProcessor::rerouteChaos() { post ([this] { engine.rerouteChaos(); setStatus ("Chaos rerouted. Fresh or Mutate to hear it."); }); }

/* ================= reading files ================= */
static constexpr double kMaxSec = 120;   // longest part of a sample file that is kept (2 minutes)

bool HalfLifeProcessor::decodeFile (const juce::File& f, double rate, std::vector<float>& out, double maxSec, double ra, double rb, double* peakDb, double* dur, const std::atomic<bool>* stop)
{
    std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
    if (! rd || rd->lengthInSamples <= 0 || rd->sampleRate <= 0) return false;
    const double fr = rd->sampleRate;
    const juce::int64 total = rd->lengthInSamples;
    const juce::int64 o = ra >= 0 ? juce::jmin (total - 1, (juce::int64) std::floor (ra * fr)) : 0;
    juce::int64 n = juce::jmin (total - o, (juce::int64) (maxSec * fr));
    if (rb > ra && ra >= 0) n = juce::jmin (n, (juce::int64) std::ceil ((rb - ra) * fr));
    if (n <= 0) return false;
    const int ch = (int) juce::jmax (1u, rd->numChannels);
    juce::AudioBuffer<float> b (ch, (int) n);
    // in pieces, so a long file can be abandoned part-way when something more urgent comes in
    for (juce::int64 at = 0; at < n; at += 1 << 18)
    {
        if (stop && stop->load()) return false;
        const int len = (int) juce::jmin ((juce::int64) (1 << 18), n - at);
        if (! rd->read (&b, (int) at, len, o + at, true, ch > 1)) return false;
    }
    std::vector<float> mono ((size_t) n);
    for (int c = 0; c < ch; ++c) { const float* p = b.getReadPointer (c); for (juce::int64 i = 0; i < n; ++i) mono[(size_t) i] += p[i] / ch; }
    out = std::abs (fr - rate) < 0.5 ? std::move (mono) : resample (mono, rate / fr);
    if (dur) *dur = (double) out.size() / rate;
    if (peakDb)
    {
        // the loudest 10 ms, in dBFS: the pool's loudness gate compares against this
        const int w = juce::jmax (1, juce::roundToInt (rate * 0.01)); double pk = 0; const int N = (int) out.size();
        for (int s = 0; s + w <= N; s += w) { double q = 0; for (int i = s; i < s + w; ++i) q += (double) out[(size_t) i] * out[(size_t) i]; pk = juce::jmax (pk, q / w); }
        if (N < w) { double q = 0; for (int i = 0; i < N; ++i) q += (double) out[(size_t) i] * out[(size_t) i]; pk = q / juce::jmax (1, N); }
        *peakDb = 10 * std::log10 (pk + 1e-12);
    }
    return true;
}

void HalfLifeProcessor::loadSlotFromFile (int i, const juce::File& f, bool fromPool, double ra, double rb, int poolIdx)
{
    std::vector<float> d;
    if (! decodeFile (f, engine.rate(), d, kMaxSec, ra, rb)) return;
    engine.setSlotData (i, std::move (d), f.getFileNameWithoutExtension().toStdString(), false);
    auto& sl = engine.slots[(size_t) i];
    sl.path = f.getFullPathName().toStdString(); sl.fromPool = fromPool; sl.regionA = ra; sl.regionB = rb; sl.poolFile = poolIdx; sl.example = false;
}

/* ================= the sample pool ================= */
bool HalfLifeProcessor::poolUsable() const { for (auto& f : pool) if (f.st != 2 && f.st != 3) return true; return false; }

juce::String HalfLifeProcessor::poolInfoText() const
{
    const int n = (int) pool.size(); int q = 0, bad = 0, ok = 0;
    for (auto& f : pool) { if (f.st == 2) q++; if (f.st == 3) bad++; if (f.st == 1) ok++; }
    if (! n) return "Drop a folder anywhere on the window, or choose one. Perish and Fresh draw from it.";
    juce::String s = juce::String (n) + " file" + (n == 1 ? "" : "s") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (ok) + " drawn so far";
    if (q) s << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << q << " too quiet";
    if (bad) s << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << bad << " unreadable";
    return s;
}

void HalfLifeProcessor::poolAdd (const juce::Array<juce::File>& list)
{
    post ([this, list]
    {
        const juce::String exts ("*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.mp3;*.ogg");
        std::set<juce::String> seen; for (auto& f : pool) seen.insert (f.file.getFullPathName());
        int added = 0;
        auto addOne = [&] (const juce::File& f, const juce::File& root)
        {
            if (! f.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;mp3;ogg")) return;
            if (seen.count (f.getFullPathName())) return;
            seen.insert (f.getFullPathName());
            pool.push_back ({ f, root.isDirectory() ? f.getRelativePathFrom (root.getParentDirectory()) : f.getFileName(), f.getFileNameWithoutExtension(), 0 });
            added++;
        };
        for (auto& f : list)
        {
            if (f.isDirectory()) { for (auto& c : f.findChildFiles (juce::File::findFiles, true, exts)) addOne (c, f); }
            else addOne (f, f);
        }
        setStatus (added ? juce::String (added) + " file" + (added == 1 ? "" : "s") + " added to the pool. Perish or Fresh to draw from it." : "No new audio files found there.");
    });
}

void HalfLifeProcessor::poolClear() { post ([this] { pool.clear(); poolCache.clear(); setStatus ("Pool cleared. Perish uses the built-in sounds again."); }); }

std::shared_ptr<HalfLifeProcessor::Decoded> HalfLifeProcessor::poolDecode (int idx)
{
    for (auto it = poolCache.begin(); it != poolCache.end(); ++it)
        if (it->first == idx) { auto v = it->second; poolCache.erase (it); poolCache.push_back ({ idx, v }); return v; }
    auto v = std::make_shared<Decoded>();
    if (! decodeFile (pool[(size_t) idx].file, engine.rate(), v->d, 120, -1, -1, &v->peakDb, &v->dur)) return nullptr;
    poolCache.push_back ({ idx, v }); while (poolCache.size() > 10) poolCache.pop_front();
    return v;
}

int HalfLifeProcessor::drawInto (int si, std::set<int>& taken)
{
    auto& sl = engine.slots[(size_t) si];
    const auto sr = engine.P.slotRole[(size_t) si];
    const Role role = sr == SlotRole::Auto ? (Role) (int) (rnd() * 4) : (Role) ((int) sr - 1);
    const double fit = engine.P.poolFit; const int want = std::abs (fit) < 0.02 ? 1 : 5;
    std::vector<int> cand;
    for (int i = 0; i < (int) pool.size(); ++i) if (pool[(size_t) i].st != 2 && pool[(size_t) i].st != 3 && ! taken.count (i) && i != sl.poolFile) cand.push_back (i);
    if (cand.empty()) for (int i = 0; i < (int) pool.size(); ++i) if (pool[(size_t) i].st != 2 && pool[(size_t) i].st != 3) cand.push_back (i);
    struct C { int f; std::vector<float> seg; double a, b, score; bool region; };
    std::vector<C> cs;
    for (int tries = 0; tries < 24 && (int) cs.size() < want && ! cand.empty(); ++tries)
    {
        const int k = (int) (rnd() * cand.size()); const int fi = cand[(size_t) k];
        auto v = poolDecode (fi);
        if (! v || v->d.empty()) { pool[(size_t) fi].st = 3; cand.erase (cand.begin() + k); continue; }
        if (v->peakDb < engine.P.poolGate) { pool[(size_t) fi].st = 2; cand.erase (cand.begin() + k); continue; }
        double a = 0, b = std::min (v->dur, kMaxSec);   // the whole file: every Fresh can pick from anywhere in it
        const double SR = engine.rate();
        const size_t ia = (size_t) std::floor (a * SR), ib = std::min (v->d.size(), (size_t) std::floor (b * SR));
        if (ib <= ia + 16) continue;
        std::vector<float> seg (v->d.begin() + (long) ia, v->d.begin() + (long) ib);
        const auto an = Engine::analyze (seg, SR);
        if (engine.validChain (an, role).empty()) continue;
        pool[(size_t) fi].st = 1;
        cs.push_back ({ fi, std::move (seg), a, b, Engine::fitScore (an, role), a > 0 || b < v->dur - 0.01 });
    }
    if (cs.empty()) return -1;
    std::vector<double> w; double tot = 0; for (auto& c : cs) { w.push_back (std::exp (3 * fit * c.score)); tot += w.back(); }
    double x = rnd() * tot; size_t pick = 0; for (size_t i = 0; i < cs.size(); ++i) { x -= w[i]; if (x <= 0) { pick = i; break; } }
    auto& c = cs[pick];
    engine.setSlotData (si, c.seg, pool[(size_t) c.f].name.toStdString(), false);
    sl.path = pool[(size_t) c.f].file.getFullPathName().toStdString(); sl.fromPool = true; sl.poolFile = c.f; sl.example = false;
    sl.regionA = c.region ? c.a : -1; sl.regionB = c.region ? c.b : -1;
    return c.f;
}

void HalfLifeProcessor::drawPool (bool all)
{
    if (! poolUsable()) return;
    std::vector<int> free; for (int i = 0; i < 4; ++i) if (! engine.slots[(size_t) i].pinned) free.push_back (i);
    if (free.empty()) { setStatus ("Every slot is pinned, nothing to draw."); return; }
    std::vector<int> targets = all ? free : std::vector<int> { free[(size_t) (int) (rnd() * free.size())] };
    std::set<int> taken;
    for (int i = 0; i < 4; ++i) if (engine.slots[(size_t) i].fromPool && std::find (targets.begin(), targets.end(), i) == targets.end()) taken.insert (engine.slots[(size_t) i].poolFile);
    juce::StringArray got;
    for (int si : targets) { const int f = drawInto (si, taken); if (f >= 0) { taken.insert (f); got.add (juce::String (keyName (si)) + juce::String (": ") + pool[(size_t) f].name); } }
    setStatus (got.size() ? "Drew " + got.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))) : "Nothing in the pool passed the loudness gate. Lower it, or add louder files.");
}

/* ================= export by dragging the waveform =================
   When things have been quiet for a moment, the render thread re-renders the current sound at 96 kHz and keeps it in
   memory (nothing is written to disk). Dragging the waveform then only has to pitch it to the last played note, bake in
   the amp envelope and write one WAV, which takes a few milliseconds. If the 96 kHz version isn't ready yet (you drag
   right after Fresh), the file is written from the sound as it plays, at the host's rate. */
void HalfLifeProcessor::renderHi()
{
    auto cur = engine.current; if (! cur || ! cur->out) return;
    const double keepRate = engine.rate(), ER = 96000, ratio = ER / keepRate;
    struct Keep { std::vector<float> data; std::unique_ptr<Mip> mip; Analysis an; };
    std::array<Keep, 4> keep;
    for (int i = 0; i < 4; ++i) { auto& s = engine.slots[(size_t) i]; keep[(size_t) i] = { std::move (s.data), std::move (s.mip), s.an }; }
    engine.setRate (ER);
    bool stopped = false;
    for (int i = 0; i < 4 && ! stopped; ++i)
    {
        auto& s = engine.slots[(size_t) i]; auto& k = keep[(size_t) i]; auto& hc = hiSlots[(size_t) i];
        if (k.data.empty()) continue;
        const std::string key = (s.example ? "ex:" + s.name : s.path + "|" + std::to_string (s.regionA) + "|" + std::to_string (s.regionB)) + "|" + std::to_string (k.data.size());
        if (hc.key != key || hc.data.empty() || ! hc.mip)
        {
            std::vector<float> hi;
            if (s.example) hi = engine.makeBuiltin (s.name, ER);
            else
            {
                const juce::File f (s.path);
                if (! (f.existsAsFile() && decodeFile (f, ER, hi, kMaxSec, s.regionA, s.regionB, nullptr, nullptr, &abortHi)))
                {
                    if (abortHi.load()) { stopped = true; break; }
                    hi = resample (k.data, ratio);
                }
            }
            engine.dcBlockNow (hi);
            hc.data = std::move (hi); hc.mip = std::make_unique<Mip> (hc.data.data(), (int) hc.data.size()); hc.key = key;
        }
        // borrow the cached copy for this render (moving a vector keeps its buffer, so the Mip stays valid)
        s.data = std::move (hc.data); s.mip = std::move (hc.mip);
        s.an = k.an; s.an.win = (int) std::round (k.an.win * ratio);
    }
    std::shared_ptr<const Out> out;
    if (! stopped)
    {
        Genome g = cur->genome;
        engine.abort = &abortHi;
        out = engine.render (g);
        engine.abort = nullptr;
    }
    engine.setRate (keepRate);
    for (int i = 0; i < 4; ++i)
    {
        auto& s = engine.slots[(size_t) i]; auto& k = keep[(size_t) i]; auto& hc = hiSlots[(size_t) i];
        if (! s.data.empty() && s.mip && hc.data.empty()) { hc.data = std::move (s.data); hc.mip = std::move (s.mip); }   // give the cached copy back
        s.data = std::move (k.data); s.mip = std::move (k.mip); s.an = k.an; if (s.has() && ! s.mip) s.mip = std::make_unique<Mip> (s.data.data(), (int) s.data.size());
    }
    if (stopped) { hiTried = nullptr; return; }
    hiTried = cur->out;
    if (! out || abortHi.load()) { if (abortHi.load()) hiTried = nullptr; return; }
    const std::lock_guard<std::mutex> l (stateLock);
    hiFor = cur->out; hiOut = out; hiSeed = cur->genome.seed;
}

juce::File HalfLifeProcessor::makeDragFile()
{
    auto st = ui(); if (! st || ! st->out) return {};
    std::shared_ptr<const Out> src = st->out; double rate = src->sampleRate; bool hi = false;
    {
        const std::lock_guard<std::mutex> l (stateLock);
        if (hiFor == st->out && hiOut) { src = hiOut; rate = 96000; hi = true; }
    }
    const int note = lastNote.load();
    const int semis = note - 60; const double r = std::pow (2.0, semis / 12.0);
    std::vector<float> L, R;
    if (semis == 0) { L.assign (src->L.begin(), src->L.begin() + src->end); R.assign (src->R.begin(), src->R.begin() + src->end); }
    else
    {
        const int n = juce::jmax (1, (int) std::floor ((src->end - 3) / r)), h = levelFor (r);
        const Level lvL = buildLevel (src->L.data(), src->end, h, 0, src->end), lvR = buildLevel (src->R.data(), src->end, h, 0, src->end);
        L.resize ((size_t) n); R.resize ((size_t) n);
        for (int i = 0; i < n; ++i) { L[(size_t) i] = (float) lvRead (lvL, i * r); R[(size_t) i] = (float) lvRead (lvR, i * r); }
    }
    auto pv = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    const double A = std::max (0.0005, pv ("attack") / 1000), D = std::max (0.002, pv ("decay") / 1000), S = pv ("sustain"), Rl = std::max (0.005, pv ("release") / 1000);
    const double dur = L.size() / rate, rs = std::max (A, dur - Rl);
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double t = i / rate; double e = t < A ? t / A : S + (1 - S) * std::exp (-(t - A) / (D / 4));
        if (t > rs) e *= std::exp (-(t - rs) / (Rl / 4));
        L[i] = (float) (L[i] * e); R[i] = (float) (R[i] * e);
    }
    // the name carries a fingerprint of the audio: dragging the same sound at the same note twice reuses the file
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint64 v) { h = (h ^ v) * 1099511628211ull; };
    mix ((juce::uint64) L.size()); mix ((juce::uint64) rate);
    for (size_t i = 0; i < L.size(); i += 7) { juce::int32 a, b; std::memcpy (&a, &L[i], 4); std::memcpy (&b, &R[i], 4); mix ((juce::uint32) a); mix ((juce::uint32) b); }
    const auto tag = juce::String::toHexString ((juce::int64) (h & 0xffffffffull)).paddedLeft ('0', 8).toUpperCase();
    auto f = outDir ("Exports").getChildFile ("half-life_" + hex6 (st->seed) + "_" + noteLabel (note).replace ("#", "s") + (hi ? "" : "_" + juce::String (juce::roundToInt (rate / 1000)) + "k") + "_" + tag + ".wav");
    if (! f.existsAsFile()) writeWav (f, L, R, rate, note);
    setStatus ("Dragged out at " + noteLabel (note) + (hi ? juce::String (" (96 kHz, 24-bit)") : " (" + juce::String (rate / 1000, 1) + " kHz: the 96 kHz version wasn't ready yet)") + ". A copy is in Documents/Half Life/Exports.");
    return f;
}

juce::File HalfLifeProcessor::outDir (const juce::String& sub) const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Half Life").getChildFile (sub);
    d.createDirectory();
    return d;
}

/* 24-bit PCM with TPDF dither; with a root note it also writes a 'smpl' chunk so samplers map the file to that key */
void HalfLifeProcessor::writeWav (const juce::File& f, const std::vector<float>& L, const std::vector<float>& R, double rate, int root)
{
    juce::MemoryOutputStream m;
    const int n = (int) L.size(), ba = 6, sm = root < 0 ? 0 : 44;
    auto u32 = [&] (juce::uint32 v) { m.writeInt ((int) v); };
    auto u16 = [&] (int v) { m.writeShort ((short) v); };
    m.write ("RIFF", 4); u32 ((juce::uint32) (36 + sm + n * ba)); m.write ("WAVE", 4); m.write ("fmt ", 4);
    u32 (16); u16 (1); u16 (2); u32 ((juce::uint32) rate); u32 ((juce::uint32) (rate * ba)); u16 (ba); u16 (24);
    if (sm) { m.write ("smpl", 4); u32 (36); u32 (0); u32 (0); u32 ((juce::uint32) std::round (1e9 / rate)); u32 ((juce::uint32) root); for (int i = 0; i < 5; ++i) u32 (0); }
    m.write ("data", 4); u32 ((juce::uint32) (n * ba));
    auto& rnd2 = juce::Random::getSystemRandom();
    for (int i = 0; i < n; ++i)
        for (const auto* ch : { &L, &R })
        {
            const int s = juce::jlimit (-8388608, 8388607, (int) std::round ((*ch)[(size_t) i] * 8388607.0 + rnd2.nextDouble() - rnd2.nextDouble()));
            const char b[3] = { (char) (s & 255), (char) ((s >> 8) & 255), (char) ((s >> 16) & 255) };
            m.write (b, 3);
        }
    f.replaceWithData (m.getData(), m.getDataSize());
}

std::function<double (double)> HalfLifeProcessor::distCurve() const
{
    return distFnOf (hlp::readSettings (const_cast<juce::AudioProcessorValueTreeState&> (apvts)));
}

/* ================= what the window sees ================= */
void HalfLifeProcessor::publish()
{
    auto u = std::make_shared<UiState>();
    const auto& P = engine.P;
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    for (int i = 0; i < 4; ++i)
    {
        const auto& sl = engine.slots[(size_t) i]; auto& s = u->slots[(size_t) i];
        s.has = sl.has(); s.example = sl.example; s.fromPool = sl.fromPool; s.pinned = sl.pinned; s.name = sl.has() ? juce::String (sl.name) : juce::String ("Empty slot"); s.path = sl.path;
        if (sl.has())
        {
            const auto sr = P.slotRole[(size_t) i];
            const Role show = sr == SlotRole::Auto ? Role::Body : (Role) ((int) sr - 1);
            const auto valid = engine.validChain (sl.an, show), scan = engine.validFor (sl.an, show == Role::Transient ? Role::Body : show);
            std::set<int> vs (valid.begin(), valid.end()), ss (scan.begin(), scan.end());
            s.db.assign (sl.an.db.begin(), sl.an.db.end()); s.on.assign ((size_t) sl.an.n, 0); s.onsets = sl.an.onsets;
            for (int k = 0; k < sl.an.n; ++k) s.on[(size_t) k] = (char) (sr == SlotRole::Transient ? (vs.count (k) || ss.count (k)) : vs.count (k));
            if (valid.empty()) { s.meta = "Too quiet for this role" + dot + "skipped"; s.metaBad = true; }
            else s.meta = juce::String (sl.an.n * 0.01, 2) + " s" + dot + juce::String (juce::roundToInt ((double) ss.size() / sl.an.n * 100)) + "% scannable as " + roleName (show) + dot + juce::String ((int) sl.an.onsets.size()) + " hits";
        }
        else s.meta = "Empty";
        const double a = sl.glide.c.a;
        s.glideKind = a == 12 ? "spiral" : a == 14 ? "torn" : a == 15.6 ? "double scroll" : a == 16.5 ? "wide double scroll" : "chaos";
        juce::StringArray rt;
        for (auto& r : sl.glide.routes) rt.add (juce::String (GLABEL[r.dest]) + " " + (r.depth > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (juce::roundToInt (std::abs (r.depth) * 100)) + "% (" + juce::String::charToString ("xyz"[r.comp]) + ")");
        s.glideRoutes = rt.joinIntoString (dot);
    }
    if (auto e = engine.current)
    {
        u->out = e->out; u->id = e->id; u->seed = e->genome.seed; u->rot = e->genome.rot;
        std::set<int> avail; for (auto& s : engine.sources()) avail.insert (s.key);
        std::set<int> skipped; if (e->out) skipped.insert (e->out->skipped.begin(), e->out->skipped.end());
        const char* nt[] = { "white", "pink", "brown", "crackle", "velvet" };
        for (const auto& L : e->genome.layers)
        {
            if (! avail.count (L.key)) continue;
            UiState::LayerUi l; l.key = L.key; l.role = roleName (L.role); l.locked = engine.locks.count (L.key) > 0; l.skipped = skipped.count (L.key) > 0;
            l.name = L.kind == Kind::Slot ? juce::String (keyName (L.key)) + dot + juce::String (engine.slots[(size_t) L.key].name) : L.kind == Kind::Noise ? "Noise" + dot + nt[(int) P.noiseType] : L.kind == Kind::Click ? "Click" : "Blip";
            const double gate = engine.layerGateMs (L, e->genome);
            l.params = l.skipped ? juce::String ("Nothing above threshold for this role")
                     : L.kind == Kind::Slot ? "gate " + juce::String (juce::roundToInt (gate)) + " ms" + dot + "grain " + juce::String (juce::roundToInt (L.grain)) + " ms" + dot + (L.pitch > 0 ? "+" : "") + juce::String (L.pitch, 1) + " st"
                     : "gate " + juce::String (juce::roundToInt (gate)) + " ms" + dot + "start " + juce::String (juce::roundToInt (L.start)) + " ms";
            u->layers.push_back (l);
        }
        for (int k : e->genome.order) if (avail.count (k)) u->order.push_back (k);
        const double fac = std::min (1.0, P.interact * 1.6);
        auto nm = [] (int k) { return juce::String (keyName (k)); };
        const char* DL[] = { "density", "scan position", "pitch", "brightness", "level", "gate" };
        for (const auto& l : e->genome.links)
        {
            if (! avail.count (l.from) || ! avail.count (l.to)) continue;
            UiState::LinkUi x; x.from = l.from; x.to = l.to; x.clock = l.type == LinkType::Clock; x.amount = l.amount;
            const int pct = juce::roundToInt (std::abs (l.amount) * fac * 100);
            x.label = nm (l.from) + " > " + nm (l.to) + " " + juce::String (pct) + "%";
            x.text = x.clock ? nm (l.from) + "'s hits re-strike " + nm (l.to) + "'s gate"
                   : l.dest == LDuck ? nm (l.from) + " ducks " + nm (l.to) + " when it's loud"
                   : nm (l.from) + "'s loudness " + (l.amount > 0 ? "raises " : "lowers ") + nm (l.to) + "'s " + DL[l.dest];
            u->links.push_back (x);
        }
        u->nLinks = (int) u->links.size();
    }
    for (auto& h : engine.history) u->history.push_back ({ h->id, juce::String (h->how), h->out, h == engine.current });
    u->poolInfo = poolInfoText(); u->poolFiles = (int) pool.size(); u->poolUsable = poolUsable();
    u->preset = presetName; u->presetEdited = presetName.isNotEmpty() && (presetDirty.load() || ! engine.current || engine.current->id != presetEntry);
    std::atomic_store (&uiShared, std::shared_ptr<const UiState> (u));
    sendChangeMessage();
}

/* ================= saving with the project ================= */
namespace
{
struct TOut
{
    juce::String s;
    void d (double v) { char b[40]; std::snprintf (b, sizeof (b), "%.17g ", v); s << b; }
    void i (long long v) { s << juce::String (v) << " "; }
    void c (const Circuit& x) { d (x.a); d (x.sp); for (double v : x.s) d (v); for (double v : x.m) d (v); for (double v : x.d) d (v); }
};
struct TIn
{
    juce::StringArray t; int p = 0;
    explicit TIn (const juce::String& s) { t.addTokens (s, " \n\t", ""); t.removeEmptyStrings(); }
    bool ok() const { return p <= t.size(); }
    double d() { return p < t.size() ? t[p++].getDoubleValue() : (p++, 0.0); }
    long long i() { return p < t.size() ? t[p++].getLargeIntValue() : (p++, 0); }
    void c (Circuit& x) { x.a = d(); x.sp = d(); for (double& v : x.s) v = d(); for (double& v : x.m) v = d(); for (double& v : x.d) v = d(); }
};
void writeGenome (TOut& o, const Genome& g)
{
    o.i (g.seed); for (double h : g.hidden) o.d (h);
    for (auto& gl : g.glide) { o.d (gl.val); for (double v : gl.vals) o.d (v); o.i ((long long) gl.routes.size()); for (auto& r : gl.routes) { o.i (r.dest); o.d (r.depth); o.i (r.comp); } }
    for (auto& c : g.chaos.h) o.c (c); for (auto& c : g.chaos.sl) o.c (c);
    for (auto& r : g.chaos.route) { o.i (r.a); o.i (r.b); o.d (r.w); o.d (r.sa); o.d (r.sb); } o.d (g.chaos.k);
    o.i ((long long) g.layers.size());
    for (auto& L : g.layers) { o.i (L.key); o.i ((int) L.role); o.i (L.seed); for (double v : { L.pos, L.spread, L.grain, L.dens }) o.d (v); o.i (L.count); for (double v : { L.pitch, L.pjit, L.rev, L.pan, L.width, L.gain, L.start, L.decay, L.curve, L.bright, L.reso, L.f0 }) o.d (v); }
    o.i ((long long) g.order.size()); for (int k : g.order) o.i (k);
    o.i ((long long) g.links.size()); for (auto& l : g.links) { o.i (l.from); o.i (l.to); o.i ((int) l.type); o.i (l.dest); o.d (l.amount); }
    o.d (g.resJit); for (double r : g.rot) o.d (r);
}
bool readGenome (TIn& in, Genome& g)
{
    g.seed = (uint32_t) in.i(); for (double& h : g.hidden) h = in.d();
    for (auto& gl : g.glide) { gl.val = in.d(); for (double& v : gl.vals) v = in.d(); const int n = (int) in.i(); if (n < 0 || n > 8) return false; gl.routes.resize ((size_t) n); for (auto& r : gl.routes) { r.dest = (int) in.i(); r.depth = in.d(); r.comp = (int) in.i(); } }
    for (auto& c : g.chaos.h) in.c (c); for (auto& c : g.chaos.sl) in.c (c);
    for (auto& r : g.chaos.route) { r.a = (int) in.i(); r.b = (int) in.i(); r.w = in.d(); r.sa = in.d(); r.sb = in.d(); } g.chaos.k = in.d();
    const int nl = (int) in.i(); if (nl < 0 || nl > 7) return false; g.layers.resize ((size_t) nl);
    for (auto& L : g.layers)
    {
        L.key = (int) in.i(); L.kind = kindOf (L.key); L.role = (Role) in.i(); L.seed = (uint32_t) in.i(); L.pos = in.d(); L.spread = in.d(); L.grain = in.d(); L.dens = in.d(); L.count = (int) in.i();
        L.pitch = in.d(); L.pjit = in.d(); L.rev = in.d(); L.pan = in.d(); L.width = in.d(); L.gain = in.d(); L.start = in.d(); L.decay = in.d(); L.curve = in.d(); L.bright = in.d(); L.reso = in.d(); L.f0 = in.d();
    }
    const int no = (int) in.i(); if (no < 0 || no > 7) return false; g.order.resize ((size_t) no); for (int& k : g.order) k = (int) in.i();
    const int nk = (int) in.i(); if (nk < 0 || nk > 64) return false; g.links.resize ((size_t) nk);
    for (auto& l : g.links) { l.from = (int) in.i(); l.to = (int) in.i(); l.type = (LinkType) in.i(); l.dest = (int) in.i(); l.amount = in.d(); }
    g.resJit = in.d(); for (double& r : g.rot) r = in.d();
    return in.ok();
}
}

void HalfLifeProcessor::rebuildCoreXml()
{
    auto x = std::make_unique<juce::XmlElement> ("Core");
    x->setAttribute ("v", 1); x->setAttribute ("hid", engine.hid); x->setAttribute ("oct", octave.load());
    if (presetName.isNotEmpty()) { x->setAttribute ("preset", presetName); x->setAttribute ("presetEdited", presetDirty.load() || ! engine.current || engine.current->id != presetEntry); }
    { TOut o; for (auto& c : engine.HNET) o.c (c); x->createNewChildElement ("Net")->addTextElement (o.s); }
    { TOut o; for (auto& r : engine.HROUTE) { o.i (r.a); o.i (r.b); o.d (r.w); o.d (r.sa); o.d (r.sb); } x->createNewChildElement ("Route")->addTextElement (o.s); }
    for (int i = 0; i < 4; ++i)
    {
        const auto& sl = engine.slots[(size_t) i]; auto* e = x->createNewChildElement ("Slot");
        e->setAttribute ("i", i); e->setAttribute ("pinned", sl.pinned);
        { TOut o; o.c (sl.glide.c); o.i ((long long) sl.glide.routes.size()); for (auto& r : sl.glide.routes) { o.i (r.dest); o.d (r.depth); o.i (r.comp); } e->createNewChildElement ("Glide")->addTextElement (o.s); }
        if (! sl.has()) continue;
        e->setAttribute ("name", juce::String (sl.name)); e->setAttribute ("path", juce::String (sl.path)); e->setAttribute ("example", sl.example);
        e->setAttribute ("fromPool", sl.fromPool); e->setAttribute ("ra", sl.regionA); e->setAttribute ("rb", sl.regionB); e->setAttribute ("poolFile", sl.poolFile);
        if (sl.example) continue;
        // the slot's audio goes into the project too (FLAC), so the sound survives a moved or deleted file
        const auto key = juce::String (sl.path) + "|" + juce::String ((int) sl.data.size()) + "|" + juce::String (engine.rate());
        if (slotFlac[(size_t) i].getSize() == 0 || slotFlacKey[(size_t) i] != key)
        {
            float pk = 1e-9f; for (float v : sl.data) pk = std::max (pk, std::abs (v));
            const float k = 0.98f / pk; std::vector<float> tmp (sl.data.size()); for (size_t j = 0; j < tmp.size(); ++j) tmp[j] = sl.data[j] * k;
            juce::MemoryBlock mb; bool ok = false;
            {
                juce::FlacAudioFormat fl;
               #if JUCE_MAJOR_VERSION >= 8
                std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (mb, false);
                auto w = fl.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (engine.rate()).withNumChannels (1).withBitsPerSample (24).withQualityOptionIndex (5));
                if (w) { const float* ch[1] = { tmp.data() }; ok = w->writeFromFloatArrays (ch, 1, (int) tmp.size()); }
               #else
                auto* os = new juce::MemoryOutputStream (mb, false);
                std::unique_ptr<juce::AudioFormatWriter> w (fl.createWriterFor (os, engine.rate(), 1, 24, {}, 5));
                if (w) { const float* ch[1] = { tmp.data() }; ok = w->writeFromFloatArrays (ch, 1, (int) tmp.size()); } else delete os;
               #endif
            }
            if (! ok) { mb.reset(); mb.append (sl.data.data(), sl.data.size() * sizeof (float)); slotFlacGain[(size_t) i] = 0; }   // raw floats fallback
            else slotFlacGain[(size_t) i] = 1.0 / k;
            slotFlac[(size_t) i] = mb; slotFlacKey[(size_t) i] = key; slotFlacRate[(size_t) i] = engine.rate();
        }
        auto* a = e->createNewChildElement ("Audio");
        a->setAttribute ("rate", slotFlacRate[(size_t) i]); a->setAttribute ("gain", slotFlacGain[(size_t) i]); a->setAttribute ("fmt", slotFlacGain[(size_t) i] > 0 ? "flac" : "f32");
        a->addTextElement (slotFlac[(size_t) i].toBase64Encoding());
    }
    { juce::String s; for (int k : engine.locks) s << k << " "; x->createNewChildElement ("Locks")->addTextElement (s); }
    { auto* pe = x->createNewChildElement ("Pool"); for (auto& f : pool) { auto* fe = pe->createNewChildElement ("F"); fe->setAttribute ("p", f.file.getFullPathName()); fe->setAttribute ("rel", f.path); fe->setAttribute ("st", f.st); } }
    {
        auto* he = x->createNewChildElement ("Hist"); he->setAttribute ("cur", engine.current ? engine.current->id : 0);
        for (auto& h : engine.history) { TOut o; writeGenome (o, h->genome); auto* ee = he->createNewChildElement ("E"); ee->setAttribute ("id", h->id); ee->setAttribute ("how", juce::String (h->how)); ee->addTextElement (o.s); }
    }
    const std::lock_guard<std::mutex> l (stateLock);
    coreEl = std::move (x);
}

void HalfLifeProcessor::restoreCore (const juce::XmlElement& x, bool asPreset)
{
    // a preset brings its sound, slots, chaos and locks; it leaves the pool, the history, the entry ids and the octave alone
    if (! asPreset) { engine.hid = x.getIntAttribute ("hid", engine.hid); octave = x.getIntAttribute ("oct", 0); }
    if (auto* n = x.getChildByName ("Net")) { TIn in (n->getAllSubText()); for (auto& c : engine.HNET) in.c (c); }
    if (auto* n = x.getChildByName ("Route")) { TIn in (n->getAllSubText()); for (auto& r : engine.HROUTE) { r.a = (int) in.i(); r.b = (int) in.i(); r.w = in.d(); r.sa = in.d(); r.sb = in.d(); } }
    for (auto* e : x.getChildWithTagNameIterator ("Slot"))
    {
        const int i = e->getIntAttribute ("i", -1); if (i < 0 || i > 3) continue;
        auto& sl = engine.slots[(size_t) i];
        if (auto* gl = e->getChildByName ("Glide")) { TIn in (gl->getAllSubText()); in.c (sl.glide.c); const int n = (int) in.i(); if (n >= 0 && n <= 8) { sl.glide.routes.resize ((size_t) n); for (auto& r : sl.glide.routes) { r.dest = (int) in.i(); r.depth = in.d(); r.comp = (int) in.i(); } } readGlide (sl.glide); }
        sl.clear(); sl.pinned = e->getBoolAttribute ("pinned");
        if (! e->hasAttribute ("name")) continue;
        const auto name = e->getStringAttribute ("name"), path = e->getStringAttribute ("path");
        const double ra = e->getDoubleAttribute ("ra", -1), rb = e->getDoubleAttribute ("rb", -1);
        if (e->getBoolAttribute ("example")) { engine.loadBuiltin (i, name.toStdString(), true); continue; }
        std::vector<float> d;
        if (auto* a = e->getChildByName ("Audio"))
        {
            juce::MemoryBlock mb; mb.fromBase64Encoding (a->getAllSubText());
            const double rate = a->getDoubleAttribute ("rate", engine.rate()), gain = a->getDoubleAttribute ("gain", 1);
            if (a->getStringAttribute ("fmt") == "f32") { d.resize (mb.getSize() / sizeof (float)); std::memcpy (d.data(), mb.getData(), d.size() * sizeof (float)); }
            else
            {
                juce::FlacAudioFormat fl;
                std::unique_ptr<juce::AudioFormatReader> rd (fl.createReaderFor (new juce::MemoryInputStream (mb, false), true));
                if (rd) { juce::AudioBuffer<float> b (1, (int) rd->lengthInSamples); rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, false); d.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples()); for (auto& v : d) v = (float) (v * gain); }
            }
            if (! d.empty() && std::abs (rate - engine.rate()) > 0.5) d = resample (d, engine.rate() / rate);
        }
        bool embedded = ! d.empty();   // embedded audio was saved after the DC filter: don't filter it twice
        if (d.empty() && path.isNotEmpty() && juce::File (path).existsAsFile()) decodeFile (juce::File (path), engine.rate(), d, kMaxSec, ra, rb);
        if (d.empty()) continue;
        engine.setSlotData (i, std::move (d), name.toStdString(), false, ! embedded);
        sl.path = path.toStdString(); sl.fromPool = e->getBoolAttribute ("fromPool"); sl.regionA = ra; sl.regionB = rb; sl.poolFile = e->getIntAttribute ("poolFile", -1);
    }
    engine.locks.clear();
    if (auto* n = x.getChildByName ("Locks")) { TIn in (n->getAllSubText()); while (in.p < in.t.size()) engine.locks.insert ((int) in.i()); }
    if (asPreset)
    {
        std::shared_ptr<Entry> e;
        if (auto* he = x.getChildByName ("Hist"))
            for (auto* ee : he->getChildWithTagNameIterator ("E"))
            {
                auto n = std::make_shared<Entry>(); TIn in (ee->getAllSubText());
                if (! readGenome (in, n->genome)) continue;
                e = n; if (ee->getIntAttribute ("id") == he->getIntAttribute ("cur")) break;
            }
        if (e)
        {
            e->id = ++engine.hid; e->how = "preset";
            Genome g = e->genome; e->out = engine.render (g);
            if (e->out)
            {
                engine.current = e; engine.history.insert (engine.history.begin(), e);
                if (engine.history.size() > 16) engine.history.pop_back();
                publishSound(); audition();
                return;
            }
        }
        if (engine.rerender()) publishSound();   // no sound in the file: keep the current one, rendered with the preset's settings
        return;
    }
    presetName = x.getStringAttribute ("preset");
    pool.clear(); poolCache.clear();
    if (auto* pe = x.getChildByName ("Pool"))
        for (auto* f : pe->getChildWithTagNameIterator ("F")) { juce::File ff (f->getStringAttribute ("p")); if (ff.existsAsFile()) pool.push_back ({ ff, f->getStringAttribute ("rel"), ff.getFileNameWithoutExtension(), f->getIntAttribute ("st") == 3 ? 3 : 0 }); }
    engine.history.clear(); engine.current = nullptr;
    std::vector<int> pendingIds;
    if (auto* he = x.getChildByName ("Hist"))
    {
        const int cur = he->getIntAttribute ("cur");
        for (auto* ee : he->getChildWithTagNameIterator ("E"))
        {
            auto e = std::make_shared<Entry>(); e->id = ee->getIntAttribute ("id"); e->how = ee->getStringAttribute ("how", "fresh").toStdString();
            TIn in (ee->getAllSubText()); if (! readGenome (in, e->genome)) continue;
            engine.history.push_back (e);
            if (e->id == cur) engine.current = e;
        }
        if (! engine.current && ! engine.history.empty()) engine.current = engine.history.front();
        // the current sound first; the rest of the history renders afterwards, one at a time, for its thumbnails
        if (engine.current) { Genome g = engine.current->genome; engine.current->out = engine.render (g); publishSound(); }
        for (auto& e : engine.history) if (e != engine.current) pendingIds.push_back (e->id);
    }
    if (! engine.current) engine.boot(), publishSound();
    presetEntry = engine.current && ! x.getBoolAttribute ("presetEdited") ? engine.current->id : -1; presetDirty = false;
    for (int id : pendingIds)
        post ([this, id] { for (auto& e : engine.history) if (e->id == id && ! e->out) { Genome g = e->genome; e->out = engine.render (g); } }, false);
}

void HalfLifeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();
    if (! xml) return;
    { const std::lock_guard<std::mutex> l (stateLock); if (coreEl) xml->addChildElement (new juce::XmlElement (*coreEl)); }
    if (auto* c = xml->getChildByName ("Core")) if (c->hasAttribute ("preset") && presetDirty.load()) c->setAttribute ("presetEdited", true);   // a knob moved after the last render
    copyXmlToBinary (*xml, dest);
}

void HalfLifeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return;
    std::shared_ptr<juce::XmlElement> core;
    if (auto* c = xml->getChildByName ("Core")) { core = std::make_shared<juce::XmlElement> (*c); xml->removeChildElement (c, true); }
    suppressRerender = true;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    suppressRerender = false; rerenderAt = 0;
    if (core)
    {
        { const std::lock_guard<std::mutex> l (stateLock); coreEl = std::make_unique<juce::XmlElement> (*core); }
        post ([this, core] { restoreCore (*core); }, false);
    }
}

/* ================= presets =================
   A preset is one .hlpreset file: a zip holding preset.xml (the knobs and the sound's genome, slots, chaos, locks)
   and the slots' audio as FLAC files, so it plays the same on a computer that doesn't have the samples.
   Not in it: the pool, the history, the master volume, file paths (they would show the sender's folders). */
namespace
{
constexpr juce::int64 kMaxPresetFile = 1536ll << 20, kMaxPresetXml = 64ll << 20, kMaxPresetAudio = 400ll << 20;
const char* const kPresetExt = ".hlpreset";

bool isFactoryPreset (int i) { return juce::String (BinaryData::originalFilenames[i]).endsWithIgnoreCase (kPresetExt); }
}

std::vector<HalfLifeProcessor::PresetRef> HalfLifeProcessor::listPresets() const
{
    std::vector<PresetRef> out, user;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (isFactoryPreset (i)) out.push_back ({ juce::String (BinaryData::originalFilenames[i]).upToLastOccurrenceOf (".", false, false), {}, i });
    for (const auto& e : juce::RangedDirectoryIterator (presetFolder(), false, juce::String ("*") + kPresetExt, juce::File::findFiles))
        user.push_back ({ e.getFile().getFileNameWithoutExtension(), e.getFile(), -1 });
    auto byName = [] (const PresetRef& a, const PresetRef& b) { return a.name.compareNatural (b.name) < 0; };
    std::sort (out.begin(), out.end(), byName); std::sort (user.begin(), user.end(), byName);
    out.insert (out.end(), user.begin(), user.end());
    return out;
}

void HalfLifeProcessor::savePreset (const juce::String& rawName)
{
    auto name = juce::File::createLegalFileName (rawName.trim()).trim();
    if (name.isEmpty()) name = "Untitled";
    post ([this, name]
    {
        rebuildCoreXml();   // the state as it is right now, audio included
        const auto f = presetFolder().getChildFile (name + kPresetExt);
        const bool existed = f.existsAsFile();
        juce::String err;
        if (writePreset (f, name, err))
        {
            presetName = name; presetEntry = engine.current ? engine.current->id : -1; presetDirty = false;
            setStatus ((existed ? "Preset replaced" : "Preset saved") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + name);
        }
        else setStatus ("Couldn't save the preset: " + err);
    });
}

bool HalfLifeProcessor::writePreset (const juce::File& f, const juce::String& name, juce::String& err)
{
    std::unique_ptr<juce::XmlElement> core;
    { const std::lock_guard<std::mutex> l (stateLock); if (coreEl) core = std::make_unique<juce::XmlElement> (*coreEl); }
    auto params = apvts.copyState().createXml();
    if (! core || ! params || ! engine.current) { err = "there's no sound yet"; return false; }

    core->deleteAllChildElementsWithTagName ("Pool");
    core->removeAttribute ("oct"); core->removeAttribute ("preset"); core->removeAttribute ("presetEdited");
    if (auto* he = core->getChildByName ("Hist"))   // only the current sound
    {
        const int cur = he->getIntAttribute ("cur");
        juce::Array<juce::XmlElement*> drop;
        for (auto* e : he->getChildWithTagNameIterator ("E")) if (e->getIntAttribute ("id") != cur) drop.add (e);
        for (auto* e : drop) he->removeChildElement (e, true);
    }
    for (auto* p : params->getChildWithTagNameIterator ("PARAM"))
        if (p->getStringAttribute ("id") == "volume") { params->removeChildElement (p, true); break; }

    // the slots' audio: out of the XML into files of their own, each distinct sound stored once
    juce::ZipFile::Builder zip;
    std::map<juce::String, juce::String> stored;   // audio text -> file name
    int n = 0;
    for (auto* sl : core->getChildWithTagNameIterator ("Slot"))
    {
        sl->setAttribute ("path", ""); sl->setAttribute ("fromPool", false); sl->setAttribute ("poolFile", -1);
        auto* a = sl->getChildByName ("Audio"); if (! a) continue;
        const auto text = a->getAllSubText(); a->deleteAllTextElements();
        auto it = stored.find (text);
        if (it == stored.end())
        {
            juce::MemoryBlock mb; mb.fromBase64Encoding (text);
            const auto file = "audio/" + juce::String (++n) + (a->getStringAttribute ("fmt") == "f32" ? ".f32" : ".flac");
            zip.addEntry (new juce::MemoryInputStream (mb, true), 0, file, juce::Time::getCurrentTime());
            it = stored.emplace (text, file).first;
        }
        a->setAttribute ("file", it->second);
    }
    juce::XmlElement root ("HalfLifePreset");
    root.setAttribute ("v", 1); root.setAttribute ("name", name); root.setAttribute ("made", getName());
    root.addChildElement (params.release()); root.addChildElement (core.release());
    const auto xml = root.toString();
    zip.addEntry (new juce::MemoryInputStream (xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true), 9, "preset.xml", juce::Time::getCurrentTime());

    // written next to the target first, so a crash or a full disk never leaves half a preset behind
    const auto tmp = f.getSiblingFile (f.getFileName() + ".part");
    {
        juce::FileOutputStream os (tmp);
        if (! os.openedOk()) { err = "can't write to " + f.getParentDirectory().getFullPathName(); return false; }
        os.setPosition (0); os.truncate();
        if (! zip.writeToStream (os, nullptr)) { os.flush(); tmp.deleteFile(); err = "writing failed (disk full?)"; return false; }
        os.flush();
        if (os.getStatus().failed()) { tmp.deleteFile(); err = os.getStatus().getErrorMessage(); return false; }
    }
    if (! tmp.moveFileTo (f)) { tmp.deleteFile(); err = "can't replace " + f.getFileName(); return false; }
    return true;
}

juce::String HalfLifeProcessor::loadPreset (const PresetRef& p)
{
    std::unique_ptr<juce::InputStream> in;
    if (p.factory >= 0 && p.factory < BinaryData::namedResourceListSize)
    {
        int size = 0; const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[p.factory], size);
        if (! data) return "the preset is missing";
        in = std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false);
    }
    else
    {
        if (! p.file.existsAsFile()) return "the file is gone";
        if (p.file.getSize() > kMaxPresetFile) return "the file is too big to be a preset";
        in = p.file.createInputStream();
        if (! in) return "can't open the file";
    }
    juce::ZipFile zip (in.release(), true);
    auto read = [&zip] (const juce::String& entry, juce::int64 cap, juce::MemoryBlock& mb)
    {
        const auto* e = zip.getEntry (entry);
        if (! e || e->uncompressedSize > cap) return false;
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (*e));
        if (! s) return false;
        mb.reset(); s->readIntoMemoryBlock (mb, (juce::pointer_sized_int) cap);
        return (juce::int64) mb.getSize() == e->uncompressedSize;
    };
    juce::MemoryBlock xmlData;
    if (! read ("preset.xml", kMaxPresetXml, xmlData)) return "this isn't a Half Life preset (or it's damaged)";
    auto root = juce::parseXML (xmlData.toString());
    if (! root || ! root->hasTagName ("HalfLifePreset")) return "this isn't a Half Life preset (or it's damaged)";
    if (root->getIntAttribute ("v", 1) > 1) return "this preset needs a newer Half Life";
    auto* params = root->getChildByName (apvts.state.getType());
    auto* coreX = root->getChildByName ("Core");
    if (! params || ! coreX) return "the preset is damaged";

    auto core = std::make_shared<juce::XmlElement> (*coreX);
    juce::int64 total = 0;
    for (auto* sl : core->getChildWithTagNameIterator ("Slot"))
        if (auto* a = sl->getChildByName ("Audio"))
        {
            const auto file = a->getStringAttribute ("file"); if (file.isEmpty()) continue;
            juce::MemoryBlock mb;
            if (! read (file, kMaxPresetAudio, mb) || (total += (juce::int64) mb.getSize()) > kMaxPresetFile) return "the preset's audio is damaged";
            a->removeAttribute ("file"); a->deleteAllTextElements(); a->addTextElement (mb.toBase64Encoding());
        }

    // the knobs; the master volume stays where the user has it
    auto now = apvts.copyState().createXml();
    juce::XmlElement px (*params);
    for (auto* q : px.getChildWithTagNameIterator ("PARAM")) if (q->getStringAttribute ("id") == "volume") { px.removeChildElement (q, true); break; }
    if (now) for (auto* q : now->getChildWithTagNameIterator ("PARAM")) if (q->getStringAttribute ("id") == "volume") { px.addChildElement (new juce::XmlElement (*q)); break; }
    suppressRerender = true;
    apvts.replaceState (juce::ValueTree::fromXml (px));
    suppressRerender = false; rerenderAt = 0;

    const auto name = p.name;   // the file name is the preset's name, so renaming the file renames it
    post ([this, core, name]
    {
        restoreCore (*core, true);
        presetName = name; presetEntry = engine.current ? engine.current->id : -1; presetDirty = false;
        setStatus ("Preset" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + name);
    });
    return {};
}

/* ================= audio ================= */
void HalfLifeProcessor::prepareToPlay (double sr, int)
{
    hostRate = sr;
    for (auto& v : voices) { if (v.on && v.buf) v.buf->users--; v = Voice(); }
    post ([this, sr]
    {
        if (std::abs (engine.rate() - sr) < 0.5) return;
        const double old = engine.rate(); engine.setRate (sr);
        for (int i = 0; i < 4; ++i)
        {
            auto& sl = engine.slots[(size_t) i]; if (! sl.has()) continue;
            if (sl.example) { engine.loadBuiltin (i, sl.name, true); continue; }
            const auto keepPath = sl.path; const bool fp = sl.fromPool; const double ra = sl.regionA, rb = sl.regionB; const int pf = sl.poolFile; const auto name = sl.name;
            std::vector<float> d; const juce::File f (keepPath);
            if (keepPath.empty() || ! f.existsAsFile() || ! decodeFile (f, sr, d, kMaxSec, ra, rb)) d = resample (sl.data, sr / old);
            engine.setSlotData (i, std::move (d), name, false);
            sl.path = keepPath; sl.fromPool = fp; sl.regionA = ra; sl.regionB = rb; sl.poolFile = pf;
        }
        poolCache.clear();
        if (engine.rerender()) publishSound();
        rebuildCoreXml();
    }, false);
}

void HalfLifeProcessor::startVoice (int note, int src, bool oneShot)
{
    PlayBuf* pb = playing.load (std::memory_order_acquire);
    if (! pb || ! pb->out || pb->out->end < 8) return;
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.on) { v = &x; break; }
    if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; if (v->buf) v->buf->users--; }
    *v = Voice();
    v->on = true; v->buf = pb; pb->users++; v->note = note; v->src = src; v->age = ++voiceClock;
    const double semisR = std::pow (2.0, (note - 60) / 12.0);
    v->rate = semisR * pb->out->sampleRate / hostRate;
    v->endPos = pb->out->end - 3;
    v->hWant = juce::jmin (PlayBuf::NL - 1, levelFor (v->rate));
    if (v->hWant > 0 && ! pb->ready[(size_t) v->hWant].load (std::memory_order_acquire)) pb->wanted[(size_t) v->hWant] = true;
    v->h = 0; for (int h = v->hWant; h > 0; --h) if (pb->ready[(size_t) h].load (std::memory_order_acquire)) { v->h = h; break; }
    const auto& a = apvts;
    const double A = std::max (0.0005, a.getRawParameterValue ("attack")->load() / 1000.0), D = std::max (0.002, a.getRawParameterValue ("decay")->load() / 1000.0);
    v->S = a.getRawParameterValue ("sustain")->load();
    v->stage = 0; v->env = 0; v->aInc = 1.0 / (A * hostRate); v->dK = std::exp (-1.0 / (D / 4 * hostRate)); v->dE = 1;
    if (oneShot)
    {
        const double R = std::max (0.005, a.getRawParameterValue ("release")->load() / 1000.0);
        const double dur = v->endPos / v->rate / hostRate;
        v->autoRel = std::max (A + 0.001, dur - R) * hostRate;
    }
    lastNote = note;
}

void HalfLifeProcessor::releaseVoice (Voice& v)
{
    if (! v.on || v.released) return;
    const double R = std::max (0.005, apvts.getRawParameterValue ("release")->load() / 1000.0);
    v.released = true; v.stage = 3; v.rK = std::exp (-1.0 / (R / 4 * hostRate)); v.rLeft = R * 2.6 * hostRate;
}

void HalfLifeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int N = buffer.getNumSamples();
    buffer.clear();
    float* outL = buffer.getWritePointer (0); float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // the window's requests
    if (const int a = auditionReq.load(); a != auditionSeen) { auditionSeen = a; startVoice (60 + 12 * octave.load(), 2, true); }
    if (const int t = triggerReq.load(); t != triggerSeen) { triggerSeen = t; startVoice (60 + 12 * octave.load(), 2, true); }
    {
        const auto r = keyFifo.read (keyFifo.getNumReady());
        auto run = [&] (int s, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const int ev = keyEvents[(size_t) (s + i)];
                if (ev > 0) startVoice (ev - 1, 1, false);
                else for (auto& v : voices) if (v.on && v.src == 1 && v.note == -ev - 1) releaseVoice (v);
            }
        };
        run (r.startIndex1, r.blockSize1); run (r.startIndex2, r.blockSize2);
    }

    PlayBuf* cur = playing.load (std::memory_order_acquire);
    const float vol = apvts.getRawParameterValue ("volume")->load();
    const float target = vol <= -60 ? 0.0f : 0.8f * std::pow (10.0f, vol / 20.0f);
    const float gk = 1.0f - std::exp (-1.0f / (0.02f * (float) hostRate));
    auto it = midi.cbegin();
    for (int i = 0; i < N; ++i)
    {
        while (it != midi.cend() && (*it).samplePosition <= i)
        {
            const auto m = (*it).getMessage(); ++it;
            if (m.isNoteOn()) startVoice (m.getNoteNumber(), 0, false);
            else if (m.isNoteOff()) { for (auto& v : voices) if (v.on && v.src == 0 && v.note == m.getNoteNumber()) releaseVoice (v); }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& v : voices) releaseVoice (v); }
        }
        float l = 0, r = 0;
        for (auto& v : voices)
        {
            if (! v.on) continue;
            // a better band-limited copy became ready: switch to it
            if (v.h < v.hWant && v.buf->ready[(size_t) v.hWant].load (std::memory_order_acquire)) v.h = v.hWant;
            const auto& o = *v.buf->out;
            double sl, sr;
            if (v.h == 0) { sl = sread (o.L.data(), o.end, v.pos); sr = sread (o.R.data(), o.end, v.pos); }
            else { sl = lvRead (*v.buf->lvL[(size_t) v.h], v.pos); sr = lvRead (*v.buf->lvR[(size_t) v.h], v.pos); }
            // envelope
            if (v.stage == 0) { v.env += v.aInc; if (v.env >= 1) { v.env = 1; v.stage = 1; } }
            else if (v.stage == 1) { v.dE *= v.dK; v.env = v.S + (1 - v.S) * v.dE; }
            else if (v.stage == 3) { v.env *= v.rK; if (--v.rLeft <= 0) { v.on = false; v.buf->users--; continue; } }
            l += (float) (sl * v.env); r += (float) (sr * v.env);
            v.pos += v.rate; v.t += 1;
            if (v.autoRel >= 0 && v.t >= v.autoRel && ! v.released) releaseVoice (v);
            if (v.pos >= v.endPos) { v.on = false; v.buf->users--; }
        }
        gainNow += (target - gainNow) * gk;
        auto safe = [] (float x) { const float a = std::abs (x); if (a < 0.9f) return x; const float y = 0.9f + 0.1f * std::tanh ((std::min (a, 2.0f) - 0.9f) / 0.1f); return x < 0 ? -y : y; };
        outL[i] = safe (l * gainNow); if (outR) outR[i] = safe (r * gainNow);
    }
    // playhead of the newest voice on the current sound
    float ph = -1; uint64_t best = 0;
    for (auto& v : voices) if (v.on && v.buf == cur && v.age > best) { best = v.age; ph = (float) juce::jlimit (0.0, 1.0, v.pos / v.endPos); }
    playhead = ph;
}

#ifndef HL_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HalfLifeProcessor(); }
#endif

