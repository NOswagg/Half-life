#include "Ui.h"
#include "BinaryData.h"

/* The look: quiet and printed rather than drawn. Hairlines instead of outlines, text that changes weight and colour
   instead of filled blocks, one strong surface (Trigger) and one corroded one (Perish). */
namespace ui
{
namespace
{
const juce::Colour faint = juce::Colour (246, 242, 236).withAlpha (0.20f);
const juce::Colour label = juce::Colour (0xffd2cbc0);   // section labels and knob names: light bone, readable over the ink

/* deterministic value noise, for the corroded edge and grain of Perish */
float hashf (int x, int y) { juce::uint32 h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return (float) ((h ^ (h >> 16)) & 0xffff) / 65535.0f; }
float noise1 (float x, int seed) { const int i = (int) std::floor (x); const float f = x - i, u = f * f * (3 - 2 * f); return hashf (i, seed) * (1 - u) + hashf (i + 1, seed) * u; }

[[maybe_unused]] juce::Path tornRect (juce::Rectangle<float> r, int seed, float amp)
{
    juce::Path p; const float step = 3.0f;
    auto wob = [&] (float t, int side) { return (noise1 (t / 9.0f, seed + side) - 0.5f) * 2 * amp + (noise1 (t / 2.3f, seed + 10 + side) - 0.5f) * amp * 0.6f; };
    p.startNewSubPath (r.getX(), r.getY() + wob (0, 0));
    for (float x = r.getX(); x <= r.getRight(); x += step) p.lineTo (x, r.getY() + wob (x, 0));
    for (float y = r.getY(); y <= r.getBottom(); y += step) p.lineTo (r.getRight() + wob (y, 1), y);
    for (float x = r.getRight(); x >= r.getX(); x -= step) p.lineTo (x, r.getBottom() + wob (x, 2));
    for (float y = r.getBottom(); y >= r.getY(); y -= step) p.lineTo (r.getX() + wob (y, 3), y);
    p.closeSubPath();
    return p;
}

[[maybe_unused]] juce::Image grain (int w, int h, int seed)
{
    juce::Image im (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float n = hashf (x + seed * 31, y), blotch = noise1 (x / 14.0f + y * 0.37f, seed) * noise1 (y / 7.0f, seed + 3);
            const float a = 0.10f * n + 0.30f * blotch * blotch;
            bd.setPixelColour (x, y, (n > 0.5f ? juce::Colour (0xff3a1a10) : juce::Colours::black).withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        }
    return im;
}
}

Look::Look()
{
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize);
    bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoBold_ttf, BinaryData::ArchivoBold_ttfSize);
    black   = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoBlack_ttf, BinaryData::ArchivoBlack_ttfSize);
    setColour (juce::Label::textColourId, col::ink);
    setColour (juce::ComboBox::textColourId, col::ink);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::bone);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TooltipWindow::textColourId, col::ink);
}
Look& Look::get() { static Look l; return l; }
juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f) { return f.isBold() ? bold : regular; }
juce::Font Look::font (float h, int w) const
{
    const auto& tf = w >= 900 ? black : w >= 700 ? bold : regular;
   #if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (tf)).withHeight (h * TS);
   #else
    return juce::Font (tf).withHeight (h * TS);
   #endif
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float, float, float, juce::Slider& s)
{
    // octagonal body, thin value arc around it; two-sided ranges draw the arc from zero
    const float W = (float) juce::jmin (w, h), R = W * 0.35f, ra = W * 0.47f;
    const juce::Point<float> o ((float) x + w * 0.5f, (float) y + h * 0.5f);
    const float A0 = juce::MathConstants<float>::pi * 0.75f, A1 = juce::MathConstants<float>::pi * 2.25f;
    const double lo = s.getMinimum(), hi = s.getMaximum();
    const float a = A0 + (float) s.valueToProportionOfLength (s.getValue()) * (A1 - A0);
    auto pt = [&] (float ang, float r) { return o + juce::Point<float> (r * std::cos (ang), r * std::sin (ang)); };
    juce::Path oct;
    for (int i = 0; i < 8; ++i) { const auto p = pt (juce::MathConstants<float>::pi / 8 + i * juce::MathConstants<float>::pi / 4, R); if (i == 0) oct.startNewSubPath (p); else oct.lineTo (p); }
    oct.closeSubPath();
    g.setColour (juce::Colour (0xff0a0908)); g.fillPath (oct);
    g.setColour (col::ink.withAlpha (s.isMouseOverOrDragging() ? 0.38f : 0.22f)); g.strokePath (oct, juce::PathStrokeType (1.0f));
    auto arc = [&] (float from, float to)
    {
        juce::Path p; const int n = juce::jmax (2, (int) ((to - from) / 0.05f));
        for (int i = 0; i <= n; ++i) { const auto q = pt (from + (to - from) * i / n, ra); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
        return p;
    };
    g.setColour (col::ink.withAlpha (0.10f)); g.strokePath (arc (A0, A1), juce::PathStrokeType (1.5f));
    const float z = lo < 0 && hi > 0 ? A0 + (float) s.valueToProportionOfLength (0.0) * (A1 - A0) : A0;
    const bool hot = s.getProperties()["hot"];
    const auto c = hot ? col::rustHi : col::bone.withAlpha (0.9f);
    g.setColour (c);
    if (std::abs (a - z) > 0.001f) g.strokePath (arc (juce::jmin (z, a), juce::jmax (z, a)), juce::PathStrokeType (1.5f));
    g.drawLine (juce::Line<float> (pt (a, R * 0.25f), pt (a, R * 0.95f)), 1.6f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    const auto k = b.getProperties()["kind"].toString();
    const float en = b.isEnabled() ? 1.0f : 0.45f;
    auto edge = [&] (juce::Colour c) { g.setColour (c); g.drawRect (r, 1.0f); };
    if (k == "clear" || k == "seg" || k == "segOn" || k == "tab" || k == "tabOn" || k == "x") return;   // text only
    if (k == "octx")
    {
        // − and +: drawn strokes, so they read the same weight at any size
        g.setColour (col::ink.withAlpha (over ? 0.12f : 0.0f)); g.fillRoundedRectangle (r, 3);
        const float cx = r.getCentreX(), cy = r.getCentreY();
        g.setColour (over ? col::ink : col::muted);
        const float L2 = 4.5f * TS, T2 = 0.9f * TS;
        g.fillRect (cx - L2, cy - T2, L2 * 2, T2 * 2);
        if (b.getButtonText() == "+") g.fillRect (cx - T2, cy - L2, T2 * 2, L2 * 2);
        return;
    }
    if (k == "icoPin" || k == "icoPinOn")
    {
        // a drawing pin: head, shoulders, needle
        const float cx = r.getCentreX(), cy = r.getCentreY();
        juce::Path p; p.startNewSubPath (cx - 2, cy - 5.5f);   // drawn at 1x around the centre, then scaled with the text p.lineTo (cx + 2, cy - 5.5f); p.lineTo (cx + 1.6f, cy - 1.5f); p.lineTo (cx + 3.5f, cy + 0.5f);
        p.lineTo (cx - 3.5f, cy + 0.5f); p.lineTo (cx - 1.6f, cy - 1.5f); p.closeSubPath(); p.startNewSubPath (cx, cy + 0.5f); p.lineTo (cx, cy + 5.5f);
        const auto sc = juce::AffineTransform::scale (TS, TS, cx, cy); p.applyTransform (sc);
        g.setColour (k == "icoPinOn" ? col::rustHi : (over ? col::ink : label)); g.strokePath (p, juce::PathStrokeType (1.15f * TS));
        if (k == "icoPinOn") { juce::Path h; h.addRectangle (cx - 1.6f, cy - 5, 3.2f, 4); h.applyTransform (sc); g.fillPath (h); }
        return;
    }
    if (k == "nav")
    {
        g.setColour (col::ink.withAlpha (over ? 0.12f : 0.06f)); g.fillRoundedRectangle (r, 3);
        const bool left = b.getProperties()["dir"].toString() == "left"; const float cx = r.getCentreX(), cy = r.getCentreY();
        juce::Path p; if (left) { p.startNewSubPath (cx + 1.5f, cy - 3.5f); p.lineTo (cx - 2, cy); p.lineTo (cx + 1.5f, cy + 3.5f); }
        else { p.startNewSubPath (cx - 1.5f, cy - 3.5f); p.lineTo (cx + 2, cy); p.lineTo (cx - 1.5f, cy + 3.5f); }
        p.applyTransform (juce::AffineTransform::scale (TS, TS, cx, cy));   // grows with the text
        g.setColour (b.isEnabled() ? (over ? col::ink : col::muted) : col::dim.withAlpha (0.5f)); g.strokePath (p, juce::PathStrokeType (1.4f * TS));
        return;
    }
    if (k == "itogOn") { g.setColour (col::bone); g.fillEllipse (r.reduced (0.5f)); g.setColour (juce::Colours::black); g.setFont (font (12, 700).withStyle (juce::Font::italic)); g.drawText (b.getButtonText(), r, juce::Justification::centred); return; }
    if (k == "itog" || k == "itogOff")
    {
        g.setColour (col::ink.withAlpha (k == "itog" ? (over ? 0.6f : 0.35f) : 0.16f)); g.drawEllipse (r.reduced (0.5f), 1.0f);
        g.setColour (k == "itog" ? col::ink : col::dim); g.setFont (font (12, 700).withStyle (juce::Font::italic));
        g.drawText (b.getButtonText(), r, juce::Justification::centred); return;
    }
    if (k == "trigger")
    {
        g.setGradientFill (juce::ColourGradient (col::bone.withAlpha (down ? 1.0f : 0.92f), 0, r.getY(), juce::Colour (0xffd9d2c7).withAlpha (down ? 1.0f : 0.92f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3);
        if (over && ! down) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (r, 3); }
    }
    else if (k == "main")
    {
        g.setColour (down ? col::ink.withAlpha (0.18f) : juce::Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (r, 3);
        g.setColour (col::ink.withAlpha (over ? 0.5f : 0.26f)); g.drawRoundedRectangle (r.reduced (0.5f), 3, 1.0f);
    }
    else if (k == "perish")
    {
        // no outline: an oxide stain, dense in the middle, dissolving unevenly into the background at its edges
        const int w = (int) r.getWidth(), h = (int) r.getHeight(), state = down ? 2 : over ? 1 : 0;
        static juce::Image cache[3]; static int cw = -1, ch = -1;
        if (cw != w || ch != h) { for (auto& c : cache) c = {}; cw = w; ch = h; }
        auto& im = cache[state];
        if (! im.isValid())
        {
            im = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            const float boost = state == 2 ? 1.35f : state == 1 ? 1.15f : 1.0f;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    // distance inside a soft rounded rectangle, 0 at the edge → 1 well inside
                    const float dx = std::abs (x + 0.5f - w * 0.5f) - (w * 0.5f - 18), dy = std::abs (y + 0.5f - h * 0.5f) - (h * 0.5f - 7);
                    const float out = std::sqrt (juce::jmax (0.0f, dx) * juce::jmax (0.0f, dx) + juce::jmax (0.0f, dy) * juce::jmax (0.0f, dy)) + juce::jmin (0.0f, juce::jmax (dx, dy));
                    const float n = noise1 (x / 11.0f + y * 0.23f, 5) * 0.6f + noise1 (y / 5.0f + x * 0.11f, 9) * 0.4f;   // uneven edge
                    float t = juce::jlimit (0.0f, 1.0f, 1.0f - (out + 9) / (11 + 10 * n));
                    t = t * t * (3 - 2 * t);
                    const float grainV = hashf (x, y);
                    const float a = juce::jlimit (0.0f, 1.0f, (0.82f * t + 0.22f * (grainV - 0.5f) * t) * boost);
                    const float v = 0.75f + 0.5f * (1.0f - (float) y / h) * 0.5f + 0.2f * (grainV - 0.5f);
                    bd.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0, 255, (int) (150 * v * boost)), (juce::uint8) juce::jlimit (0, 255, (int) (50 * v * boost)), (juce::uint8) juce::jlimit (0, 255, (int) (30 * v))).withAlpha (a));
                }
        }
        g.drawImageAt (im, (int) r.getX(), (int) r.getY());
    }
    else if (k == "pinOn" || k == "lockOn") { edge (col::rust.withAlpha (0.8f)); }
    else if (k == "pin" || k == "lock") { edge (col::ink.withAlpha ((over ? 0.4f : 0.18f) * en)); }
    else if (k == "key" || k == "keyOn" || k == "keyBlack" || k == "keyBlackOn")
    {
        // dark slats: white keys barely lift off the panel, black keys are voids; the played note burns at its lower edge
        const bool black = k.startsWith ("keyBlack"), lit = k.endsWith ("On");
        g.setColour (down ? col::ink.withAlpha (0.22f) : black ? juce::Colour (over ? 0xff151311 : 0xff050404) : juce::Colour (20, 18, 16).withAlpha (over ? 0.95f : 0.8f));
        g.fillRect (r);
        if (! black) { g.setGradientFill (juce::ColourGradient (col::ink.withAlpha (0.05f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getBottom(), false)); g.fillRect (r); }
        g.setColour (col::ink.withAlpha (black ? 0.10f : 0.14f)); g.drawRect (r, 1.0f);
        if (lit) { g.setColour (col::rustHi); g.fillRect (r.getX() + 1, r.getBottom() - 3, r.getWidth() - 2, 2.0f);
                   g.setGradientFill (juce::ColourGradient (col::rustHi.withAlpha (0.28f), 0, r.getBottom(), juce::Colours::transparentBlack, 0, r.getBottom() - r.getHeight() * 0.6f, false)); g.fillRect (r.reduced (1, 0)); }
    }
    else if (k == "hood") { edge (col::ink.withAlpha (over ? 0.4f : 0.18f)); }
    else { g.setColour (down ? col::ink.withAlpha (0.12f) : juce::Colours::black.withAlpha (0.3f)); g.fillRect (r); edge (col::ink.withAlpha ((over ? 0.5f : 0.22f) * en)); }   // ghost
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto k = b.getProperties()["kind"].toString();
    auto r = b.getLocalBounds();
    const float en = b.isEnabled() ? 1.0f : 0.45f;
    if (k == "clear" || k == "itog" || k == "itogOff" || k == "itogOn" || k == "icoPin" || k == "icoPinOn" || k == "nav" || k == "octx") return;
    if (k == "x") { g.setColour (over ? col::ink : label); g.setFont (font (13, 400)); g.drawText (b.getButtonText(), r, juce::Justification::centred); return; }
    if (k == "perish")
    {
        g.setColour (down ? juce::Colour (0xfffff0e6) : juce::Colour (0xffe6cfc0));
        if (r.getHeight() < S (52)) { g.setFont (font (14, 900).withExtraKerningFactor (0.38f)); g.drawText (b.getButtonText(), r.translated (3, 0), juce::Justification::centred); return; }
        g.setFont (font (17, 900).withExtraKerningFactor (0.42f));
        g.drawText (b.getButtonText(), r.removeFromTop (r.getHeight() * 58 / 100).translated (4, 2), juce::Justification::centredBottom);
        g.setColour (juce::Colour (0xff9f7b69));
        g.setFont (font (11, 700).withExtraKerningFactor (0.2f));
        g.drawText ("REROLL EVERYTHING", r.translated (1, 1), juce::Justification::centredTop);
        return;
    }
    juce::Font f = font (12.5f, 700); juce::Colour c = (over ? col::ink : col::muted).withAlpha (en); juce::Justification j = juce::Justification::centred;
    if (k == "trigger") { f = font (13, 700).withExtraKerningFactor (0.24f); c = juce::Colour (0xff0b0a09); }
    else if (k == "main") { f = font (13, 700).withExtraKerningFactor (0.24f); c = col::ink; }
    else if (k == "segOn" || k == "tabOn")
    {
        c = col::ink; if (k == "tabOn") f = font (12.5f * BIG, 700).withExtraKerningFactor (0.1f);
        const float tw = juce::jmin ((float) r.getWidth() - 4, juce::GlyphArrangement::getStringWidth (f, b.getButtonText()));
        g.setColour (col::bone); g.fillRect ((r.getWidth() - tw) / 2.0f, (float) r.getBottom() - (k == "tabOn" ? 2.0f : 1.5f), tw, k == "tabOn" ? 2.0f : 1.5f);
    }
    else if (k == "seg") c = over ? col::muted : label;
    else if (k == "tab" || k == "hood") { c = over ? col::ink : label; f = font (12.5f * BIG, 700).withExtraKerningFactor (0.1f); }
    else if (k == "pin" || k == "lock") { c = (over ? col::ink : label).withAlpha (en); f = font (11.5f, 700); }
    else if (k == "pinOn" || k == "lockOn") { c = col::rustHi; f = font (11.5f, 700); }
    else if (k == "key" || k == "keyBlack" || k == "keyOn" || k == "keyBlackOn") { c = k.endsWith ("On") ? col::rustHi : label.withAlpha (0.8f); f = font (10, 700); j = juce::Justification::centredBottom; r = r.withTrimmedBottom (6); }
    g.setColour (c); g.setFont (f);
    const auto tri = b.getProperties()["tri"].toString();
    if (tri.isNotEmpty())
    {
        // the font has no triangles: draw one after the text
        const float tw = juce::GlyphArrangement::getStringWidth (f, b.getButtonText()), x = (r.getWidth() - tw - 14) / 2.0f, cy = r.getCentreY() - 0.5f;
        g.drawText (b.getButtonText(), juce::Rectangle<float> (x, 0, tw + 2, (float) r.getHeight()), juce::Justification::centredLeft);
        juce::Path t; const float tx = x + tw + 9;
        if (tri == "up") t.addTriangle (tx - 3.5f, cy + 2, tx + 3.5f, cy + 2, tx, cy - 2.5f); else t.addTriangle (tx - 3.5f, cy - 2, tx + 3.5f, cy - 2, tx, cy + 2.5f);
        g.fillPath (t);
        return;
    }
    g.drawFittedText (b.getButtonText(), r.reduced (2, 0), j, 1, 0.85f);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const auto r = b.getLocalBounds();
    if (b.getProperties()["chip"])
    {
        // generators: dashed, not solid like the samples; brighter when on
        const bool on = b.getToggleState(); const auto rf = r.toFloat().reduced (0.5f);
        if (on) { g.setColour (col::ink.withAlpha (0.14f)); g.fillRoundedRectangle (rf, 3); g.setColour (col::bone); g.fillEllipse (rf.getX() + 7, rf.getCentreY() - 2.5f, 5, 5); }
        juce::Path p; p.addRoundedRectangle (rf, 3); juce::Path d; const float dash[] = { 3, 2.5f };
        juce::PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
        g.setColour (col::ink.withAlpha (on ? (over ? 0.7f : 0.5f) : (over ? 0.3f : 0.15f))); g.fillPath (d);
        g.setColour (on ? col::ink : col::dim); g.setFont (font (12, 700));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (on ? 6 : 0), juce::Justification::centred);
        return;
    }
    const juce::Rectangle<float> box (1, (r.getHeight() - 12) * 0.5f, 12, 12);
    g.setColour (col::ink.withAlpha (over ? 0.55f : 0.32f)); g.drawRect (box, 1.0f);
    if (b.getToggleState()) { g.setColour (col::bone); g.fillRect (box.reduced (3)); }
    g.setColour (b.getToggleState() ? col::ink : col::muted); g.setFont (font (13, 700));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (19), juce::Justification::centredLeft);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c)
{
    const juce::Rectangle<float> r (0, 0, (float) w, (float) h);
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (r);
    g.setColour (col::ink.withAlpha (c.isMouseOver (true) ? 0.45f : (c.isEnabled() ? 0.2f : 0.1f))); g.drawRect (r, 1.0f);
    juce::Path ar; const float cx = w - 8.0f, cy = h * 0.5f;
    ar.startNewSubPath (cx - 3.5f, cy - 1.5f); ar.lineTo (cx, cy + 2); ar.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (c.isEnabled() ? col::muted : col::dim); g.strokePath (ar, juce::PathStrokeType (1.2f));
}

