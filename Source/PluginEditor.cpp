#include "PluginEditor.h"
#include "Shaders.h"

using namespace juce::gl;
using namespace ui;

static juce::String U8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
static const juce::String DOT = U8 (" \xc2\xb7 ");

/* ======================= display: the current sound ======================= */
/* A clean waveform: the exact min/max shape per screen pixel (positive and negative apart, so polarity reads),
   a brighter core at the average level, a faint zero line; drawn once per sound at the screen's resolution.
   A new sound lights up: it fades in from nothing, flares a little past full and settles (about 1.2 s). */
/* The waveform as solid shapes: per screen column, the min/max outline (positive and negative apart, so polarity
   reads) and a brighter core at the average level. Drawn as rectangles rather than an image, because OpenGL smooths
   images and that turned the thin columns grey. A new sound lights up: it fades in, flares a little past full, settles. */
void Display::rebuild (const UiState& st)
{
    const float sc = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    const auto wa = waveArea().toFloat();
    const int W = juce::jmax (2, juce::roundToInt (wa.getWidth() * sc)); const float cw = 1.0f / sc;
    const float mid = wa.getHeight() / 2.0f, amp = wa.getHeight() * 0.46f;
    auto disp = [] (double v) { return (float) ((v < 0 ? -1.0 : 1.0) * std::pow (std::abs (v), 0.8)); };
    body.clear(); core.clear(); curve.clear(); hasCurve = false;
    if (st.out && st.out->end > 0)
    {
        const auto& o = *st.out; const int n = o.end;
        for (int X = 0; X < W; ++X)
        {
            const int a0 = (int) std::floor ((double) X / W * n), a1 = juce::jmax (a0 + 1, (int) std::floor ((double) (X + 1) / W * n));
            double lo = 0, hi = 0, sp = 0, sn = 0; int cp = 0, cn = 0;
            for (int i = a0; i < a1 && i < n; ++i) { const double v = ((double) o.L[(size_t) i] + o.R[(size_t) i]) * 0.5; lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); if (v >= 0) { sp += v * v; cp++; } else { sn += v * v; cn++; } }
            const float x = X * cw, yT = mid - disp (hi) * amp, yB = mid - disp (lo) * amp;
            body.addWithoutMerging ({ x, yT, cw, juce::jmax (cw, yB - yT) });
            const float cT = mid - disp (cp ? std::sqrt (sp / cp) : 0) * amp, cB = mid - disp (cn ? -std::sqrt (sn / cn) : 0) * amp;
            if (cB - cT > 0.5f) core.addWithoutMerging ({ x, cT, cw, cB - cT });
        }
        const double spc = (double) n / W;   // very short sounds: the actual sample curve
        if (spc < 6)
        {
            hasCurve = true;
            for (int X = 0; X < W; ++X) { const int i = juce::jmin (n - 1, (int) std::floor (X * spc)); const float y = mid - disp (((double) o.L[(size_t) i] + o.R[(size_t) i]) * 0.5) * amp; if (X == 0) curve.startNewSubPath (0, y); else curve.lineTo (X * cw, y); }
        }
    }
    built = true;
    if (st.id != shownId) fadeStart = juce::Time::getMillisecondCounterHiRes();
    shownId = st.id; shownOut = st.out.get();
}

void Display::tick()
{
    auto st = proc.ui(); if (! st) return;
    if (getWidth() > 0 && (! built || shownId != st->id || shownOut != st->out.get())) { rebuild (*st); repaint(); }
    const float ph = proc.playheadFraction();
    const bool fading = juce::Time::getMillisecondCounterHiRes() - fadeStart < 1250;
    if (ph >= 0 || lastPh >= 0 || fading || wasFading) repaint();
    lastPh = ph; wasFading = fading;
}

void Display::paint (juce::Graphics& g)
{
    // a faint panel behind the sound, the ink still visible through it; the waveform sits inside it
    g.setColour (juce::Colour (14, 12, 11).withAlpha (0.25f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6);
    const auto r = waveArea().toFloat();
    if (! built) return;
    const double ft = (juce::Time::getMillisecondCounterHiRes() - fadeStart) / 1000.0;
    float a = 1, flare = 0;
    if (ft < 1.2) { const float up = (float) juce::jmin (1.0, ft / 0.55); a = up * up * (3 - 2 * up); flare = (float) juce::jmax (0.0, std::sin (juce::MathConstants<double>::pi * juce::jmin (1.0, ft / 1.2))) * 0.5f; }
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::translation (r.getX(), r.getY()));
        g.setColour (col::ink.withAlpha (0.1f * a)); g.fillRect (0.0f, r.getHeight() / 2 - 0.5f, r.getWidth(), 1.0f);   // zero line
        // white at 95%: the ink never shows through the sound; the core a touch brighter; the flare lifts it past white briefly
        g.setColour (juce::Colour (0xfff3efe8).withAlpha (0.95f * a)); g.fillRectList (body);
        g.setColour (juce::Colours::white.withAlpha (juce::jmin (1.0f, (0.6f + flare) * a))); g.fillRectList (core);
        if (hasCurve) { g.setColour (juce::Colours::white.withAlpha (0.95f * a)); g.strokePath (curve, juce::PathStrokeType (1.4f)); }
    }
    {   // the Shape it was built toward, as a dashed rust line over the top half
        const auto P = proc.settings(); auto st = proc.ui();
        if (P.shFollow >= 0.01 && st && st->out && st->out->len > 0)
        {
            juce::Path p; const int n = juce::jmax (2, (int) r.getWidth() / 2);
            for (int i = 0; i <= n; ++i) { const double t = (double) i / n * st->out->len; const float y = r.getCentreY() - (float) std::pow (hl::shapeLevel (P, t) * 0.89, 0.8) * r.getHeight() * 0.46f; if (i == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + r.getWidth() * i / n, y); }
            juce::Path d; const float dash[] = { 4, 4 }; juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
            g.setColour (col::rustHi.withAlpha (0.6f)); g.fillPath (d);
        }
    }
    const float ph = proc.playheadFraction();
    if (ph >= 0) { g.setColour (col::ink.withAlpha (0.4f)); g.fillRect (r.getX() + r.getWidth() * ph, r.getY(), 1.0f, r.getHeight()); }
    {   // the waveform is the export: drag it out
        static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = proc.getLastNote();
        const juce::String t = "drag into your DAW" + DOT + juce::String (N[((n % 12) + 12) % 12]) + juce::String ((int) std::floor (n / 12.0) - 2);
        const auto f = Look::get().font (12, 700); const float tw = juce::GlyphArrangement::getStringWidth (f, t) + S (22);
        const juce::Rectangle<float> chip (r.getRight() - tw - 4, r.getBottom() - S (28), tw, (float) S (24));
        g.setColour (juce::Colours::black.withAlpha (isMouseOver() ? 0.7f : 0.5f)); g.fillRoundedRectangle (chip, 3);
        g.setColour (col::ink.withAlpha (isMouseOver() ? 0.35f : 0.16f)); g.drawRoundedRectangle (chip.reduced (0.5f), 3, 1.0f);
        g.setColour (isMouseOver() ? col::ink : col::muted); g.setFont (f);
        g.drawText (t, chip, juce::Justification::centred);
    }
    if (proc.isBusy()) { g.setColour (col::muted); g.setFont (Look::get().font (12, 700)); g.drawText (U8 ("rendering\xe2\x80\xa6"), getLocalBounds().reduced (10, 8), juce::Justification::topRight); }
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5) return;
    dragging = true;
    const auto f = proc.makeDragFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    else dragging = false;
}

/* ======================= slot cards ======================= */
SlotCard::SlotCard (HalfLifeProcessor& p, int i) : proc (p), idx (i)
{
    const char* ids[] = { "roleA", "roleB", "roleC", "roleD" };
    role.addItemList ({ "Auto", "Transient", "Body", "Tail", "Texture" }, 1);
    roleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids[i], role);
    // one menu for loading: a file from disk, or a built-in test sound
    lib.setTextWhenNothingSelected ("Load");
    lib.addItem (U8 ("File\xe2\x80\xa6"), 999); lib.addSeparator(); lib.addSectionHeading ("Built-in test sounds");
    int k = 1; for (auto& n : hl::Engine::libNames()) lib.addItem (n, k++);
    lib.onChange = [this]
    {
        const int id = lib.getSelectedId(); lib.setSelectedId (0, juce::dontSendNotification);
        if (id == 999)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sound into slot " + juce::String (hl::keyName (idx)), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadFile (idx, fc.getResult()); });
        }
        else if (id >= 1 && id <= (int) hl::Engine::libNames().size()) proc.loadBuiltin (idx, hl::Engine::libNames()[(size_t) id - 1]);
    };
    role.getProperties().set ("info", "Role|What this sample plays: transient, body, tail or texture. Auto picks per sound.");
    lib.getProperties().set ("info", "Load|A file from disk, or one of the built-in test sounds.");
    kind (pin, "icoPin"); kind (clear, "x");
    pin.getProperties().set ("info", "Pin|Keep this sample when Perish or Fresh draw from the pool.");
    clear.getProperties().set ("info", "Clear|Empty this slot.");
    pin.onClick = [this] { proc.togglePin (idx); };
    clear.onClick = [this] { proc.clearSlot (idx); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &pin, &clear, &role, &lib }) addAndMakeVisible (c);
}

void SlotCard::update (const UiState::SlotUi& n)
{
    s = n;
    kind (pin, s.pinned ? "icoPinOn" : "icoPin");
    // the sample's details (length, loudness, problems) live in the hint line, the row stays one name and one shape
    getProperties().set ("info", juce::String ("Slot ") + hl::keyName (idx) + "|" + (s.has ? s.meta + ". " : juce::String()) + "Drop a file here, or use Load. Bright bars are the parts its role can use.");
    repaint();
}

void SlotCard::filesDropped (const juce::StringArray& files, int, int)
{
    drag = false; repaint();
    if (files.size() == 1 && juce::File (files[0]).existsAsFile()) { proc.loadFile (idx, juce::File (files[0])); return; }
    juce::Array<juce::File> fs; for (auto& f : files) fs.add (juce::File (f));
    proc.poolAdd (fs);   // a folder or several files join the pool, as in the prototype
}

/* one compact row: letter, name, pin, clear; then the shape of the sample, its role and Load */
void SlotCard::resized()
{
    auto r = getLocalBounds().withTrimmedTop (5);
    auto head = r.removeFromTop (S (18));
    clear.setBounds (head.removeFromRight (S (16))); head.removeFromRight (2);
    pin.setBounds (head.removeFromRight (S (18)));
    r.removeFromTop (5);
    auto row = r.removeFromTop (S (20));
    lib.setBounds (row.removeFromRight (S (52))); row.removeFromRight (4);
    role.setBounds (row.removeFromRight (S (82)));
}

void SlotCard::paint (juce::Graphics& g)
{
    const auto& L = Look::get();
    g.setColour (juce::Colour (246, 242, 236).withAlpha (0.07f)); g.fillRect (0, 0, getWidth(), 1);
    if (drag) { g.setColour (col::bone.withAlpha (0.8f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f, 1.5f), 3, 1.5f); }
    auto r = getLocalBounds().withTrimmedTop (5);
    auto head = r.removeFromTop (S (18));
    g.setColour (col::muted); g.setFont (L.font (12, 900)); g.drawText (hl::keyName (idx), head.removeFromLeft (S (14)), juce::Justification::centredLeft);
    head.removeFromLeft (4); head.removeFromRight (S (16) + 2 + S (18) + 4);
    g.setFont (L.font (13, 700)); g.setColour (s.metaBad ? col::rustHi : s.has ? col::ink : col::muted);
    g.drawFittedText (s.has ? s.name : juce::String ("Empty"), head, juce::Justification::centredLeft, 1, 0.85f);
    r.removeFromTop (5);
    auto wave = r.removeFromTop (S (20)).withTrimmedRight (S (52) + 4 + S (82) + 6).withTrimmedLeft (S (14) + 4);
    if (! s.has) { g.setColour (col::dim); g.setFont (L.font (11.5f, 400)); g.drawText ("drop audio", wave, juce::Justification::centredLeft); return; }
    const int n = (int) s.db.size(); const float bw = (float) wave.getWidth() / juce::jmax (1, n);
    for (int k = 0; k < n; ++k)
    {
        const float v = juce::jmax (0.0f, (s.db[(size_t) k] + 60) / 60);
        const float bh = juce::jmax (1.0f, v * wave.getHeight());
        g.setColour (s.on[(size_t) k] ? col::bone.withAlpha (0.78f) : col::ink.withAlpha (0.14f));
        g.fillRect (wave.getX() + k * bw, wave.getY() + (wave.getHeight() - bh) / 2, juce::jmax (1.0f, bw - 0.2f), bh);
    }
    g.setColour (col::rustHi.withAlpha (0.9f)); for (int k : s.onsets) g.fillRect (wave.getX() + k * bw, (float) wave.getY(), 1.0f, 2.0f);
}

