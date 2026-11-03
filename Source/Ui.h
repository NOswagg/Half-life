#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

/* Half Life — window building blocks: the look (Archivo, octagonal knobs, hairline plates) and small widgets.
   Colours and sizes follow the prototype's CSS (half-life.html + the plugin-window mockup). */
namespace ui
{
/* text sizes: every size written in the code is multiplied by TS (1.5), and the bottom panel by BIG on top (2x in all).
   S() scales a hand-placed size that holds text, so the layout grows with it. */
constexpr float TS = 1.5f, BIG = 4.0f / 3.0f;
inline int S (float v) { return juce::roundToInt (v * TS); }

namespace col
{
    const juce::Colour ink { 0xfff6f2ec }, muted { 0xffe2dcd2 }, dim { 0xffa59e94 }, bone { 0xffebe5dc }, rust { 0xffb4553a }, rustHi { 0xffe07a5c };
    const juce::Colour screen { 0xff040404 };
    inline juce::Colour line() { return juce::Colour (246, 242, 236).withAlpha (0.26f); }
    inline juce::Colour line2() { return juce::Colour (246, 242, 236).withAlpha (0.16f); }
    inline juce::Colour hair() { return juce::Colour (246, 242, 236).withAlpha (0.13f); }
    inline juce::Colour track() { return juce::Colour (246, 242, 236).withAlpha (0.22f); }
    inline juce::Colour glass() { return juce::Colour (7, 6, 5).withAlpha (0.84f); }
    inline juce::Colour glassDeep() { return juce::Colour (4, 4, 4).withAlpha (0.9f); }
    inline juce::Colour layer (int key) { const juce::uint32 c[] = { 0xfff2ede6, 0xffc9c3ba, 0xffa29c93, 0xff7f7a73, 0xffb6b0a7, 0xffffffff, 0xff8f8a83 }; return juce::Colour (c[juce::jlimit (0, 6, key)]); }
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    static Look& get();
    juce::Font font (float h, int weight = 400) const;      // 400, 700, 900
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox& c) override { return font ((bool) c.getProperties()["big"] ? 13 * BIG : 13, 700); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return font (14, 700); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool hl, bool ticked, bool sub, const juce::String& text, const juce::String&, const juce::Drawable*, const juce::Colour*) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
private:
    juce::Typeface::Ptr regular, bold, black;
};

/* button kinds (set with ui::kind (button, "...")): trigger, main, ghost, seg, segOn, perish, tab, tabOn, pin, pinOn, lock, lockOn, rec, key, keyBlack, keyOn */
inline void kind (juce::Button& b, const juce::String& k) { b.getProperties().set ("kind", k); b.repaint(); }

/* a slider with Shift for fine control */
class FineSlider : public juce::Slider
{
public:
    void mouseDown (const juce::MouseEvent& e) override { setMouseDragSensitivity (e.mods.isShiftDown() ? 600 : 160); juce::Slider::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setMouseDragSensitivity (e.mods.isShiftDown() ? 600 : 160); juce::Slider::mouseDrag (e); }
};

/* knob + name + value, bound to a host parameter. mini = small knob with the text beside it */
class Knob : public juce::Component
{
public:
    Knob (HalfLifeProcessor& p, const juce::String& paramId, bool mini = false, const juce::String& label = {});
    void resized() override;
    void paint (juce::Graphics&) override {}
    FineSlider slider;
    bool mini, big = false;
    void setBig();   // the bottom panel's knobs: names and values at 2x
    juce::String pid;
private:
    juce::Label name, value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    juce::RangedAudioParameter* param = nullptr;
};

/* a translucent plate with a hairline edge and an optional title */
class Plate : public juce::Component
{
public:
    explicit Plate (juce::String t = {}, bool deep = false) : title (std::move (t)), deep (deep) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics&) override;
    juce::String title; bool deep; float alpha = -1; bool open = false; bool bare = false;   // alpha: an opaque overlay (Under the hood, Interaction)
};

/* a segmented choice (Keep / Swap one / Swap all, Decide by …) bound to a choice parameter */
class Seg : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    Seg (HalfLifeProcessor& p, const juce::String& paramId);
    ~Seg() override;
    void resized() override;
    int rowsNeeded (int width) const;
    int widthNeeded() const;
private:
    HalfLifeProcessor& proc; juce::String id;
    juce::OwnedArray<juce::TextButton> buttons;
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void layout (int width, bool apply, int& rows) const;
};

/* small text label in the house style */
inline void style (juce::Label& l, float size, int weight, juce::Colour c, juce::Justification j = juce::Justification::centredLeft)
{
    l.setFont (Look::get().font (size, weight)); l.setColour (juce::Label::textColourId, c); l.setJustificationType (j);
    l.setInterceptsMouseClicks (false, false); l.setBorderSize ({ 0, 0, 0, 0 }); l.setMinimumHorizontalScale (0.7f);
}
}
