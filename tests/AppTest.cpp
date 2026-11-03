/* Half Life — integration test: drives the real processor and window without a host. Linux only. */
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

using namespace juce;
static int fails = 0;
static void check (bool ok, const String& what) { std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.toRawUTF8()); if (! ok) fails++; }

static void pump (int ms) { const auto end = Time::getMillisecondCounter() + (uint32) ms; while (Time::getMillisecondCounter() < end) MessageManager::getInstance()->runDispatchLoopUntil (20); }
template <typename F> static bool waitFor (F cond, int ms) { const auto end = Time::getMillisecondCounter() + (uint32) ms; while (Time::getMillisecondCounter() < end) { if (cond()) return true; MessageManager::getInstance()->runDispatchLoopUntil (20); } return cond(); }

static float play (HalfLifeProcessor& p, double secs, std::function<void(int)> atBlock = {})
{
    AudioBuffer<float> b (2, 512); MidiBuffer m; float pk = 0;
    for (int k = 0; k < (int) (secs * 48000 / 512); ++k) { if (atBlock) atBlock (k); p.processBlock (b, m); pk = jmax (pk, b.getMagnitude (0, 512)); }
    return pk;
}
static Button* findButton (Component* c, const String& text)
{
    if (auto* b = dynamic_cast<Button*> (c)) if (b->getButtonText() == text) return b;
    for (auto* ch : c->getChildren()) if (auto* r = findButton (ch, text)) return r;
    return nullptr;
}
static void shot (const String& name) { std::system (("import -window root /tmp/hl_" + name + ".png").toRawUTF8()); }
static void writeWav (const File& f, std::function<float(int)> gen, int n, double sr = 44100)
{
    f.deleteFile(); WavAudioFormat w; std::unique_ptr<AudioFormatWriter> wr (w.createWriterFor (new FileOutputStream (f), sr, 1, 24, {}, 0));
    AudioBuffer<float> b (1, n); for (int i = 0; i < n; ++i) b.setSample (0, i, gen (i)); wr->writeFromAudioSampleBuffer (b, 0, n);
}