void Look::positionComboBoxText (juce::ComboBox& c, juce::Label& l)
{
    l.setBounds (4, 0, c.getWidth() - 16, c.getHeight());
    l.setFont (font ((bool) c.getProperties()["big"] ? 12 * BIG : 12, 700)); l.setMinimumHorizontalScale (0.75f);
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff060505));
    g.setColour (faint); g.drawRect (0, 0, w, h, 1);
}

void Look::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active, bool hl, bool ticked, bool, const juce::String& text, const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (sep) { g.setColour (col::line2()); g.fillRect (area.withHeight (1).withY (area.getCentreY()).reduced (6, 0)); return; }
    if (hl && active) { g.setColour (col::ink.withAlpha (0.1f)); g.fillRect (area.reduced (2, 0)); }
    g.setColour (! active ? col::dim : ticked ? col::ink : (hl ? col::ink : col::muted));
    g.setFont (font (13.5f, 700));
    if (ticked) { g.setColour (col::bone); g.fillRect (area.getX() + 6, area.getCentreY() - 2, 4, 4); g.setColour (col::ink); }
    g.drawText (text, area.reduced (16, 0), juce::Justification::centredLeft);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (juce::Colour (0xf2050505)); g.setColour (faint); g.drawRect (0, 0, w, h, 1);
    g.setColour (col::ink); g.setFont (font (13, 400));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
}
juce::Rectangle<int> Look::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (S (360), (int) juce::GlyphArrangement::getStringWidth (font (13, 400), text) + 20), h = w >= S (360) ? S (44) : S (26);
    return juce::Rectangle<int> (pos.x + 12, pos.y + 16, w, h).constrainedWithin (parent);
}