/* ======================= keyboard ======================= */
Keybed::Keybed (HalfLifeProcessor& p) : proc (p)
{
    const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C" };
    for (int i = 0; i < 13; ++i)
    {
        auto* k = keys.add (new Key()); k->i = i; k->kb = this;
        const bool black = juce::String (names[i]).contains ("#");
        k->setButtonText (black ? "" : names[i]); kind (*k, black ? "keyBlack" : "key");
        k->getProperties().set ("info", "Keys|Play the sound at this note. The last key you play sets the note of the dragged file.");
        addAndMakeVisible (k);
    }
    kind (down, "octx"); kind (up, "octx");
    down.getProperties().set ("info", "Octave down|The keys play one octave lower."); up.getProperties().set ("info", "Octave up|The keys play one octave higher.");
    down.onClick = [this] { setOct (proc.getOctave() - 1); }; up.onClick = [this] { setOct (proc.getOctave() + 1); };
    style (oct, 13, 700, col::ink, juce::Justification::centred);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &down, &up, &oct }) addAndMakeVisible (c);
    setOct (proc.getOctave());
}
void Keybed::setOct (int o) { proc.setOctave (o); oct.setText ("C" + juce::String (3 + proc.getOctave()), juce::dontSendNotification); }
void Keybed::Key::mouseDown (const juce::MouseEvent& e) { juce::TextButton::mouseDown (e); held = true; kb->proc.keyDown (60 + 12 * kb->proc.getOctave() + i); }
void Keybed::Key::mouseUp (const juce::MouseEvent& e) { juce::TextButton::mouseUp (e); if (held) kb->proc.keyUp (60 + 12 * kb->proc.getOctave() + i); held = false; }
void Keybed::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (S (20)); r.removeFromTop (6);
    up.setBounds (top.removeFromRight (S (22))); oct.setBounds (top.removeFromRight (S (36))); down.setBounds (top.removeFromRight (S (22)));
    // eight white keys C..C, the black ones over their gaps
    static const int whiteOf[13] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6, 7 };
    const float ww = (r.getWidth() + 1) / 8.0f;
    for (int i = 0; i < 13; ++i)
        if (whiteOf[i] >= 0) keys[i]->setBounds (r.getX() + juce::roundToInt (whiteOf[i] * ww), r.getY(), juce::roundToInt (ww) - 1, r.getHeight());
    for (int i = 0; i < 13; ++i)
        if (whiteOf[i] < 0) { const float x = r.getX() + whiteOf[i - 1] * ww + ww - 0.5f - ww * 0.3f; keys[i]->setBounds (juce::roundToInt (x), r.getY(), juce::roundToInt (ww * 0.6f), r.getHeight() * 58 / 100); keys[i]->toFront (false); }
}
void Keybed::paint (juce::Graphics&) { oct.setText ("C" + juce::String (3 + proc.getOctave()), juce::dontSendNotification); }
void Keybed::tick()
{
    const int k = proc.getLastNote() - (60 + 12 * proc.getOctave());
    if (k == lit) return;
    lit = k;
    for (int i = 0; i < 13; ++i) { const bool black = keys[i]->getButtonText().isEmpty(); kind (*keys[i], (black ? juce::String ("keyBlack") : juce::String ("key")) + (i == k ? "On" : "")); }
}

/* ======================= history ======================= */
void History::paint (juce::Graphics& g)
{
    const auto& L = Look::get();
    g.setColour (juce::Colour (0xffd2cbc0)); g.setFont (L.font (11, 700).withExtraKerningFactor (0.16f));
    g.drawText ("HISTORY", 0, 0, getWidth(), S (14), juce::Justification::centredLeft);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto c = cell (i); if (c.getRight() > getWidth()) break;
        const auto& h = items[(size_t) i];
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (c.toFloat(), 2);
        g.setColour (h.cur ? col::bone.withAlpha (0.8f) : col::ink.withAlpha (0.1f)); g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 2, 1.0f);
        if (h.out && ! h.out->mixMax.empty())
        {
            const auto t = c.reduced (2).removeFromTop (c.getHeight() - S (14)); const int cols = t.getWidth(); const int n = (int) h.out->mixMax.size();
            for (int x = 0; x < cols; ++x)
            {
                const int k = juce::jmin (n - 1, (int) ((double) x / cols * n));
                const float v = juce::jmax (h.out->mixMax[(size_t) k], -h.out->mixMin[(size_t) k]); const int hh = juce::jmax (1, juce::roundToInt (v * t.getHeight()));
                g.setColour (col::layer (0).withAlpha (0.95f - 0.65f * x / cols)); g.fillRect (t.getX() + x, t.getY() + (t.getHeight() - hh) / 2, 1, hh);
            }
        }
        g.setColour (h.cur ? col::ink : col::muted); g.setFont (L.font (10.5f, 400));
        g.drawText ("#" + juce::String (h.id), c.reduced (3, 1), juce::Justification::bottomLeft);
    }
}
void History::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) items.size(); ++i) if (cell (i).contains (e.getPosition())) { proc.recall (items[(size_t) i].id); return; }
}

/* ======================= drag well ======================= */
void DragWell::paint (juce::Graphics& g)
{
    const auto& L = Look::get();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool over = isMouseOver();
    g.setColour (col::rustHi.withAlpha (over ? 0.12f : 0.06f)); g.fillRoundedRectangle (r, 4);
    juce::Path p; p.addRoundedRectangle (r, 4); juce::Path d; const float dash[] = { 4, 3 };
    juce::PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
    g.setColour (col::rustHi.withAlpha (over ? 0.9f : 0.6f)); g.fillPath (d);
    auto area = getLocalBounds().reduced (16, 12);
    auto text = area.removeFromBottom (S (16)); area.removeFromBottom (6);
    if (u && u->out && ! u->out->mixMax.empty())
    {
        const auto& o = *u->out; const int n = (int) o.mixMax.size(), cols = area.getWidth(); const float mid = (float) area.getCentreY(), hh = area.getHeight() * 0.5f;
        for (int x = 0; x < cols; ++x)
        {
            const int k = juce::jmin (n - 1, (int) ((double) x / cols * n));
            const float top = juce::jmin (1.0f, o.mixMax[(size_t) k]), bot = juce::jmin (1.0f, -o.mixMin[(size_t) k]);
            g.setColour (col::bone.withAlpha (0.85f)); g.fillRect ((float) area.getX() + x, mid - top * hh, 1.0f, juce::jmax (1.0f, (top + bot) * hh));
        }
    }
    static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int note = proc.getLastNote();
    g.setColour (col::ink); g.setFont (L.font (12.5f, 700));
    g.drawText ("Drag me out" + DOT + juce::String (N[((note % 12) + 12) % 12]) + juce::String ((int) std::floor (note / 12.0) - 2), text, juce::Justification::centred);
}

void DragWell::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5) return;
    dragging = true;
    const auto f = proc.makeDragFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    else dragging = false;
}

/* ======================= shape view ======================= */
void ShapeView::paint (juce::Graphics& g)
{
    const auto P = proc.settings();
    const auto r = getLocalBounds().toFloat().reduced (1);
    g.setColour (juce::Colours::black.withAlpha (0.3f)); g.fillRoundedRectangle (r, 3);
    const auto a = r.reduced (8, 8);
    juce::Path p; const int n = juce::jmax (2, (int) a.getWidth());
    for (int i = 0; i <= n; ++i) { const double t = (double) i / n * P.maxLen; const float y = a.getBottom() - (float) hl::shapeLevel (P, t) * a.getHeight(); if (i == 0) p.startNewSubPath (a.getX(), y); else p.lineTo (a.getX() + (float) i, y); }
    juce::Path fill (p); fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
    const bool on = P.shFollow >= 0.01;
    g.setColour (col::rustHi.withAlpha (on ? 0.14f : 0.05f)); g.fillPath (fill);
    g.setColour (on ? col::rustHi : col::dim); g.strokePath (p, juce::PathStrokeType (1.6f));
    if (! on) { g.setColour (col::muted); g.setFont (Look::get().font (13, 700)); g.drawText ("Follow is off", r, juce::Justification::centred); }
}