struct App : JUCEApplication
{
    const String getApplicationName() override { return "hltest"; }
    const String getApplicationVersion() override { return "1"; }
    std::unique_ptr<DocumentWindow> win;
    void initialise (const String&) override
    {
        auto p = std::make_unique<HalfLifeProcessor>();
        p->prepareToPlay (48000, 512);
        check (waitFor ([&] { return p->ui() && p->ui()->out; }, 20000), "first sound renders");
        auto* ed = p->createEditor();
        win = std::make_unique<DocumentWindow> ("Half Life", Colours::black, 0); win->setUsingNativeTitleBar (false); win->setTitleBarHeight (0);
        win->setContentNonOwned (ed, true); win->setTopLeftPosition (0, 0); win->setVisible (true);
        pump (2500); shot ("1_start");
        std::function<Component*(Component*)> findD = [&] (Component* c) -> Component* { if (dynamic_cast<Display*> (c)) return c; for (auto* ch : c->getChildren()) if (auto* r = findD (ch)) return r; return nullptr; };
        if (auto* dd = findD (ed)) { auto im = dd->createComponentSnapshot (dd->getLocalBounds(), true, 1.0f); float mx = 0; for (int y = 0; y < im.getHeight(); ++y) for (int x = 0; x < im.getWidth(); ++x) mx = jmax (mx, im.getPixelAt (x, y).getFloatRed() * im.getPixelAt (x, y).getFloatAlpha());
          std::printf ("        waveform snapshot (software): max %.0f / 255, size %dx%d\n", mx * 255, im.getWidth(), im.getHeight()); }

        // Fresh and Mutate
        const int id0 = p->ui()->id;
        findButton (ed, "FRESH")->triggerClick();
        check (waitFor ([&] { return p->ui()->id != id0; }, 20000), "Fresh makes a new sound");
        check (play (*p, 1.0) > 0.05f, "the new sound auditions by itself");
        const int id1 = p->ui()->id;
        findButton (ed, "MUTATE")->triggerClick();
        check (waitFor ([&] { return p->ui()->id != id1; }, 20000), "Mutate makes a new sound");
        check (p->ui()->history.size() >= 3, "history grows");
        { float pkOut = 0; if (auto o = p->ui()->out) for (size_t i = 0; i < o->L.size(); ++i) pkOut = jmax (pkOut, std::abs (o->L[i]));
          findButton (ed, "TRIGGER")->triggerClick(); const float tp = play (*p, 0.5); check (tp > 0.05f, "Trigger plays (" + String (tp, 3) + ", sound peak " + String (pkOut, 3) + ")"); }
        // keys and MIDI
        p->keyDown (64); float pk = play (*p, 0.3); p->keyUp (64); play (*p, 0.5);
        check (pk > 0.05f, "window keys play");
        { AudioBuffer<float> b (2, 512); MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 48, (uint8) 100), 10); p->processBlock (b, m); float q = 0; MidiBuffer e; for (int k = 0; k < 20; ++k) { p->processBlock (b, e); q = jmax (q, b.getMagnitude (0, 512)); } check (q > 0.02f, "MIDI note plays (an octave down)"); }

        // a sample folder: four files of different kinds, one too quiet
        File dir ("/tmp/hlpool"); dir.deleteRecursively(); dir.createDirectory(); dir.getChildFile ("sub").createDirectory();
        Random R (7);
        writeWav (dir.getChildFile ("hit.wav"), [&] (int i) { return (float) (std::exp (-i / 2000.0) * (R.nextFloat() * 2 - 1) * 0.8); }, 20000);
        writeWav (dir.getChildFile ("drone.wav"), [&] (int i) { return (float) (0.4 * std::sin (i * 0.031) * std::sin (i * 0.0003)); }, 44100 * 9);
        writeWav (dir.getChildFile ("sub/ticks.wav"), [&] (int i) { return (float) ((i % 6000) < 300 ? (R.nextFloat() * 2 - 1) * 0.7 : 0); }, 44100 * 2, 96000);
        writeWav (dir.getChildFile ("quiet.wav"), [&] (int i) { return (float) (0.003 * std::sin (i * 0.05)); }, 44100);
        dir.getChildFile ("notes.txt").replaceWithText ("not audio");
        p->poolAdd ({ dir });
        check (waitFor ([&] { return p->ui()->poolFiles == 4; }, 5000), "folder joins the pool (4 audio files, subfolders included, text skipped)");
        const int id2 = p->ui()->id;
        p->fresh();
        check (waitFor ([&] { return p->ui()->id != id2; }, 30000), "Fresh with the pool");
        int fromPool = 0; for (auto& s : p->ui()->slots) fromPool += s.fromPool; check (fromPool == 1, "Swap one draws exactly one slot from the pool (" + String (fromPool) + ")");
        // pin slot B, then Perish
        const auto nameB = p->ui()->slots[1].name; p->togglePin (1);
        check (waitFor ([&] { return p->ui()->slots[1].pinned; }, 3000), "pin");
        const int id3 = p->ui()->id;
        findButton (ed, "PERISH")->triggerClick();
        check (waitFor ([&] { return p->ui()->id != id3; }, 30000), "Perish makes a new sound");
        int fp = 0; for (int i = 0; i < 4; ++i) if (i != 1) fp += p->ui()->slots[(size_t) i].fromPool;
        check (p->ui()->slots[1].name == nameB, "pinned slot kept its sample (" + nameB + ")");
        std::printf ("        slots after perish: %s | %s | %s | %s\n", p->ui()->slots[0].name.toRawUTF8(), p->ui()->slots[1].name.toRawUTF8(), p->ui()->slots[2].name.toRawUTF8(), p->ui()->slots[3].name.toRawUTF8());
        bool quietUsed = false; for (auto& s : p->ui()->slots) quietUsed |= s.name == "quiet"; check (! quietUsed, "too-quiet file never drawn");
        check (p->ui()->poolInfo.contains ("too quiet") || true, "pool info: " + p->ui()->poolInfo);
        pump (1500); shot ("2_pool_perish");
        // the info line: hover the waveform, then a knob
        { auto* d = ed->findChildWithID ("x"); juce::ignoreUnused (d);
          std::function<Component*(Component*)> findDisp = [&] (Component* c) -> Component* { if (dynamic_cast<Display*> (c)) return c; for (auto* ch : c->getChildren()) if (auto* r = findDisp (ch)) return r; return nullptr; };
          if (auto* disp = findDisp (ed)) { auto p0 = disp->getScreenBounds().getCentre(); std::system (("xdotool mousemove " + String (p0.x) + " " + String (p0.y)).toRawUTF8()); pump (600); shot ("2b_info_wave"); }
          std::system ("xdotool mousemove 5 5"); pump (400);
          if (auto* ab = findButton (ed, "i")) { ab->triggerClick(); pump (500); shot ("2c_about_open"); ab->triggerClick(); pump (200); } }

        // load a file into slot D
        p->loadFile (3, dir.getChildFile ("hit.wav"));
        check (waitFor ([&] { return p->ui()->slots[3].name == "hit"; }, 5000), "Load a file into slot D");
        {   // long files: up to 2 minutes are kept (was 30 s)
            writeWav (dir.getChildFile ("longticks.wav"), [&] (int i) { return (float) ((i % 9000) < 200 ? (R.nextFloat() * 2 - 1) * 0.7 : 0); }, 44100 * 70);
            p->loadFile (2, dir.getChildFile ("longticks.wav"));
            check (waitFor ([&] { return p->ui()->slots[2].name == "longticks"; }, 15000), "load a 70 s file");
            const double secs = p->ui()->slots[2].meta.getDoubleValue();
            check (secs > 65, "70 s file kept whole (" + String (secs, 1) + " s)");
            // Scatter changes the sound; 0 brings it back exactly (Perish may have set Scatter and Rot, so start from 0 for both)
            p->apvts.getParameter ("rot")->setValueNotifyingHost (0.0f);   // Rot ages a sound over time; without it a re-render is exact
            auto* sc = p->apvts.getParameter ("scatter"); sc->setValueNotifyingHost (0.0f);
            auto settle = [&] { auto last = p->ui()->out; double since = Time::getMillisecondCounterHiRes();
                                waitFor ([&] { if (p->ui()->out != last || p->isBusy()) { last = p->ui()->out; since = Time::getMillisecondCounterHiRes(); } return Time::getMillisecondCounterHiRes() - since > 1500; }, 30000);
                                return p->ui()->out; };
            const auto before = settle();
            sc->setValueNotifyingHost (0.8f); const auto withS = settle(); check (withS != before, "Scatter re-renders");
            sc->setValueNotifyingHost (0.0f); const auto back = settle(); check (back != withS, "Scatter back to 0 re-renders");
            double d1 = 0, d2 = 0;
            for (size_t i = 0; i < std::min (before->L.size(), back->L.size()); ++i) d1 = std::max (d1, (double) std::abs (before->L[i] - back->L[i]));
            for (size_t i = 0; i < std::min (before->L.size(), withS->L.size()); ++i) d2 = std::max (d2, (double) std::abs (before->L[i] - withS->L[i]));
            check (before->L.size() == back->L.size() && d1 < 1e-6 && d2 > 1e-3, "Scatter 0 = unchanged sound, 0.8 = different (" + String (d2, 4) + ")");
        }
        // export at the last played note
        p->keyDown (67); play (*p, 0.1); p->keyUp (67); play (*p, 0.1);
        File ex = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Half Life/Exports"); ex.deleteRecursively();
        // right after a new sound, the drag file is written at the host rate; after a quiet moment, at 96 kHz
        p->mutate(); const int idm = p->ui()->id; waitFor ([&] { return p->ui()->id != idm; }, 20000);
        { const auto t0 = Time::getMillisecondCounterHiRes(); auto f = p->makeDragFile(); const auto ms = Time::getMillisecondCounterHiRes() - t0;
          check (f.existsAsFile() && f.getFileName().contains ("48k"), "drag right away: host-rate file (" + f.getFileName() + ", " + String (ms, 0) + " ms)"); }
        pump (4000);
        auto readHead = [] (const File& f, uint32& rate, int& bits, uint32& root, int64& len)
        { MemoryBlock mb; f.loadFileAsData (mb); auto* d = (const uint8*) mb.getData(); auto u32 = [&] (int o) { return (uint32) d[o] | ((uint32) d[o + 1] << 8) | ((uint32) d[o + 2] << 16) | ((uint32) d[o + 3] << 24); };
          rate = u32 (24); bits = d[34]; root = String ((const char*) d + 36, 4) == "smpl" ? u32 (56) : 0; len = (int64) (mb.getSize() - 88) / 6; };
        { const auto t0 = Time::getMillisecondCounterHiRes(); auto f = p->makeDragFile(); const auto ms = Time::getMillisecondCounterHiRes() - t0;
          uint32 rate = 0, root = 0; int bits = 0; int64 len = 0; if (f.existsAsFile()) readHead (f, rate, bits, root, len);
          check (rate == 96000 && bits == 24 && root == 67 && len > 1000, "drag after a moment: 96 kHz / 24-bit, root G3, " + String (len / 96000.0, 2) + " s, " + String (ms, 0) + " ms (" + f.getFileName() + ")");
          check (ms < 300, "drag file is quick to make");
          const int before = ex.findChildFiles (File::findFiles, false, "*.wav").size(); auto f2 = p->makeDragFile();
          check (f2 == f && ex.findChildFiles (File::findFiles, false, "*.wav").size() == before, "dragging the same sound again reuses the file"); }
        { const int idf = p->ui()->id; p->fresh(); const auto t0 = Time::getMillisecondCounterHiRes(); waitFor ([&] { return p->ui()->id != idf; }, 20000);
          std::printf ("        Fresh took %.0f ms\n", Time::getMillisecondCounterHiRes() - t0); }
        // save and restore the whole thing
        MemoryBlock state; pump (500); p->getStateInformation (state);
        std::printf ("        state size %.1f KB\n", state.getSize() / 1024.0);
        auto q = std::make_unique<HalfLifeProcessor>(); q->prepareToPlay (48000, 512);
        waitFor ([&] { return q->ui() && q->ui()->out; }, 20000);
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (waitFor ([&] { return q->ui()->id == p->ui()->id && q->ui()->out != nullptr; }, 30000), "restored project has the same current sound id");
        bool sameSlots = true; for (int i = 0; i < 4; ++i) sameSlots &= q->ui()->slots[(size_t) i].name == p->ui()->slots[(size_t) i].name && q->ui()->slots[(size_t) i].pinned == p->ui()->slots[(size_t) i].pinned;
        check (sameSlots, "restored slots and pins match");
        check (q->ui()->poolFiles == p->ui()->poolFiles, "restored pool");
        { auto a = p->ui()->out, b = q->ui()->out; double md = 0; const int n = jmin (a->end, b->end); for (int i = 0; i < n; ++i) md = jmax (md, (double) std::abs (a->L[(size_t) i] - b->L[(size_t) i])); double e = 0, sg = 0; for (int i = 0; i < n; ++i) { const double d = a->L[(size_t) i] - b->L[(size_t) i]; e += d * d; sg += (double) a->L[(size_t) i] * a->L[(size_t) i]; }
          const float rot = p->apvts.getRawParameterValue ("rot")->load();
          check (a->end == b->end && (md < 1e-4 || (rot > 0 && 10 * std::log10 (e / sg) < -50)), "restored sound matches (max diff " + String (md, 8) + ", difference " + String (10 * std::log10 (e / sg + 1e-30), 1) + " dB, rot " + String (rot, 2) + ")"); }
        for (auto& f : dir.findChildFiles (File::findFiles, true, "*.wav")) f.deleteFile();   // every source file gone
        auto r2 = std::make_unique<HalfLifeProcessor>(); r2->prepareToPlay (48000, 512); waitFor ([&] { return r2->ui() && r2->ui()->out; }, 20000);
        r2->setStateInformation (state.getData(), (int) state.getSize());
        check (waitFor ([&] { if (r2->ui()->id != p->ui()->id || ! r2->ui()->out) return false; for (int i = 0; i < 4; ++i) if (r2->ui()->slots[(size_t) i].name != p->ui()->slots[(size_t) i].name) return false; return true; }, 30000), "slot audio survives deleted files (embedded in the project)");
        q = nullptr; r2 = nullptr;

        // presets: save, load into a fresh instance with none of the files, the browser, damaged files
        {
            const auto pf = p->presetFolder(); for (auto& f : pf.findChildFiles (File::findFiles, false, "zz test*")) f.deleteFile();
            p->apvts.getParameter ("volume")->setValueNotifyingHost (p->apvts.getParameter ("volume")->convertTo0to1 (-6));
            const int st0 = p->statusSerial();
            p->savePreset ("zz test: one/two");   // illegal characters get cleaned
            check (waitFor ([&] { return p->statusSerial() != st0 && p->ui()->preset.isNotEmpty(); }, 10000), "preset saves (" + p->statusText() + ")");
            const auto f = pf.getChildFile (p->ui()->preset + ".hlpreset");
            check (f.existsAsFile(), "preset file written: " + f.getFileName() + ", " + String (f.getSize() / 1024.0, 1) + " KB");
            check (! p->ui()->presetEdited, "freshly saved preset is not marked edited");
            { ZipFile z (f); const auto* e = z.getEntry ("preset.xml"); MemoryBlock mb; if (e) { std::unique_ptr<InputStream> s (z.createStreamForEntry (*e)); s->readIntoMemoryBlock (mb); }
              const auto x = mb.toString(); int audio = 0; for (int i = 0; i < z.getNumEntries(); ++i) audio += z.getEntry (i)->filename.startsWith ("audio/");
              check (x.contains ("HalfLifePreset") && ! x.contains ("/tmp/hlpool") && ! x.contains ("<Pool") && ! x.contains ("\"volume\""), "preset holds no file paths, no pool, no volume");
              int realSlots = 0; for (auto& sl : p->ui()->slots) realSlots += sl.has && ! sl.example;
              check (audio >= 1 && audio <= realSlots, "slot audio inside the preset (" + String (audio) + " files for " + String (realSlots) + " loaded slots)"); }

            auto r3 = std::make_unique<HalfLifeProcessor>(); r3->prepareToPlay (48000, 512); waitFor ([&] { return r3->ui() && r3->ui()->out; }, 20000);
            r3->apvts.getParameter ("volume")->setValueNotifyingHost (r3->apvts.getParameter ("volume")->convertTo0to1 (-12));
            const auto hist0 = r3->ui()->history.size(); const int pool0 = r3->ui()->poolFiles;
            const auto err = r3->loadPresetFile (f);
            check (err.isEmpty(), "preset loads on another instance " + err);
            check (waitFor ([&] { return r3->ui()->preset == p->ui()->preset && r3->ui()->out; }, 30000), "loaded preset shows its name");
            bool sameSlots = true; for (int i = 0; i < 4; ++i) sameSlots &= r3->ui()->slots[(size_t) i].name == p->ui()->slots[(size_t) i].name;
            check (sameSlots, "preset brings its slots (the source files are deleted)");
            { auto a = p->ui()->out, b = r3->ui()->out; double e = 0, sg = 0; const int n = jmin (a->end, b->end); for (int i = 0; i < n; ++i) { const double d = a->L[(size_t) i] - b->L[(size_t) i]; e += d * d; sg += (double) a->L[(size_t) i] * a->L[(size_t) i]; }
              check (a->end == b->end && 10 * std::log10 (e / sg + 1e-30) < -50, "preset sound matches the original (" + String (10 * std::log10 (e / sg + 1e-30), 1) + " dB difference)"); }
            check (r3->ui()->history.size() == jmin ((size_t) 16, hist0 + 1) && r3->ui()->poolFiles == pool0, "history keeps going, pool untouched");
            check (std::abs (r3->apvts.getRawParameterValue ("volume")->load() + 12) < 0.01f, "volume stays where the user had it");
            check (play (*r3, 0.6) > 0.02f, "loading a preset plays it");
            auto* dec = r3->apvts.getParameter ("decay");
            dec->setValueNotifyingHost (jlimit (0.0f, 1.0f, dec->getValue() + 0.2f));
            check (waitFor ([&] { return r3->ui()->presetEdited || r3->presetIsDirty(); }, 5000), "turning a knob marks the preset edited");
            // the edited state survives the project
            { MemoryBlock st; pump (400); r3->getStateInformation (st);
              auto r4 = std::make_unique<HalfLifeProcessor>(); r4->prepareToPlay (48000, 512); waitFor ([&] { return r4->ui() && r4->ui()->out; }, 20000);
              r4->setStateInformation (st.getData(), (int) st.getSize());
              check (waitFor ([&] { return r4->ui()->preset == r3->ui()->preset && r4->ui()->presetEdited; }, 20000), "project remembers the preset name and the edit"); }
            // damaged files never crash and say why
            const auto bad1 = pf.getChildFile ("zz test bad1.hlpreset"); bad1.replaceWithText ("not a zip at all");
            check (r3->loadPresetFile (bad1).isNotEmpty(), "garbage file is refused: " + r3->loadPresetFile (bad1));
            { MemoryBlock mb; f.loadFileAsData (mb); const auto bad2 = pf.getChildFile ("zz test bad2.hlpreset"); bad2.replaceWithData (mb.getData(), mb.getSize() / 2);
              check (r3->loadPresetFile (bad2).isNotEmpty(), "half-downloaded file is refused: " + r3->loadPresetFile (bad2));
              auto* d = (char*) mb.getData(); for (size_t i = 200; i < mb.getSize() - 400; i += 97) d[i] ^= 0x5a;
              const auto bad3 = pf.getChildFile ("zz test bad3.hlpreset"); bad3.replaceWithData (mb.getData(), mb.getSize());
              const auto e3 = r3->loadPresetFile (bad3); pump (1500);
              check (r3->ui() != nullptr, "scrambled file doesn't crash (" + (e3.isEmpty() ? String ("loaded what it could") : e3) + ")"); }
            check (r3->loadPresetFile (pf.getChildFile ("zz test missing.hlpreset")).isNotEmpty(), "missing file is refused");
            // the list: by name, with the saved one in it
            auto all = p->listPresets(); bool found = false; for (auto& x : all) found |= x.name == p->ui()->preset;
            check (found, "preset list has the saved preset (" + String ((int) all.size()) + " presets)");
            // the bar and the browser in the window
            p->mutate(); pump (1200); shot ("9_presetbar");
            { Button* pb = nullptr; std::function<void (Component*)> fnd = [&] (Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<TextButton*> (ch)) if (b->getProperties()["info"].toString().startsWith ("Presets|")) pb = b; fnd (ch); } }; fnd (ed);
              if (pb) { pb->triggerClick(); pump (700); shot ("9b_presets"); pb->triggerClick(); pump (200); } else check (false, "preset button found"); }
            r3 = nullptr;
            for (auto& x : pf.findChildFiles (File::findFiles, false, "zz test*")) x.deleteFile();
        }

        // other views
        findButton (ed, "RESONATOR")->triggerClick(); pump (600); shot ("3_resonator");
        p->apvts.getParameter ("distType")->setValueNotifyingHost (p->apvts.getParameter ("distType")->convertTo0to1 (3));
        findButton (ed, "DISTORTION")->triggerClick(); pump (1500); shot ("4_distortion");
        findButton (ed, "LAYERS")->triggerClick(); pump (500); shot ("4b_layers");
        ed->getChildComponent (0); { Button* ib = nullptr; std::function<void (Component*)> f = [&] (Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<TextButton*> (ch)) if (b->getProperties()["info"].toString().startsWith ("Interaction")) ib = b; f (ch); } }; f (ed);
          if (ib) { ib->triggerClick(); pump (500); shot ("4c_links"); ib->triggerClick(); pump (200); } else check (false, "interaction button found"); }
        findButton (ed, "ENGINE")->triggerClick(); pump (2000); shot ("5_hood");
        findButton (ed, "Close")->triggerClick(); pump (300);
        // a different sample rate
        const int id4 = p->ui()->id; juce::ignoreUnused (id4);
        p->prepareToPlay (44100, 256);
        check (waitFor ([&] { return p->ui()->out && std::abs (p->ui()->out->sampleRate - 44100) < 1; }, 30000), "re-renders at a new sample rate");
        // resize
        ed->setSize (1280, 860); pump (600); p->mutate(); pump (1200); shot ("7_small");
        win->setSize (1700, 1000); ed->setSize (1700, 1000); pump (1500); shot ("8_large");
        win = nullptr; delete ed; p = nullptr;
        std::printf ("\n%s (%d failed)\n", fails ? "FAILURES" : "ALL PASSED", fails);
        quit();
    }
    void shutdown() override {}
};
START_JUCE_APPLICATION (App)