/* ---------- Knob ---------- */
Knob::Knob (HalfLifeProcessor& p, const juce::String& id, bool m, const juce::String& lbl) : mini (m), pid (id)
{
    param = p.apvts.getParameter (id);
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (160);
    slider.setScrollWheelEnabled (true);
    if (id == "rot") slider.getProperties().set ("hot", true);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, slider);
    if (param) slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    for (auto* l : { &name, &value }) addAndMakeVisible (*l);
    style (name, 11.5f, 700, label, mini ? juce::Justification::bottomLeft : juce::Justification::centredTop);
    style (value, 12, 400, col::ink, mini ? juce::Justification::topLeft : juce::Justification::centredTop);
    name.setText (lbl.isNotEmpty() ? lbl : (param ? param->getName (40) : id), juce::dontSendNotification);
    auto upd = [this] { if (param) { value.setText (param->getCurrentValueAsText(), juce::dontSendNotification); } };
    slider.onValueChange = upd; upd();
    name.setMinimumHorizontalScale (0.8f); value.setMinimumHorizontalScale (0.8f);
}

void Knob::setBig()
{
    big = true;
    style (name, 11.5f * BIG, 700, label, juce::Justification::centredTop);
    style (value, 12 * BIG, 400, col::ink, juce::Justification::centredTop);
    name.setMinimumHorizontalScale (0.75f); value.setMinimumHorizontalScale (0.75f);
    resized();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    const bool bare = getWidth() < S (36);   // too narrow for text: just the knob (the value shows in its tooltip)
    name.setVisible (! bare); value.setVisible (! bare);
    if (bare) { const int s = juce::jmin (getWidth(), getHeight()); slider.setBounds (r.withSizeKeepingCentre (s, s)); return; }
    if (mini)
    {
        const int s = juce::jmin (S (32), r.getHeight());
        slider.setBounds (r.removeFromLeft (s).withSizeKeepingCentre (s, s));
        r.removeFromLeft (6);
        name.setBounds (r.removeFromTop (r.getHeight() / 2)); value.setBounds (r);
        return;
    }
    // the knob gets what the two text lines leave
    const int nh = juce::roundToInt (name.getFont().getHeight()) + 2, vh = juce::roundToInt (value.getFont().getHeight()) + 2;
    const int s = juce::jlimit (30, S (44), r.getHeight() - nh - vh - 2);
    slider.setBounds (r.removeFromTop (s).withSizeKeepingCentre (s, s));
    r.removeFromTop (2);
    name.setBounds (r.removeFromTop (nh)); value.setBounds (r.removeFromTop (vh));
}