/* ======================= interaction graph ======================= */
void Graph::paint (juce::Graphics& g)
{
    const auto& L = Look::get();
    g.setColour (juce::Colours::black.withAlpha (0.3f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3);
    if (! u) return;
    const auto& keys = u->order; const int n = (int) keys.size(); const float w = (float) getWidth(), h = (float) getHeight(), mid = h / 2, pad = 14;
    auto px = [&] (int k) { auto it = std::find (keys.begin(), keys.end(), k); const int i = (int) (it - keys.begin()); return n < 2 ? w / 2 : pad + i * (w - 2 * pad) / (n - 1); };
    const double fac = juce::jmin (1.0, interact * 1.6);
    for (auto& l : u->links)
    {
        const float x1 = px (l.from), x2 = px (l.to); const bool upArc = ! l.clock; const float off = upArc ? -13.0f : 13.0f;
        const float reach = juce::jmin (mid - 16, (16 + (x2 - x1) * 0.32f) * juce::jmax (1.0f, h / 130.0f)), cy = mid + (upArc ? -1 : 1) * reach * 2, y0 = mid + off;
        const auto c = col::layer (l.from);
        juce::Path p; p.startNewSubPath (x1, y0); p.quadraticTo ((x1 + x2) / 2, cy, x2, y0);
        juce::PathStrokeType st ((float) (1 + std::abs (l.amount) * fac * 3));
        g.setColour (c);
        if (upArc) g.strokePath (p, st);
        else { juce::Path dashed; const float d[] = { 5, 4 }; st.createDashedStroke (dashed, p, d, 2); g.fillPath (dashed); }
        const float ang = std::atan2 (y0 - cy, x2 - (x1 + x2) / 2);
        juce::Path ar; ar.addTriangle (x2, y0, x2 - 8 * std::cos (ang - 0.45f), y0 - 8 * std::sin (ang - 0.45f), x2 - 8 * std::cos (ang + 0.45f), y0 - 8 * std::sin (ang + 0.45f)); g.fillPath (ar);
        if (n > 1 && (w - 2 * pad) / (n - 1) < 40) continue;   // too tight for labels: arcs only
        const float ay = 0.5f * y0 + 0.5f * cy;
        const juce::String lbl = l.clock ? juce::String ("hits") : l.text.contains ("ducks") ? juce::String ("duck") : juce::String (l.amount > 0 ? "+" : U8 ("\xe2\x88\x92")) + l.text.fromLastOccurrenceOf ("'s ", false, false).substring (0, 4);
        g.setFont (L.font (11, 700)); const float tw = juce::GlyphArrangement::getStringWidth (L.font (11, 700), lbl) + 6;
        g.setColour (col::screen); g.fillRect ((x1 + x2) / 2 - tw / 2, ay - 7, tw, 14.0f);
        g.setColour (c); g.drawText (lbl, juce::Rectangle<float> ((x1 + x2) / 2 - tw / 2, ay - 7, tw, 14), juce::Justification::centred);
    }
    for (int k : keys)
    {
        const float cx = px (k);
        const float rr = juce::jlimit (8.0f, 12.0f, (n > 1 ? (w - 2 * pad) / (n - 1) : 24.0f) * 0.42f);
        g.setColour (col::layer (k)); g.fillEllipse (cx - rr, mid - rr, rr * 2, rr * 2);
        g.setColour (col::screen); g.setFont (L.font (rr < 10 ? 9.5f : 11.0f, 700));
        const char* nm[] = { "A", "B", "C", "D", "N", "Cl", "Bl" };
        g.drawText (nm[k], juce::Rectangle<float> (cx - rr, mid - rr, rr * 2, rr * 2), juce::Justification::centred);
    }
}

/* ======================= distortion curve ======================= */
void Curve::paint (juce::Graphics& g)
{
    const auto& L = Look::get();
    const float w = (float) getWidth(), h = (float) getHeight();
    g.fillAll (col::screen);
    g.setColour (juce::Colour (150, 108, 70).withAlpha (0.22f)); g.fillRect (0.0f, std::round (h / 2), w, 1.0f); g.fillRect (std::round (w / 2), 0.0f, 1.0f, h);
    g.setFont (L.font (11, 400)); g.setColour (col::muted);
    const auto P = proc.settings();
    if (P.distType == hl::DistType::Off) { g.drawText ("bypassed", 6, 4, 200, 14, juce::Justification::topLeft); return; }
    juce::Path p;
    if (P.distType == hl::DistType::Phase)
    {
        g.drawText ("a sine through the phase settings", 6, 4, 260, 14, juce::Justification::topLeft);
        const double SR2 = 96000, per = SR2 / 300; const int total = (int) std::ceil (per * 6);
        std::vector<float> s ((size_t) total); for (int i = 0; i < total; ++i) s[(size_t) i] = (float) (0.9 * std::sin (2 * hl::PI * i / per));
        hl::phaseDistortOf (P, s, SR2);
        const int st = (int) std::floor (per * 3), span = (int) std::floor (per * 2);
        for (int i = 0; i <= span; ++i) { const float x = i / (float) span * w, y = h / 2 - s[(size_t) (st + i)] * h * 0.42f; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
    }
    else
    {
        g.drawText ("in > out", 6, 4, 200, 14, juce::Justification::topLeft);
        const auto fn = hl::distFnOf (P); std::vector<double> ys; double mx = 0;
        for (int i = 0; i <= 300; ++i) { const double y = fn (i / 300.0 * 2 - 1); ys.push_back (y); mx = juce::jmax (mx, std::abs (y)); }
        const double sc = 1 / juce::jmax (1.0, mx);
        g.setColour (col::ink.withAlpha (0.25f)); g.drawLine (0, h, w, 0);
        for (int i = 0; i <= 300; ++i) { const float x = i / 300.0f * w, y = (float) (h / 2 - ys[(size_t) i] * sc * h * 0.46); if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
    }
    g.setColour (col::layer (0)); g.strokePath (p, juce::PathStrokeType (1.5f));
}

/* ======================= chaos view ======================= */
void ChaosView::push (const HalfLifeProcessor::Live& l)
{
    for (int i = 0; i < 4; ++i) { trails[(size_t) i].push_back ({ l.h[i][0], l.h[i][2] }); trails[(size_t) i + 4].push_back ({ l.s[i][0], l.s[i][2] }); }
    for (auto& t : trails) while (t.size() > 260) t.pop_front();
    if (isShowing()) repaint();
}
void ChaosView::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    g.fillAll (juce::Colour (0xff030302));
    auto P2 = [&] (juce::Point<float> p) { return juce::Point<float> (w * 0.5f + p.x * w * 0.44f, h * 0.5f - p.y * h * 0.42f); };
    for (int k = 0; k < 8; ++k)
    {
        const auto& t = trails[(size_t) k]; const auto c = k < 4 ? juce::Colour (138, 123, 104) : col::layer (k - 4); const float base = k < 4 ? 0.35f : 0.8f;
        for (size_t i = 1; i < t.size(); ++i) { g.setColour (c.withAlpha (base * std::pow ((float) i / t.size(), 1.6f))); g.drawLine (juce::Line<float> (P2 (t[i - 1]), P2 (t[i])), 1.0f); }
        if (! t.empty()) { const auto q = P2 (t.back()); g.setColour (c.withAlpha (juce::jmin (1.0f, base * 1.6f))); g.fillRect (std::round (q.x) - 1, std::round (q.y) - 1, 3.0f, 3.0f); }
    }
    auto& R = juce::Random::getSystemRandom();
    g.setColour (juce::Colour (221, 211, 194).withAlpha (0.05f));
    for (int i = 0; i < 140; ++i) g.fillRect ((float) R.nextInt ((int) w), (float) R.nextInt ((int) h), 1.0f, 1.0f);
}

/* ======================= bars ======================= */
void Bars::paint (juce::Graphics& g)
{
    const auto& L = Look::get(); const int rh = S (18);
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& r = rows[(size_t) i]; const int y = i * rh;
        g.setColour (col::muted); g.setFont (L.font (12.5f, 700)); g.drawText (r.name, 0, y, S (130), rh, juce::Justification::centredLeft);
        const juce::Rectangle<float> bar ((float) S (138), y + rh / 2.0f - 2, (float) getWidth() - S (138) - S (56), 4);
        g.setColour (col::track()); g.fillRect (bar);
        g.setColour (r.c);
        if (r.centred) { const float c = bar.getCentreX(), x = c + juce::jmin (0.0f, r.v) * bar.getWidth() / 2; g.fillRect (x, bar.getY(), std::abs (r.v) * bar.getWidth() / 2, bar.getHeight()); g.setColour (col::dim); g.fillRect (c, bar.getY() - 2, 1.0f, 8.0f); }
        else g.fillRect (bar.getX(), bar.getY(), juce::jlimit (0.0f, 1.0f, r.v) * bar.getWidth(), bar.getHeight());
        g.setColour (col::ink); g.setFont (L.font (12.5f, 400));
        g.drawText (r.centred ? (r.v >= 0 ? "+" : "") + juce::String (r.v, 2) : juce::String (juce::roundToInt (r.v * 100)) + "%", getWidth() - S (52), y, S (52), rh, juce::Justification::centredRight);
    }
}


/* ======================= the info line ======================= */
namespace
{
const char* knobInfo (const juce::String& id)
{
    static const std::map<juce::String, const char*> m = {
        { "instability", "Instability|How hard the hidden chaos pushes grain scatter, gate time, pitch drift and more." },
        { "mutation", "Mutation|How far Mutate moves from the current sound." },
        { "glide", "Glide|How much each slot's slow chaos LFO moves its grains." },
        { "rot", "Rot|Damage that grows as the sound dies: pitch sag, erosion, bit rot, dropouts." },
        { "rotOnset", "Rot sets in|How late in the decay the rot starts." },
        { "interact", "Interaction|How many links between layers, and how strong." },
        { "cands", "Candidates|How many versions to render before keeping one." },
        { "poolFit", "Fit|+ picks samples that suit each slot's role, - picks deliberate mismatches, 0 is pure chance." },
        { "poolGate", "Loudness gate|Pool files quieter than this are skipped." },
        { "noiseLevel", "Noise level|How loud the noise layer is." },
        { "volume", "Volume|Output level." },
        { "lpgResp", "Response time|Scales how long every layer's gate stays open." },
        { "body", "Body|A slow last stage that keeps the gates partly open." },
        { "bodyTime", "Body time|How long that body lasts." },
        { "reso", "Resonance|Resonance of the gates' filters." },
        { "bright", "Brightness|How far the gates' filters open." },
        { "pitchAmt", "Amount|Semitones the pitch starts above (or below) and falls from." },
        { "pitchTime", "Time|How fast the pitch falls." },
        { "pitchCurve", "Curve|The shape of the fall." },
        { "attack", "Attack|How keys and MIDI play the sound. Baked into the dragged file." },
        { "decay", "Decay|How keys and MIDI play the sound. Baked into the dragged file." },
        { "sustain", "Sustain|How keys and MIDI play the sound. Baked into the dragged file." },
        { "release", "Release|How keys and MIDI play the sound. Baked into the dragged file." },
        { "maxLen", "Max length|The longest a sound can be." },
        { "threshold", "Scan threshold|How loud part of a sample must be to be used." },
        { "scatter", "Scatter|How often a grain or hit comes from somewhere else in the sample. 0: each layer stays in one spot. High: pieces from all over a long file." },
        { "resAmt", "Resonance|How long the resonator rings." },
        { "resPitch", "Pitch|The resonator's note." },
        { "resRand", "Random tune|Each sound detunes the resonator by up to this." },
        { "resDamp", "Damping|Higher partials die faster." },
        { "resVoices", "Voices|How many partials or strings ring." },
        { "resSpread", "Spread|Detune between voices." },
        { "resMix", "Mix|Dry sound against the resonance." },
        { "distDrive", "Drive|How hard the signal hits the distortion." },
        { "distTone", "Tone|Low-pass on the distorted signal." },
        { "distMix", "Mix|Dry against distorted." },
        { "satKnee", "Knee|Soft to hard clipping." },
        { "satAsym", "Asymmetry|Adds even harmonics." },
        { "shCurve", "Curve|Expand to compress." },
        { "shH2", "2nd harmonic|Added by the shaper." }, { "shH3", "3rd harmonic|Added by the shaper." }, { "shH5", "5th harmonic|Added by the shaper." },
        { "shSteps", "Steps|Quantises the curve into steps." },
        { "fdFolds", "Folds|How many times the wave folds back on itself." },
        { "fdBias", "Symmetry offset|Folds one side more than the other." },
        { "fdShape", "Shape|Triangle to sine folding." },
        { "pdDepth", "Depth|How far the signal bends its own timing." },
        { "pdSmooth", "Mod brightness|How bright the bending signal is." },
        { "pdFb", "Feedback|Feeds the bent signal back in." },
        { "chaosSpeed", "Speed|How fast the hidden chaos drifts between sounds." },
        { "chaosTurb", "Turbulence|How much it moves inside one sound." },
        { "chaosCouple", "Coupling|How tied the chaos circuits are to each other." },
        { "shpA", "Attack|How the sound should rise. Slow attacks stagger the entries and soften the hit." },
        { "shpD", "Decay|How long it should ring when Sustain is 0. Longer is reached by the gates and grains, never by pitch." },
        { "shpS", "Sustain|0: a hit. High: holds like a note until the release." },
        { "shpR", "Release|How it ends when Sustain is above 0." },
        { "shpFollow", "Follow|How strictly the sound follows the Shape. 0 is off." } };
    auto it = m.find (id); return it == m.end() ? nullptr : it->second;
}
void info (juce::Component& c, const juce::String& t) { c.getProperties().set ("info", t); }
juce::Component* deepest (juce::Component* c, juce::Point<int> p)
{
    for (int i = c->getNumChildComponents(); --i >= 0;)
    {
        auto* ch = c->getChildComponent (i);
        if (ch->isVisible() && ch->getBounds().contains (p)) return deepest (ch, p - ch->getPosition());
    }
    return c;
}
}

void HalfLifeEditor::updateInfo()
{
    juce::String t;
    // where the mouse is, from its screen position (works whether or not the window has focus)
    const auto mp = face.getLocalPoint (nullptr, juce::Desktop::getInstance().getMainMouseSource().getScreenPosition()).roundToInt();
    if (face.getLocalBounds().contains (mp) && face.isShowing())
        for (auto* c = deepest (&face, mp); c != nullptr && c != &face; c = c->getParentComponent())
        {
            const auto v = c->getProperties()["info"].toString();
            if (v.isNotEmpty()) { t = v; break; }
            if (auto* k = dynamic_cast<ui::Knob*> (c)) if (auto* s = knobInfo (k->pid)) { t = s; break; }
        }
    if (t != infoText) { infoText = t; face.repaint (rInfo); }
}

/* what's under the mouse: a short wrapped note at the right of the modules panel (one line in the header when narrow) */
void HalfLifeEditor::paintInfo (juce::Graphics& g)
{
    const auto& L = Look::get();
    if (rInfo.getWidth() < 80 || ! infoToggle.getProperties()["on"] || infoText.isEmpty()) return;
    const auto head = infoText.upToFirstOccurrenceOf ("|", false, false), body = infoText.fromFirstOccurrenceOf ("|", false, false);
    const bool block = rInfo.getHeight() > 50;
    juce::AttributedString as; as.setWordWrap (block ? juce::AttributedString::byWord : juce::AttributedString::none);
    as.append (head + (body.isEmpty() ? "" : block ? U8 (" \xe2\x80\x94 ") : "  "), L.font (12.5f, 700), col::ink);
    if (body.isNotEmpty()) as.append (body, L.font (12.5f, 400), col::muted);
    as.setLineSpacing (3);
    if (block)
    {
        g.setColour (col::ink.withAlpha (0.08f)); g.fillRect (rInfo.getX() - 16, rInfo.getY(), 1, rInfo.getHeight() - 6);
        juce::TextLayout tl; tl.createLayout (as, (float) rInfo.getWidth()); tl.draw (g, rInfo.toFloat());
    }
    else { as.setJustification (juce::Justification::centredLeft); juce::TextLayout tl; tl.createLayout (as, 10000.0f); g.saveState(); g.reduceClipRegion (rInfo); tl.draw (g, rInfo.toFloat().withWidth (10000.0f)); g.restoreState(); }
}

void AboutPanel::paint (juce::Graphics& g)
{
    ui::Plate::paint (g);
    const auto& L = Look::get(); int y = S (14); const int x = S (16), w = getWidth() - S (32);
    g.setColour (col::bone); g.setFont (L.font (15, 700)); g.drawText ("An endless percussive engine.", x, y, w, S (20), juce::Justification::left); y += S (28);
    g.setColour (col::muted); g.setFont (L.font (13, 400));
    g.drawFittedText ("Every press makes a sound that has never existed: grains of your samples, gated, rung and rotted.", x, y, w, S (36), juce::Justification::topLeft, 2, 1.0f); y += S (46);
    const char* steps[3][2] = { { "Load samples", ", or use the ones already there. Drop a folder to give it a pool to draw from." },
                               { "Play with the parameters, or randomize", " with Fresh, Mutate and Perish, until you like it." },
                               { "Pick the key", " on the keyboard, then drag the waveform out into your DAW." } };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (col::bone); g.setFont (L.font (13, 700)); g.drawText (juce::String (i + 1), x, y, S (16), S (18), juce::Justification::topLeft);
        juce::AttributedString as; as.setWordWrap (juce::AttributedString::byWord);
        as.append (steps[i][0], L.font (13, 700), col::ink); as.append (steps[i][1], L.font (13, 400), col::muted);
        juce::TextLayout tl; tl.createLayout (as, (float) w - S (22)); tl.draw (g, juce::Rectangle<float> ((float) x + S (22), (float) y, (float) w - S (22), tl.getHeight()));
        y += (int) tl.getHeight() + 6;
    }
    g.setColour (col::dim); g.setFont (L.font (12, 400));
    g.drawFittedText ("Turn on Hints (?) at the top right to see what each control does.", x, y + 2, w, S (32), juce::Justification::topLeft, 2, 1.0f);
}

void AboutPanel::mouseUp (const juce::MouseEvent&) { setVisible (false); if (onClose) onClose(); }

/* ======================= presets ======================= */
static void styleField (juce::TextEditor& t, const juce::String& hint)
{
    t.setFont (Look::get().font (13, 400));
    t.setColour (juce::TextEditor::backgroundColourId, juce::Colour (246, 242, 236).withAlpha (0.05f));
    t.setColour (juce::TextEditor::outlineColourId, col::line2()); t.setColour (juce::TextEditor::focusedOutlineColourId, col::line());
    t.setColour (juce::TextEditor::textColourId, col::ink); t.setColour (juce::TextEditor::highlightColourId, col::rust.withAlpha (0.5f));
    t.setColour (juce::CaretComponent::caretColourId, col::ink);
    t.setTextToShowWhenEmpty (hint, col::dim); t.setIndents (S (6), 0); t.setJustification (juce::Justification::centredLeft);
}

PresetPanel::PresetPanel (HalfLifeProcessor& p) : ui::Plate ("Presets", true), proc (p)
{
    setInterceptsMouseClicks (true, true); alpha = 0.96f;
    styleField (search, "Search"); styleField (nameField, "Name for this sound");
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { if (! shown.empty()) pick (0); };
    search.onEscapeKey = [this] { setVisible (false); };
    nameField.onTextChange = [this] { updateSave(); };
    nameField.onReturnKey = [this] { save.triggerClick(); };
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setRowHeight (S (24)); list.setOutlineThickness (0);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, col::ink.withAlpha (0.25f));
    loadFile.setButtonText (juce::String (juce::CharPointer_UTF8 ("Load file\xe2\x80\xa6")));
    for (auto* b : { &save, &loadFile, &folder }) kind (*b, "ghost");
    save.onClick = [this]
    {
        const auto n = nameField.getText().trim();
        if (n.isEmpty()) { nameField.grabKeyboardFocus(); return; }
        proc.savePreset (n);
    };
    loadFile.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a Half Life preset", proc.presetFolder(), "*.hlpreset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            const auto err = proc.loadPresetFile (f);
            if (err.isNotEmpty() && onError) onError (err);
        });
    };
    folder.onClick = [this] { proc.presetFolder().startAsProcess(); };
    save.setTooltip ("Save the current sound as a preset (its samples go inside the file, so it can be shared)");
    loadFile.setTooltip ("Load a preset from anywhere, e.g. one someone sent you");
    folder.setTooltip ("Open the presets folder (Documents / Half Life / Presets)");
    for (juce::Component* c : std::initializer_list<juce::Component*> { &search, &list, &nameField, &save, &loadFile, &folder }) addAndMakeVisible (c);
}

void PresetPanel::resized()
{
    auto r = getLocalBounds().reduced (S (10)); r.removeFromTop (S (22));
    search.setBounds (r.removeFromTop (S (26))); r.removeFromTop (S (6));
    auto bottom = r.removeFromBottom (S (24)); r.removeFromBottom (S (6));
    auto row2 = r.removeFromBottom (S (26)); r.removeFromBottom (S (8));
    save.setBounds (row2.removeFromRight (S (70))); row2.removeFromRight (S (6)); nameField.setBounds (row2);
    folder.setBounds (bottom.removeFromRight (S (64))); bottom.removeFromRight (S (6)); loadFile.setBounds (bottom.removeFromRight (S (86)));
    list.setBounds (r);
}

void PresetPanel::rescan() { all = proc.listPresets(); filter(); updateSave(); }

void PresetPanel::showFor (const juce::String& cur)
{
    current = cur; nameField.setText (cur, false);
    rescan(); setVisible (true); toFront (false);
    search.grabKeyboardFocus();
}

void PresetPanel::filter()
{
    const auto q = search.getText().trim();
    shown.clear();
    for (auto& p : all) if (q.isEmpty() || p.name.containsIgnoreCase (q)) shown.push_back (p);
    list.updateContent(); list.repaint();
}

void PresetPanel::updateSave()
{
    const auto n = juce::File::createLegalFileName (nameField.getText().trim()).trim();
    bool exists = false; for (auto& p : all) if (p.factory < 0 && p.name.equalsIgnoreCase (n)) exists = true;
    save.setButtonText (exists ? "Replace" : "Save"); save.setEnabled (n.isNotEmpty());
}

void PresetPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& p = shown[(size_t) row]; const auto& L = Look::get();
    const bool cur = p.name == current;
    if (cur) { g.setColour (col::rust.withAlpha (0.22f)); g.fillRect (0, 0, w, h); g.setColour (col::rustHi); g.fillRect (0, 2, 2, h - 4); }
    else if (list.isMouseOver (true) && list.getRowContainingPosition (list.getMouseXYRelative().x, list.getMouseXYRelative().y) == row) { g.setColour (col::ink.withAlpha (0.06f)); g.fillRect (0, 0, w, h); }
    int right = w - S (8);
    if (p.factory >= 0) { g.setColour (col::dim); g.setFont (L.font (10.5f, 700).withExtraKerningFactor (0.12f)); g.drawText ("FACTORY", 0, 0, right, h, juce::Justification::centredRight); right -= S (64); }
    g.setColour (cur ? col::ink : col::bone); g.setFont (L.font (13, cur ? 700 : 400));
    g.drawFittedText (p.name, S (10), 0, right - S (10), h, juce::Justification::centredLeft, 1, 0.85f);
}

void PresetPanel::pick (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto p = shown[(size_t) row];
    const auto err = proc.loadPreset (p);
    if (err.isNotEmpty()) { if (onError) onError (p.name + ": " + err); return; }
    current = p.name; nameField.setText (p.name, false); updateSave(); list.repaint();
}

/* ======================= the editor ======================= */
static const juce::Colour LABEL (0xffd2cbc0);