/* ---------- Plate ---------- */
void Plate::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (open)
    {
        // generators: not a specimen slide like the samples but a bare rack, the background shows through
        g.setColour (juce::Colour (6, 5, 4).withAlpha (0.35f)); g.fillRect (r);
        juce::Path p; p.addRectangle (r.reduced (0.5f)); juce::Path d; const float dash[] = { 3, 3 };
        juce::PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
        g.setColour (juce::Colour (246, 242, 236).withAlpha (0.16f)); g.fillPath (d);
        if (title.isNotEmpty()) { g.setColour (label); g.setFont (Look::get().font (11, 700).withExtraKerningFactor (0.16f)); g.drawText (title.toUpperCase(), 10, 7, getWidth() - 20, 14, juce::Justification::topLeft); }
        return;
    }
    if (bare) return;
    // an island: a see-through panel over the ink, soft corners, almost no edge
    g.setColour (alpha > 0 ? juce::Colour (6, 5, 5).withAlpha (alpha) : juce::Colour (14, 12, 11).withAlpha (deep ? 0.88f : 0.8f)); g.fillRoundedRectangle (r, 6);
    g.setColour (juce::Colour (246, 242, 236).withAlpha (0.06f)); g.drawRoundedRectangle (r.reduced (0.5f), 6, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (label); g.setFont (Look::get().font (11, 700).withExtraKerningFactor (0.16f));
        g.drawText (title.toUpperCase(), S (10), S (7), getWidth() - S (20), S (14), juce::Justification::topLeft);
    }
}