HalfLifeEditor::HalfLifeEditor (HalfLifeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&Look::get());
    face.ed = this;
    addAndMakeVisible (face);
    auto add = [this] (juce::Component& c) { face.addAndMakeVisible (c); };
    auto knob = [this] (const char* id, bool mini = false, const juce::String& label = {}) { return std::make_unique<Knob> (proc, id, mini, label); };

    // the islands go in first, everything else sits on them
    for (auto* pl : { &headPlate, &srcPlate, &stripPlate, &takePlate }) add (*pl);
    add (bench);
    labels.ed = this; add (labels);

    // header
    kind (perish, "perish"); perish.setTooltip ("Randomize everything and make a new sound");
    perish.onClick = [this] { proc.perish(); };
    volume = knob ("volume", true, "Out"); add (*volume); add (perish);


    // 01 feed it: four sample rows, the pool, the generators
    style (poolInfo, 12.5f, 700, col::ink, juce::Justification::centredLeft); poolInfo.setMinimumHorizontalScale (0.8f); add (poolInfo);
    poolPick.setButtonText ("+ Folder"); poolFiles.setButtonText ("+ Files"); poolClear.setButtonText (U8 ("\xc3\x97"));
    for (auto* b : { &poolPick, &poolFiles }) { kind (*b, "ghost"); add (*b); }
    kind (poolClear, "x"); add (poolClear); poolClear.setTooltip ("Clear the pool");
    poolPick.setTooltip ("Add a folder (subfolders included). You can also drop a folder anywhere on the window.");
    poolPick.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample folder for the pool");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) proc.poolAdd ({ fc.getResult() }); });
    };
    poolFiles.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add sound files to the pool", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc) { if (fc.getResults().size()) proc.poolAdd (fc.getResults()); });
    };
    poolClear.onClick = [this] { proc.poolClear(); };
    for (int i = 0; i < 4; ++i) add (*slots.add (new SlotCard (proc, i)));
    freshSeg = std::make_unique<Seg> (proc, "poolFresh");
    fitK = knob ("poolFit", true); gateK = knob ("poolGate", true);
    for (juce::Component* c : std::initializer_list<juce::Component*> { freshSeg.get(), fitK.get(), gateK.get() }) add (*c);
    noiseType.addItemList ({ "White", "Pink", "Brown", "Crackle", "Velvet" }, 1);
    noiseRole.addItemList ({ "Texture", "Body", "Tail", "Transient" }, 1);
    noiseRole.setTooltip ("The role the noise layer plays");
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, "noiseType", noiseType));
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, "noiseRole", noiseRole));
    btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "noiseOn", noiseOn));
    btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "clickOn", clickOn));
    btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "blipOn", blipOn));
    for (auto* t : { &noiseOn, &clickOn, &blipOn }) t->getProperties().set ("chip", true);
    noiseLevel = knob ("noiseLevel", true, "Noise level");
    for (juce::Component* c : std::initializer_list<juce::Component*> { &noiseOn, &noiseType, &noiseRole, noiseLevel.get(), &clickOn, &blipOn }) add (*c);

    // 02 the sound: the waveform straight on the ink, then one strip of buttons and macros
    add (display);
    kind (trig, "trigger"); kind (fresh, "main"); kind (mutate, "main");
    trig.setTooltip ("Play the current sound"); fresh.setTooltip ("Generate a new sound"); mutate.setTooltip ("Evolve the current sound");
    trig.onClick = [this] { proc.trigger(); }; fresh.onClick = [this] { proc.fresh(); }; mutate.onClick = [this] { proc.mutate(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &trig, &fresh, &mutate }) add (*c);
    for (auto id : { "instability", "mutation", "glide", "rot", "rotOnset", "interact" }) add (*macroKnobs.add (new Knob (proc, id)));

    // 03 take it: note, drag, decide, history
    add (keybed); add (well);
    targetSeg = std::make_unique<Seg> (proc, "target");
    candK = knob ("cands", true);
    add (*targetSeg); add (*candK);
    add (history);

    // bench: tabs over one module at a time
    const char* tabNames[] = { "Shape", "Gate", "Resonator", "Distortion", "Pitch", "Amp", "Render", "Layers" };
    for (int i = 0; i < 8; ++i) { auto* b = tabs.add (new juce::TextButton (juce::String (tabNames[i]).toUpperCase())); b->onClick = [this, i] { showTab (i); }; add (*b); }
    kind (hoodBtn, "tab"); hoodBtn.setButtonText ("ENGINE"); hoodBtn.getProperties().set ("tri", "up"); hoodBtn.onClick = [this] { showHood (! hood.isVisible()); }; add (hoodBtn);
    auto hint = [this] (int t, const juce::String& text) { auto* l = hints.add (new juce::Label()); style (*l, 12 * BIG, 400, col::muted, juce::Justification::topLeft); l->setMinimumHorizontalScale (1.0f); l->setText (text, juce::dontSendNotification); hintTab.push_back (t); bench.addAndMakeVisible (l); };
    auto mk = [this] (int t, const char* id, const juce::String& dist = {}, const juce::String& label = {}) { auto* k = modKnobs.add (new Knob (proc, id, false, label)); k->setBig(); modKnobTab.push_back (t); modKnobDist.push_back (dist); bench.addAndMakeVisible (k); };
    hint (0, "The envelope the sound is built toward. Pitch is never touched. Follow 0 is off.");
    for (auto id : { "shpA", "shpD", "shpS", "shpR", "shpFollow" }) mk (0, id);
    bench.addAndMakeVisible (shapeView);
    hint (1, "Volume and brightness fall together, like a vactrol.");
    for (auto id : { "lpgResp", "body", "bodyTime", "reso", "bright" }) mk (1, id);
    hint (2, "Tuned resonators on the whole mix.");
    resType.addItemList ({ "Off", "Modal bank", U8 ("Comb \xc2\xb7 string") }, 1); resStruct.addItemList ({ "Harmonic", "Bar", "Bell", "Plate", "Random per sound" }, 1);
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, "resType", resType));
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, "resStruct", resStruct));
    for (auto* c : { &resType, &resStruct, &distType }) c->getProperties().set ("big", true);
    bench.addAndMakeVisible (resType); bench.addAndMakeVisible (resStruct);
    mk (2, "resAmt", {}, "Resonance"); mk (2, "resPitch"); mk (2, "resRand", {}, "Random tune"); mk (2, "resDamp"); mk (2, "resVoices"); mk (2, "resSpread"); mk (2, "resMix");
    distType.addItemList ({ "Off", "Saturation", "Wave shaper", "Folder", "Phase distortion" }, 1);
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, "distType", distType));
    bench.addAndMakeVisible (distType); bench.addAndMakeVisible (curve);
    mk (3, "distDrive", "common"); mk (3, "distTone", "common"); mk (3, "distMix", "common");
    mk (3, "satKnee", "sat", "Knee"); mk (3, "satAsym", "sat", "Asymmetry");
    mk (3, "shCurve", "shape", "Curve"); mk (3, "shH2", "shape", "2nd harm."); mk (3, "shH3", "shape", "3rd harm."); mk (3, "shH5", "shape", "5th harm."); mk (3, "shSteps", "shape");
    mk (3, "fdFolds", "fold"); mk (3, "fdBias", "fold", "Symmetry"); mk (3, "fdShape", "fold", "Tri to sine");
    mk (3, "pdDepth", "phase", "Depth"); mk (3, "pdSmooth", "phase", "Mod bright"); mk (3, "pdFb", "phase");
    hint (4, "Baked in. Positive starts high and drops.");
    for (auto id : { "pitchAmt", "pitchTime", "pitchCurve" }) mk (4, id);
    hint (5, "How keys and MIDI play it. Baked into the drag.");
    for (auto id : { "attack", "decay", "sustain", "release" }) mk (5, id);
    hint (6, "Threshold is relative to each sample's peak.");
    for (auto id : { "maxLen", "threshold", "scatter" }) mk (6, id);
    for (auto* k : modKnobs) k->slider.onDragEnd = [this] { curve.repaint(); };
    distType.onChange = [this] { layoutBench(); curve.repaint(); };
    resType.onChange = [this] { resStruct.setEnabled (resType.getSelectedId() == 2); };

    // links: a tab in the bottom panel
    // interaction: one collapsed line in the header; click it and the links open under it
    kind (interBtn, "clear"); add (interBtn);
    interBtn.onClick = [this] { interPanel.setVisible (! interPanel.isVisible()); if (interPanel.isVisible()) { presetPanel.setVisible (false); interPanel.toFront (false); } face.repaint(); };
    add (interPanel); interPanel.setVisible (false); interPanel.alpha = 0.95f; interPanel.title = "Interaction";
    graph.setTooltip ("Who pushes whom, left to right. Solid arcs above: loudness drives a parameter of another source. Dashed arcs below: hits re-strike another's gate.");
    btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "lockLinks", lockLinks));
    lockLinks.setButtonText ("Lock");
    lockLinks.setTooltip ("Keep these links when Fresh and Mutate make new sounds");
    kind (reroll, "ghost"); reroll.setButtonText ("Reroll"); reroll.onClick = [this] { proc.relink(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &graph, &lockLinks, &reroll }) interPanel.addAndMakeVisible (c);
    for (int i = 0; i < 8; ++i) { auto* l = linkRows.add (new juce::Label()); style (*l, 11.5f, 400, col::muted); interPanel.addAndMakeVisible (l); }

    // presets: arrow, the name (opens the browser), arrow
    presetPrev.getProperties().set ("dir", "left"); kind (presetPrev, "nav"); kind (presetNext, "nav"); kind (presetBtn, "clear");
    for (auto* b : { &presetPrev, &presetBtn, &presetNext }) add (*b);
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    presetBtn.onClick = [this]
    {
        if (presetPanel.isVisible()) presetPanel.setVisible (false);
        else { interPanel.setVisible (false); presetPanel.showFor (u ? u->preset : juce::String()); }
        face.repaint();
    };
    add (presetPanel); presetPanel.setVisible (false);
    presetPanel.onError = [this] (const juce::String& e) { showError (e); };
    info (presetBtn, "Presets|Click to open the list: search, load, save. A preset carries its samples inside it, so you can send it to someone who doesn't have them.");
    info (presetPrev, "Previous preset|Loads the one before it in the list."); info (presetNext, "Next preset|Loads the next one in the list.");

    // under the hood: an overlay over the working area
    add (hood); hood.setVisible (false); hood.alpha = 0.96f;
    kind (hoodClose, "ghost"); kind (rerouteChaos, "ghost");
    hoodClose.onClick = [this] { showHood (false); };
    rerouteChaos.onClick = [this] { proc.rerouteChaos(); };
    for (auto* l : { &chaosL, &driftL, &glideL, &rotL, &slotGlideL }) style (*l, 11, 700, LABEL);
    chaosL.setText (U8 ("CHAOS \xc2\xb7 FOUR COUPLED CHUA CIRCUITS"), juce::dontSendNotification);
    style (hoodHint, 12, 400, col::muted, juce::Justification::topLeft);
    hoodHint.setText ("These wander on their own all the time. Fresh and Mutate capture their position at that moment; Instability sets how hard they push. Glide sets how far each slot's LFO pushes its routed parameters.", juce::dontSendNotification);
    driftL.setText ("GLOBAL DRIFT", juce::dontSendNotification);
    glideL.setText ("SLOT GLIDE LFOS", juce::dontSendNotification);
    rotL.setText (U8 ("ROT RECIPE \xc2\xb7 DEPTH AT FULL AGE"), juce::dontSendNotification);
    slotGlideL.setText ("SLOT GLIDE ROUTES", juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &hoodClose, &rerouteChaos, &chaosView, &drift, &glides, &rotBars, &hoodHint, &driftL, &glideL, &rotL, &chaosL, &slotGlideL }) hood.addAndMakeVisible (c);
    for (auto id : { "chaosSpeed", "chaosTurb", "chaosCouple" }) hood.addAndMakeVisible (chaosKnobs.add (new Knob (proc, id)));
    for (int i = 0; i < 4; ++i)
    {
        auto* l = glideTexts.add (new juce::Label()); style (*l, 12, 400, col::ink); hood.addAndMakeVisible (l);
        auto* b = glideReroute.add (new juce::TextButton ("Reroute")); kind (*b, "ghost"); b->onClick = [this, i] { proc.rerouteGlide (i); }; hood.addAndMakeVisible (b);
    }

    // what each control does, for the info line
    info (display, "Waveform|The current sound. Drag it into your DAW: a WAV at the last note you played, with the amp envelope.");
    info (trig, "Trigger|Play the current sound.");
    info (fresh, "Fresh|A new sound from what's loaded. With a pool, it can swap samples first (see Fresh draws).");
    info (mutate, "Mutate|A variation of the current sound. Mutation sets how far it moves.");
    info (perish, "Perish|Rerolls everything: knobs, roles, generators, and new samples from the pool. Pinned slots stay.");
    info (poolInfo, "Sample pool|Drop a folder anywhere on the window. Perish and Fresh draw samples from it.");
    info (well, "Drag out|Drag this into your DAW: a WAV at the last note you played, with the amp envelope. One file per sound, never duplicated.");
    info (*fitK, "Fit|How a sample from the pool is cut to fit its slot."); info (*gateK, "Gate|Quieter parts of pool samples are skipped.");
    info (poolPick, "Folder|Add a folder of samples to the pool (subfolders too). You can also drop a folder anywhere.");
    info (poolFiles, "Files|Add single files to the pool."); info (poolClear, "Clear|Empty the pool.");

    info (noiseOn, "Noise|A noise layer. Pick its colour and role."); info (noiseType, "Noise colour|White, pink, brown, crackle or velvet.");
    info (noiseRole, "Noise role|What the noise layer plays: texture, body, tail or transient.");
    info (clickOn, "Click|A short impulse at the start, for attack."); info (blipOn, "Blip|A short FM tone, for body.");

    info (interPanel, "Interaction|How layers push each other: one's loudness moves another's parameter, or its hits re-strike another's gate.");
    info (lockLinks, "Lock|Keep these links when Fresh and Mutate make new sounds."); info (reroll, "Reroll|New links between the layers, same sound otherwise.");
    info (*freshSeg, "Fresh draws|What Fresh does with the pool: keep the samples, swap one, or swap all.");
    info (*targetSeg, "Decide by|Fresh and Mutate render several versions and keep the one that fits best: punchy, long, bright, dark, or wrong.");
    info (keybed, "Keys|Play the sound pitched. The last key you play sets the note of the dragged file.");
    info (history, "History|The last sounds. Click one to go back to it.");
    for (auto* t : tabs) info (*t, "Modules|The sound's processing, one at a time.");
    info (*tabs[7], "Layers|Every source in this sound and its role. Lock keeps a layer through Fresh and Mutate.");
    info (interBtn, "Interaction|How layers push each other: one's loudness moves another's parameter, or its hits re-strike another's gate. Click to open.");
    info (hoodBtn, "Engine|Under the hood: the chaos that drives everything hidden, and this sound's rot recipe.");
    info (resType, "Resonator type|Modal bank (struck objects) or comb (strings)."); info (resStruct, "Structure|The pattern of partials the modal bank rings at.");
    info (distType, "Distortion type|Saturation, wave shaper, folder or phase distortion."); info (curve, "Curve|What the distortion does to the signal.");
    info (rerouteChaos, "Reroute chaos|New routing from the chaos circuits to the hidden parameters.");

    // the info line's switch, kept with the project
    infoToggle.setButtonText ("?"); info (infoToggle, "Hints|Turn these explanations on or off.");
    auto setInfoOn = [this] (bool on) { infoToggle.getProperties().set ("on", on); kind (infoToggle, on ? "itog" : "itogOff"); face.repaint (rInfo); };
    setInfoOn ((bool) proc.apvts.state.getProperty ("infoLine", true));
    infoToggle.onClick = [this, setInfoOn] { const bool on = ! (bool) infoToggle.getProperties()["on"]; proc.apvts.state.setProperty ("infoLine", on, nullptr); setInfoOn (on); };
    add (infoToggle);
    // About: what the plugin is, in one breath
    aboutBtn.setButtonText ("i"); kind (aboutBtn, "itog"); info (aboutBtn, "About|What Half Life is and how to use it.");
    about.onClose = [this] { kind (aboutBtn, "itog"); };
    aboutBtn.onClick = [this] { about.setVisible (! about.isVisible()); if (about.isVisible()) about.toFront (false); kind (aboutBtn, about.isVisible() ? "itogOn" : "itog"); };
    add (aboutBtn); add (about); about.setVisible (false); about.alpha = 0.97f;

    proc.addChangeListener (this);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (false);
    gl.attachTo (*this);

    // text keeps its size; resizing changes how the space is shared
    setResizable (true, true);
    setResizeLimits (1280, 860, 2560, 1600);
    setSize (1440, 900);
    showTab (0);
    resType.onChange();
    refresh();
    startTimerHz (30);
}

HalfLifeEditor::~HalfLifeEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    gl.detach();
    setLookAndFeel (nullptr);
}

void HalfLifeEditor::paint (juce::Graphics& g) { if (! glOk.load()) g.fillAll (juce::Colours::black); }

void HalfLifeEditor::resized()
{
    face.setBounds (getLocalBounds()); labels.setBounds (getLocalBounds());
    faceW = getWidth(); faceH = getHeight();
    layoutFace();
}

/* Real pixels, no scaling: text keeps its size and only the sharing of space changes.
   Islands sit on the ink and are only as tall as what they hold; the ink shows in every gap.
   header ─ [01 feed it] [02 the sound, the strip] [03 take it] ─ modules */
static const int P = 12, G = 10, IN = 16, SIDE = 360, HEAD = S (40), TABH = 46, BENCH = 178, STRIP = 12 + S (34) + 8 + 104 + 12, LCOL = S (200);

void HalfLifeEditor::layoutFace()
{
    const int W = getWidth(), H = getHeight();
    // header
    headPlate.setBounds (P, P, W - 2 * P, HEAD);
    const int logoW = (int) juce::GlyphArrangement::getStringWidth (Look::get().font (16, 900).withExtraKerningFactor (0.32f), "HALF LIFE") + 4;
    const int hx = P + IN + logoW + 24;
    interBtn.setBounds (hx, P + (HEAD - S (28)) / 2, S (190), S (28));
    const bool narrow = W < 1400;   // small windows: a narrower Perish, the ? without the word "Hints"
    perish.setBounds (W - P - S (narrow ? 124 : 150), P, S (narrow ? 124 : 150), HEAD);
    volume->setBounds (perish.getX() - 8 - S (104), P + (HEAD - S (32)) / 2, S (104), S (32));
    infoToggle.setBounds (volume->getX() - S (18) - S (18), P + (HEAD - S (18)) / 2, S (18), S (18));
    rHintsLabel = narrow ? juce::Rectangle<int> (infoToggle.getX(), P, 0, HEAD) : juce::Rectangle<int> (infoToggle.getX() - S (44), P, S (40), HEAD);
    {   // the preset bar, right of Interaction: as wide as the header allows, up to a point
        const int y = P + (HEAD - S (28)) / 2, x0 = interBtn.getRight() + S (12), limit = rHintsLabel.getX() - S (14);
        const int pw = juce::jlimit (S (100), S (200), limit - x0 - 2 * S (24) - 4);
        presetPrev.setBounds (x0, y, S (24), S (28));
        presetBtn.setBounds (presetPrev.getRight() + 2, y, pw, S (28));
        presetNext.setBounds (presetBtn.getRight() + 2, y, S (24), S (28));
        presetPanel.setBounds (presetPrev.getX() - 8, P + HEAD + 4, S (330), juce::jmin (S (380), H - P - HEAD - 20 - P));
    }
    rToast = { presetNext.getRight() + 18, P, juce::jmax (0, rHintsLabel.getX() - 16 - (presetNext.getRight() + 18)), HEAD };
    toastInReadout = rToast.getWidth() < S (170);   // no room left in the header: messages go on the readout line
    interPanel.setBounds (hx - 8, P + HEAD + 4, S (560), S (230));
    layoutInter();

    const int body0 = P + HEAD + G;
    rBench = { P, H - P - BENCH, W - 2 * P, BENCH };
    about.setBounds (P, rBench.getY() - 4 - S (236), S (400), S (236));   // opens upward from the i in the lower left
    const int bodyBottom = rBench.getY() - G;

    // 01 feed it
    {
        const int x = P + IN, iw = SIDE - 2 * IN; int y = body0 + 12;
        rSrcHead = { x, y, iw, S (20) }; y += S (24);
        const int rowH = 10 + S (18) + S (20) + 2;
        for (int i = 0; i < 4; ++i) { slots[i]->setBounds (x, y, iw, rowH); y += rowH; }
        y += 6;
        // the pool, in one row: how many files, and the ways to add more
        const bool clr = u && u->poolFiles > 0;
        int bx = x + iw;
        poolClear.setVisible (clr); if (clr) { bx -= S (16); poolClear.setBounds (bx, y, S (16), S (22)); bx -= 4; }
        bx -= S (48); poolFiles.setBounds (bx, y, S (48), S (22)); bx -= 4;
        bx -= S (56); poolPick.setBounds (bx, y, S (56), S (22));
        const int pw = (int) juce::GlyphArrangement::getStringWidth (Look::get().font (11, 700).withExtraKerningFactor (0.14f), "POOL") + 4;
        rPoolHead = { x, y, pw, S (22) };
        poolInfo.setBounds (x + pw + 8, y, bx - 6 - (x + pw + 8), S (22)); y += S (22) + 8;
        freshSeg->setBounds (x, y, iw, S (22)); y += S (22) + 6;
        fitK->setBounds (x, y, iw / 2 - 2, S (30)); gateK->setBounds (x + iw / 2 + 2, y, iw / 2 - 2, S (30)); y += S (30) + 10;
        rGensHead = { x, y, iw, S (16) }; y += S (16) + 6;
        const int cw = (iw - 8) / 3;
        noiseOn.setBounds (x, y, cw, S (24)); clickOn.setBounds (x + cw + 4, y, cw, S (24)); blipOn.setBounds (x + 2 * (cw + 4), y, iw - 2 * (cw + 4), S (24)); y += S (24) + 6;
        const int lw = S (26), ww = (iw - lw - 12) / 2;
        noiseType.setBounds (x, y, ww, S (22)); noiseRole.setBounds (x + ww + 4, y, ww, S (22)); noiseLevel->setBounds (x + iw - lw, y - 2, lw, lw); y += S (22);
        srcPlate.setBounds (P, body0, SIDE, y + 12 - body0);
    }
    // 03 take it
    {
        const int px = W - P - SIDE, x = px + IN, iw = SIDE - 2 * IN; int y = body0 + 12;
        rTakeHead = { x, y, iw, S (20) };
        keybed.setBounds (x, y, iw, S (20) + 6 + S (42)); y += S (20) + 6 + S (42) + 10;
        well.setBounds (x, y, iw, S (70)); y += S (70) + 10;
        rDecideHead = { x, y, iw, S (14) }; y += S (14) + 6;
        const int rows = targetSeg->rowsNeeded (iw);
        targetSeg->setBounds (x, y, iw, rows * (S (22) + 2)); y += rows * (S (22) + 2) + 2;
        candK->setBounds (x, y, iw, S (30)); y += S (30) + 10;
        history.setBounds (x, y, iw, S (18) + S (30)); y += S (18) + S (30);
        takePlate.setBounds (px, body0, SIDE, y + 12 - body0);
    }
    // 02 the sound
    {
        const int cx = P + SIDE + G, cw = W - P - SIDE - G - cx;
        const int sy = bodyBottom - STRIP;
        stripPlate.setBounds (cx, sy, cw, STRIP);
        const int iw = cw - 24, bh = S (34), t = juce::roundToInt ((iw - 12) * 0.5), f = (iw - 12 - t) / 2;
        trig.setBounds (cx + 12, sy + 12, t, bh); fresh.setBounds (cx + 12 + t + 6, sy + 12, f, bh); mutate.setBounds (cx + 12 + t + 6 + f + 6, sy + 12, iw - t - f - 12, bh);
        const float kw = iw / 6.0f; const int ky = sy + 12 + bh + 8;
        for (int i = 0; i < 6; ++i) macroKnobs[i]->setBounds (cx + 12 + juce::roundToInt (i * kw), ky, juce::roundToInt (kw), sy + STRIP - 10 - ky);
        rReadout = { cx, body0, cw, S (40) };
        if (toastInReadout) rToast = { cx, body0 + S (12), cw, S (22) };
        display.setBounds (cx, body0 + S (44), cw, juce::jmax (40, sy - G - (body0 + S (44))));
    }
    // modules
    bench.setBounds (rBench);
    {
        const auto tf = Look::get().font (12.5f * BIG, 700).withExtraKerningFactor (0.1f); int x = P + IN;
        aboutBtn.setBounds (x, rBench.getY() + (TABH - S (18)) / 2, S (18), S (18)); x += S (18) + 12;
        for (int i = 0; i < tabs.size(); ++i) { const int w = (int) juce::GlyphArrangement::getStringWidth (tf, tabs[i]->getButtonText()) + 20; tabs[i]->setBounds (x, rBench.getY(), w, TABH); x += w + 2; }
        const int hw = (int) juce::GlyphArrangement::getStringWidth (tf, hoodBtn.getButtonText()) + 34;
        hoodBtn.setBounds (x, rBench.getY(), hw, TABH);
    }
    layoutBench();
    // under the hood: over everything below the header
    {
        hood.setBounds (P, body0, W - 2 * P, rBench.getBottom() - body0);
        const int hw = hood.getWidth(), hhh = hood.getHeight(), colW = (hw - 48) / 2, rx = 24 + colW + 16, rh = S (18);
        hoodClose.setBounds (hw - S (80), 6, S (70), S (22));
        chaosL.setBounds (16, S (30), colW, S (14));
        const int kH = S (80), gl = 4 * S (26);
        const int cvH = juce::jmax (80, hhh - S (48) - 4 - kH - 8 - S (20) - gl - 16);
        chaosView.setBounds (16, S (48), colW, cvH);
        int yy = S (48) + cvH + 4;
        for (int i = 0; i < 3; ++i) chaosKnobs[i]->setBounds (16 + i * S (90), yy, S (86), kH);
        rerouteChaos.setBounds (16 + 3 * S (90) + 10, yy + kH / 3, S (120), S (24));
        yy += kH + 8;
        slotGlideL.setBounds (16, yy, colW, S (14)); yy += S (20);
        for (int i = 0; i < 4; ++i) { glideTexts[i]->setBounds (16, yy + i * S (26), colW - S (84), S (22)); glideReroute[i]->setBounds (16 + colW - S (76), yy + i * S (26), S (76), S (22)); }
        int ry = S (30);
        hoodHint.setBounds (rx, ry, colW - S (90), S (60)); ry += S (60) + 6;
        driftL.setBounds (rx, ry, colW, S (14)); ry += S (18); drift.setBounds (rx, ry, colW, 8 * rh); ry += 8 * rh + 8;
        glideL.setBounds (rx, ry, colW, S (14)); ry += S (18); glides.setBounds (rx, ry, colW, 4 * rh); ry += 4 * rh + 8;
        rotL.setBounds (rx, ry, colW, S (14)); ry += S (18); rotBars.setBounds (rx, ry, colW, 9 * rh);
    }
    face.repaint();
}

/* inside the modules panel: a text column, then the knobs; Layers fills the panel itself. The hint text sits at the
   right when the knobs leave room for it, otherwise it moves into the header. */
void HalfLifeEditor::layoutBench()
{
    const int dist = distType.getSelectedId() - 1;
    const char* dn[] = { "off", "sat", "shape", "fold", "phase" };
    const int top = TABH + 8, left = 300, KW = 136, kh = BENCH - top - 6;
    int count = 0;
    for (int i = 0; i < modKnobs.size(); ++i)
    {
        bool vis = modKnobTab[(size_t) i] == tab;
        if (vis && modKnobTab[(size_t) i] == 3) { const auto& d = modKnobDist[(size_t) i]; vis = dist > 0 && (d == "common" || d == dn[juce::jmax (0, dist)]); }
        modKnobs[i]->setVisible (vis);
        if (vis) { modKnobs[i]->setBounds (left + 16 + count * KW, top - 2, KW - 4, kh); count++; }
    }
    // the hint text: in the panel if there's room after the knobs, else in the header
    {
        const int used = tab == 7 ? left + 3 * LCOL : left + 16 + count * KW;
        const int room = rBench.getWidth() - used - IN - 24;
        rInfo = room >= 300 ? juce::Rectangle<int> (rBench.getRight() - IN - juce::jmin (S (240), room), rBench.getY() + top, juce::jmin (S (240), room), BENCH - top - 10)
                            : rToast;
    }
    shapeView.setVisible (tab == 0); shapeView.setBounds (IN, top - 2, left - IN, BENCH - top - 8);
    for (int i = 0; i < hints.size(); ++i) { hints[i]->setVisible (hintTab[(size_t) i] == tab && tab != 0); hints[i]->setBounds (IN, top, left - IN, tab == 2 ? 62 : BENCH - top - 10); }
    resType.setVisible (tab == 2); resStruct.setVisible (tab == 2);
    resType.setBounds (IN, top + 66, 136, 36); resStruct.setBounds (IN + 140, top + 66, left - IN - 140, 36);
    distType.setVisible (tab == 3); curve.setVisible (tab == 3);
    distType.setBounds (IN, top, left - IN, 36); curve.setBounds (IN, top + 42, left - IN, BENCH - top - 50);
    const int right = (rInfo.getY() > rBench.getY() ? rInfo.getX() - 20 : rBench.getRight() - IN);
    rLayers = { rBench.getX() + IN, rBench.getY() + top, right - (rBench.getX() + IN), BENCH - top - 8 };
    layoutLocks();
    face.repaint();
}