/* ---------- Seg ---------- */
Seg::Seg (HalfLifeProcessor& p, const juce::String& paramId) : proc (p), id (paramId)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
        for (int i = 0; i < c->choices.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (c->choices[i]));
            b->onClick = [this, i, c] { c->beginChangeGesture(); c->setValueNotifyingHost (c->convertTo0to1 ((float) i)); c->endChangeGesture(); };
            addAndMakeVisible (b);
        }
    proc.apvts.addParameterListener (id, this);
    handleAsyncUpdate();
}
Seg::~Seg() { proc.apvts.removeParameterListener (id, this); }
void Seg::handleAsyncUpdate()
{
    const int v = juce::roundToInt (proc.apvts.getRawParameterValue (id)->load());
    for (int i = 0; i < buttons.size(); ++i) kind (*buttons[i], i == v ? "segOn" : "seg");
}
void Seg::layout (int width, bool apply, int& rows) const
{
    int x = 0, y = 0; rows = 1; const int h = S (22), gap = 2;
    for (auto* b : buttons)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (Look::get().font (12.5f, 700), b->getButtonText()) + S (12);
        if (x > 0 && x + w > width) { x = 0; y += h + gap; rows++; }
        if (apply) b->setBounds (x, y, w, h);
        x += w + gap;
    }
}
void Seg::resized() { int rows; layout (getWidth(), true, rows); }
int Seg::rowsNeeded (int width) const { int rows; layout (width, false, rows); return rows; }
int Seg::widthNeeded() const { int w = 0; for (auto* b : buttons) w += (int) juce::GlyphArrangement::getStringWidth (Look::get().font (12.5f, 700), b->getButtonText()) + S (12) + 2; return w; }
}