/* the Lock buttons of the Layers tab, beside their rows */
void HalfLifeEditor::layoutLocks()
{
    if (! u) return;
    for (int i = 0; i < lockBtns.size(); ++i)
    {
        auto* b = lockBtns[i];
        const int rh = S (22), rows = juce::jmax (1, rLayers.getHeight() / rh), c = i / rows, x = rLayers.getX() + c * (LCOL + 16);
        const bool fits = tab == 7 && x + LCOL <= rLayers.getRight() + 4;
        if (i >= (int) u->layers.size() || hood.isVisible() || ! fits) { b->setVisible (false); continue; }
        const auto& l = u->layers[(size_t) i];
        b->setVisible (true); b->setButtonText (l.locked ? "Locked" : "Lock"); kind (*b, l.locked ? "lockOn" : "lock");
        b->setBounds (x + LCOL - S (50), rLayers.getY() + (i % rows) * rh + 2, S (50), rh - 4);
        b->getProperties().set ("info", l.name + "|" + l.params + ". Lock keeps this layer through Fresh and Mutate.");
        const int key = l.key; b->onClick = [this, key] { proc.toggleLock (key); };
        b->toBehind (&hood);
    }
}

/* the opened Interaction menu: Lock and Reroll, the graph, the links in words */
void HalfLifeEditor::layoutInter()
{
    const int iw = interPanel.getWidth() - 24, ih = interPanel.getHeight() - S (34) - 12;
    lockLinks.setBounds (12, S (34), S (80), S (22)); reroll.setBounds (12, S (34) + S (28), S (80), S (22));
    const int gx = 12 + S (80) + 12, gw = S (230);
    graph.setBounds (gx, S (34), gw, ih);
    const int lx = gx + gw + 14;
    for (int i = 0; i < linkRows.size(); ++i) { linkRows[i]->setVisible (true); linkRows[i]->setBounds (lx, S (34) + i * S (18), iw + 12 - lx, S (18)); }
}

void HalfLifeEditor::showTab (int i)
{
    tab = i;
    for (int k = 0; k < tabs.size(); ++k) kind (*tabs[k], k == i ? "tabOn" : "tab");
    layoutBench(); refresh();
}

void HalfLifeEditor::showHood (bool on)
{
    hood.setVisible (on); if (on) { hood.toFront (false); interPanel.setVisible (false); }
    hoodBtn.getProperties().set ("tri", on ? "down" : "up"); kind (hoodBtn, on ? "tabOn" : "tab");
    layoutBench(); refresh();
}

/* painted on the face: logo, the sound's name between the arrows, the readout over the waveform, section heads */
void HalfLifeEditor::paintFace (juce::Graphics& g)
{
    const auto& L = Look::get();
    g.setColour (col::bone); g.setFont (L.font (16, 900).withExtraKerningFactor (0.32f));
    g.drawText ("HALF LIFE", P + IN, P, S (200), HEAD, juce::Justification::centredLeft);
    g.setColour (LABEL); g.setFont (L.font (12, 400)); g.drawText ("Hints", rHintsLabel, juce::Justification::centredRight);
    auto head = [&] (juce::Rectangle<int> r, const juce::String& num, const juce::String& name, const juce::String& right)
    {
        int x = r.getX();
        if (num.isNotEmpty()) { g.setColour (col::rustHi); g.setFont (L.font (17, 900)); g.drawText (num, x, r.getY(), S (30), r.getHeight(), juce::Justification::centredLeft); x += (int) juce::GlyphArrangement::getStringWidth (L.font (17, 900), num) + 8; }
        g.setColour (num.isNotEmpty() ? col::ink : LABEL); g.setFont (L.font (num.isNotEmpty() ? 12 : 11, 700).withExtraKerningFactor (0.14f));
        g.drawText (name, x, r.getY(), r.getRight() - x, r.getHeight(), juce::Justification::centredLeft);
        if (right.isNotEmpty()) { g.setColour (LABEL); g.setFont (L.font (11.5f, 400)); g.drawText (right, r, juce::Justification::centredRight); }
    };
    head (rSrcHead, "01", "FEED IT", "drop audio on a row");
    head (rPoolHead, {}, "POOL", {});
    head (rGensHead, {}, "GENERATORS", "made, not loaded");
    head (rTakeHead, "03", "TAKE IT", {});
    head (rDecideHead, {}, "DECIDE BY", {});
    if (! u) return;
    // interaction, collapsed: label, how many links, the strongest one; a triangle that says it opens
    {
        const auto r = interBtn.getBounds(); const bool open = interPanel.isVisible();
        g.setColour (col::ink.withAlpha (interBtn.isMouseOver() || open ? 0.1f : 0.05f)); g.fillRoundedRectangle (r.toFloat(), 3);
        auto t = r.reduced (10, 0);
        g.setColour (LABEL); g.setFont (L.font (11, 700).withExtraKerningFactor (0.14f)); g.drawText ("INTERACTION", t.removeFromLeft (S (92)), juce::Justification::centredLeft);
        juce::Path tri; const float tx = (float) t.getRight() - 5, ty = (float) t.getCentreY();
        if (open) tri.addTriangle (tx - 5, ty - 2, tx + 5, ty - 2, tx, ty + 4); else tri.addTriangle (tx - 2, ty - 5, tx - 2, ty + 5, tx + 4, ty);
        g.setColour (col::muted); g.fillPath (tri); t.removeFromRight (S (16));
        const int n = (int) u->links.size();
        g.setColour (col::ink); g.setFont (L.font (12.5f, 700));
        const juce::String cnt = n == 0 ? juce::String ("no links") : juce::String (n) + (n == 1 ? " link" : " links");
        g.drawText (cnt, t, juce::Justification::centredLeft);
    }
    // the preset bar: the name between the arrows, a star when the sound has changed since
    {
        const auto r = presetBtn.getBounds(); const bool open = presetPanel.isVisible();
        g.setColour (col::ink.withAlpha (presetBtn.isMouseOver() || open ? 0.1f : 0.05f)); g.fillRoundedRectangle (r.toFloat(), 3);
        auto t = r.reduced (S (8), 0);
        juce::Path tri; const float tx = (float) t.getRight() - 5, ty = (float) t.getCentreY();
        if (open) tri.addTriangle (tx - 5, ty - 2, tx + 5, ty - 2, tx, ty + 4); else tri.addTriangle (tx - 5, ty - 2, tx + 5, ty - 2, tx, ty + 4);
        g.setColour (col::muted); g.fillPath (tri); t.removeFromRight (S (14));
        const bool none = u->preset.isEmpty();
        g.setColour (none ? LABEL : col::ink); g.setFont (L.font (12.5f, none ? 400 : 700));
        g.drawFittedText (none ? juce::String ("No preset") : u->preset + (u->presetEdited || proc.presetIsDirty() ? " *" : ""), t, juce::Justification::centredLeft, 1, 0.8f);
    }
    // 02: the readout over the sound
    {
        const auto o = u->out; int x = rReadout.getX() + 2; const int by = rReadout.getY() + S (32);
        g.setColour (col::rustHi); g.setFont (L.font (17, 900)); g.drawText ("02", x, by - S (20), S (30), S (20), juce::Justification::bottomLeft); x += S (30);
        const juce::String th = o && o->thalf >= 0 ? juce::String (juce::roundToInt (o->thalf)) + " ms" : U8 ("\xe2\x80\x94");
        g.setColour (col::ink); g.setFont (L.font (32, 900)); g.drawText (th, x, by - S (34), S (200), S (36), juce::Justification::bottomLeft);
        x += (int) juce::GlyphArrangement::getStringWidth (L.font (32, 900), th) + 12;
        g.setColour (col::muted); g.setFont (L.font (12.5f, 400));
        const auto rd = "half-life" + DOT + (o ? juce::String (o->len, 2) + " s" : juce::String ("-")) + DOT + (o ? juce::String (o->layers) : juce::String ("-")) + " layers";
        g.drawText (rd, x, by - S (18), rReadout.getRight() - x, S (16), juce::Justification::bottomLeft);
        readoutEnd = x + (int) juce::GlyphArrangement::getStringWidth (L.font (12.5f, 400), rd);
    }
}

/* drawn over everything: the layer list (Layers tab), the hint text, the status line */
void HalfLifeEditor::paintFaceOver (juce::Graphics& g)
{
    const auto& L = Look::get();
    if (! u) return;
    if (! hood.isVisible() && tab == 7)
    {
        const int rh = S (22), rows = juce::jmax (1, rLayers.getHeight() / rh), colW = LCOL;
        if (u->layers.empty()) { g.setColour (col::muted); g.setFont (L.font (12.5f, 400)); g.drawText ("No layers yet.", rLayers, juce::Justification::topLeft); }
        for (size_t i = 0; i < u->layers.size(); ++i)
        {
            const int c = (int) i / rows, rr = (int) i % rows; const int x = rLayers.getX() + c * (colW + 16), y = rLayers.getY() + rr * rh;
            if (x + colW > rLayers.getRight() + 4) break;
            const auto& l = u->layers[i];
            const float a = l.skipped ? 0.5f : 1.0f;
            g.setColour (col::layer (l.key).withAlpha (a * 0.9f)); g.fillRect (x, y + rh / 4, 2, rh / 2);
            const int nw = colW - 10 - S (54);
            const auto nf = L.font (12, 700); const int tw = juce::jmin (nw - 30, (int) juce::GlyphArrangement::getStringWidth (nf, l.name) + 1);
            g.setColour (col::ink.withAlpha (a)); g.setFont (nf);
            g.drawFittedText (l.name, x + 8, y, tw, rh, juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (LABEL.withAlpha (a)); g.setFont (L.font (11, 700).withExtraKerningFactor (0.06f));
            g.drawText (l.role.toUpperCase(), x + 8 + tw + 6, y, nw - tw - 6, rh, juce::Justification::centredLeft, true);
        }
    }
    const double age = juce::Time::getMillisecondCounterHiRes() - toastAt;
    const bool toastOn = toast.isNotEmpty() && age < 4000 && rToast.getWidth() > 60;
    const bool infoInHeader = rInfo == rToast;
    auto rT = rToast; if (toastInReadout) rT.setLeft (juce::jmax (rT.getX(), readoutEnd + S (18)));   // right of the readout's text
    if (! hood.isVisible() && ! (infoInHeader && toastOn)) { const auto keep = rInfo; if (infoInHeader) rInfo = rT; paintInfo (g); rInfo = keep; }
    // the status line, or the one-line description, in the header
    if (toastOn)
    {
        const float a = (float) juce::jlimit (0.0, 1.0, (4000 - age) / 400);
        g.setColour (col::muted.withAlpha (a)); g.setFont (L.font (12.5f, 400));
        g.drawFittedText (toast, rT, toastInReadout ? juce::Justification::centredRight : juce::Justification::centredLeft, 1, 0.85f);
    }
    else if (! toastInReadout && ! (infoInHeader && infoText.isNotEmpty() && (bool) infoToggle.getProperties()["on"]))
    {
        g.setColour (LABEL); g.setFont (L.font (12.5f, 400));
        g.drawText ("an endless percussive engine", rToast, juce::Justification::centredLeft, true);
    }
}

void HalfLifeEditor::refresh()
{
    const bool relayout = ! u || ! proc.ui() || u->layers.size() != proc.ui()->layers.size() || u->links.size() != proc.ui()->links.size() || (u->poolFiles > 0) != (proc.ui()->poolFiles > 0);
    const auto oldPreset = u ? u->preset : juce::String();
    u = proc.ui(); if (! u) return;
    if (u->preset != oldPreset && presetPanel.isVisible()) { presetPanel.current = u->preset; presetPanel.rescan(); }
    face.repaint (presetBtn.getBounds());
    for (int i = 0; i < 4; ++i) slots[i]->update (u->slots[(size_t) i]);
    poolInfo.setText (u->poolFiles > 0 ? juce::String (u->poolFiles) + (u->poolFiles == 1 ? " file" : " files") : juce::String ("no files"), juce::dontSendNotification);
    info (poolInfo, "Sample pool|" + u->poolInfo + (u->poolFiles > 0 ? ". Perish and Fresh draw samples from it." : ""));
    well.u = u; well.repaint();
    if (relayout) layoutFace();
    // lock buttons, one per layer row
    while (lockBtns.size() < (int) u->layers.size()) { auto* b = lockBtns.add (new juce::TextButton ("Lock")); face.addAndMakeVisible (b); }
    layoutLocks();
    history.update (*u);
    graph.u = u; graph.interact = proc.apvts.getRawParameterValue ("interact")->load(); graph.repaint();
    for (int i = 0; i < linkRows.size(); ++i)
    {
        if (i < (int) u->links.size()) { linkRows[i]->setText (u->links[(size_t) i].label + "  " + u->links[(size_t) i].text, juce::dontSendNotification); linkRows[i]->setTooltip (u->links[(size_t) i].text); }
        else linkRows[i]->setText (i == 0 && u->links.empty() ? "No links. Raise Interaction, then Fresh." : "", juce::dontSendNotification);
    }
    for (int i = 0; i < 4; ++i) glideTexts[i]->setText (juce::String (hl::keyName (i)) + DOT + u->slots[(size_t) i].glideKind + "   " + u->slots[(size_t) i].glideRoutes, juce::dontSendNotification);
    rotBars.rows.clear();
    const float rot = proc.apvts.getRawParameterValue ("rot")->load();
    for (int k = 0; k < hl::NROT; ++k) rotBars.rows.push_back ({ hl::ROTLABEL[k], (float) (u->rot[(size_t) k] * rot), false, col::rustHi });
    rotBars.repaint();
    face.repaint();
}

void HalfLifeEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (proc.ui() != u) refresh();
    if (proc.statusSerial() != toastSerial) { toastSerial = proc.statusSerial(); toast = proc.statusText(); toastAt = juce::Time::getMillisecondCounterHiRes(); face.repaint (rToast); }
    display.repaint();
}

void HalfLifeEditor::timerCallback()
{
    display.tick();   // fallback: if the host never sends screen refreshes, the waveform still updates (at 30 Hz)
    keybed.tick();
    { const bool n = noiseOn.getToggleState(); noiseType.setEnabled (n); noiseRole.setEnabled (n); noiseLevel->setEnabled (n); noiseLevel->setAlpha (n ? 1.0f : 0.4f); }
    if (well.isMouseOver()) well.repaint();
    updateInfo();
    if (proc.ui() != u) refresh();
    if (const bool d = proc.presetIsDirty(); d != shownDirty) { shownDirty = d; face.repaint (presetBtn.getBounds()); }
    if (juce::Time::getMillisecondCounterHiRes() - toastAt < 3400) face.repaint (rToast);
    const auto lv = proc.live();
    chaosView.push (lv);   // trails keep growing while the overlay is closed, like the prototype
    if (hood.isVisible())
    {
        drift.rows.clear(); for (int k = 0; k < hl::NH; ++k) drift.rows.push_back ({ hl::HLABEL[k], lv.hv[k], true, col::bone });
        glides.rows.clear(); for (int i = 0; i < 4; ++i) glides.rows.push_back ({ "Slot " + juce::String (hl::keyName (i)) + " LFO", lv.g[i], true, col::layer (i) });
        drift.repaint(); glides.repaint();
    }
    // background inputs: the knobs, the current sound's character, and the live chaos
    const auto P = proc.settings();
    auto n01 = [] (double v, double a, double b) { return juce::jlimit (0.0, 1.0, std::log (v / a) / std::log (b / a)); };
    const hl::Features f = u && u->out ? u->out->feat : hl::Features {};
    const double noise = juce::jlimit (0.0, 1.0, f.bright / 0.9), sbr = juce::jlimit (0.0, 1.0, f.bright / 0.6), sharp = juce::jlimit (0.0, 1.0, f.attack);
    const double length = n01 (juce::jmax (0.1, f.len), 0.1, 8), ring = juce::jlimit (0.0, 1.0, f.tail * 1.5);
    const double resonance = juce::jmax (P.reso, P.resType != hl::ResType::Off ? P.resAmt / 0.9 * P.resMix : 0.0);
    const double bodyTime = n01 (P.bodyTime, 0.3, 8) * (0.4 + P.body / 0.6 * 0.6), decay = n01 (P.D, 5, 3000);
    const double c[6] = { lv.h[0][0], lv.h[1][1], lv.h[2][2], lv.h[3][0], lv.h[3][2], lv.h[1][2] };
    const double tg[12] = {
        P.rot, 0.65 + 0.7 * P.instability + 0.08 * c[0],
        -0.3 + 0.35 * bodyTime + 0.2 * decay + 0.15 * length - 0.25 * noise + 0.08 * c[1],
        0.2 + 0.8 * noise, 0.35 + 0.8 * resonance + 0.6 * ring, 0.3 + 0.9 * sharp + 0.4 * P.rot,
        0.25 + 0.45 * n01 (P.bright, 0.2, 2) + 0.25 * sbr + 0.05 * c[2],
        0.4 + 1.2 * P.glide, 0.3 + 1.7 * (P.mutation - 0.02) / 0.98,
        0.56 + 0.06 * c[3], 0.44 + 0.04 * c[4], 0.85 + 0.25 * c[5] };
    for (int i = 0; i < 12; ++i) in[i].store ((float) tg[i]);
    for (int i = 0; i < 6; ++i) chaosIn[i].store ((float) c[i]);
    if (curve.isVisible() && (int) juce::Time::getMillisecondCounter() / 100 % 3 == 0) curve.repaint();
    if (shapeView.isVisible() && (int) juce::Time::getMillisecondCounter() / 100 % 2 == 0) shapeView.repaint();
    gl.triggerRepaint();
}

void HalfLifeEditor::stepPreset (int dir)
{
    presetPanel.rescan();
    const auto& all = presetPanel.all;
    if (all.empty()) { showError ("No presets yet. Click the name to save one."); return; }
    int i = -1; const auto cur = u ? u->preset : juce::String();
    for (size_t k = 0; k < all.size(); ++k) if (all[k].name == cur) { i = (int) k; break; }
    const int n = (int) all.size(), next = i < 0 ? (dir > 0 ? 0 : n - 1) : ((i + dir) % n + n) % n;
    const auto err = proc.loadPreset (all[(size_t) next]);
    if (err.isNotEmpty()) showError (all[(size_t) next].name + ": " + err);
    else { presetPanel.current = all[(size_t) next].name; presetPanel.repaint(); }
}

void HalfLifeEditor::showError (const juce::String& e)
{
    toast = e; toastAt = juce::Time::getMillisecondCounterHiRes(); face.repaint();
}

void HalfLifeEditor::filesDropped (const juce::StringArray& files, int, int)
{
    // a dropped preset loads; everything else joins the pool
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".hlpreset")) { const auto err = proc.loadPresetFile (juce::File (f)); if (err.isNotEmpty()) showError (err); return; }
    juce::Array<juce::File> fs; for (auto& f : files) fs.add (juce::File (f));
    proc.poolAdd (fs);   // dropped outside a slot: everything joins the pool
}

/* ======================= background (GL thread) ======================= */
void HalfLifeEditor::newOpenGLContextCreated()
{
    auto make = [this] (const char* frag) -> std::unique_ptr<juce::OpenGLShaderProgram>
    {
        auto p = std::make_unique<juce::OpenGLShaderProgram> (gl);
        if (p->addVertexShader (hlshaders::vertex) && p->addFragmentShader (frag) && p->link()) return p;
        DBG ("Half Life shader error: " << p->getLastError());
        return nullptr;
    };
    prog1 = make (hlshaders::substance); prog2 = make (hlshaders::light);
    const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenBuffers (1, &vbo); glBindBuffer (GL_ARRAY_BUFFER, vbo); glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
    glOk = prog1 != nullptr && prog2 != nullptr;
    for (int i = 0; i < 12; ++i) V[i] = in[i].load();
    lastTime = juce::Time::getMillisecondCounterHiRes();
    T = juce::Random::getSystemRandom().nextDouble() * 200; TM = juce::Random::getSystemRandom().nextDouble() * 200;
}

void HalfLifeEditor::openGLContextClosing()
{
    prog1.reset(); prog2.reset(); fbo.reset();
    if (vbo) { glDeleteBuffers (1, &vbo); vbo = 0; }
    glOk = false;
}

void HalfLifeEditor::renderOpenGL()
{
    const float scale = (float) gl.getRenderingScale();
    const int W = juce::roundToInt (faceW.load() * scale), H = juce::roundToInt (faceH.load() * scale);
    juce::OpenGLHelpers::clear (juce::Colours::black);
    if (! glOk.load() || W < 2 || H < 2) return;
    const double now = juce::Time::getMillisecondCounterHiRes(), dt = juce::jmin (0.1, (now - lastTime) / 1000.0); lastTime = now;
    static const float TC[12] = { 4, 5, 7, 6, 5, 5, 6, 4, 6, 4, 4, 3 };
    for (int i = 0; i < 12; ++i) V[i] += (in[i].load() - V[i]) * (float) (1.0 - std::exp (-dt / TC[i]));
    T += dt * V[7]; TM += dt * V[8];
    dx += (chaosIn[0].load() * 0.05 - dx) * 0.01; dy += (chaosIn[2].load() * 0.012 - dy) * 0.01;
    const float px = 1.5f;
    const int TW = juce::jmax (2, (int) std::ceil (W / (px * scale))), TH = juce::jmax (2, (int) std::ceil (H / (px * scale)));
    if (! fbo || fbo->getWidth() != TW || fbo->getHeight() != TH) { fbo = std::make_unique<juce::OpenGLFrameBuffer>(); fbo->initialise (gl, TW, TH); }
    glDisable (GL_BLEND); glDisable (GL_DEPTH_TEST);
    auto quad = [this] (juce::OpenGLShaderProgram& p)
    {
        glBindBuffer (GL_ARRAY_BUFFER, vbo);
        const GLint loc = glGetAttribLocation (p.getProgramID(), "a");
        if (loc < 0) return;
        glEnableVertexAttribArray ((GLuint) loc); glVertexAttribPointer ((GLuint) loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray ((GLuint) loc);
    };
    // pass 1: the substance and how dense it is, at reduced resolution
    fbo->makeCurrentRenderingTarget();
    glViewport (0, 0, TW, TH);
    prog1->use();
    prog1->setUniform ("res", (GLfloat) TW, (GLfloat) TH); prog1->setUniform ("t", (GLfloat) T); prog1->setUniform ("tm", (GLfloat) TM);
    prog1->setUniform ("drift", (GLfloat) dx, (GLfloat) dy);
    const char* names[] = { "rot", "warp", "solid", "smoke", "wet", "grit", "lightY" };
    for (int i = 0; i < 7; ++i) prog1->setUniform (names[i], (GLfloat) V[i]);
    quad (*prog1);
    fbo->releaseAsRenderingTarget();
    // pass 2: the light behind it, through the ink
    glViewport (0, 0, W, H);
    prog2->use();
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, fbo->getTextureID());
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    prog2->setUniform ("tex", (GLint) 0); prog2->setUniform ("res", (GLfloat) W, (GLfloat) H); prog2->setUniform ("K", (GLfloat) (px * scale));
    prog2->setUniform ("lp", (GLfloat) V[9], (GLfloat) V[10]); prog2->setUniform ("t", (GLfloat) std::fmod (T, 100.0)); prog2->setUniform ("power", (GLfloat) V[11]);
    quad (*prog2);
    glBindTexture (GL_TEXTURE_2D, 0);
}
