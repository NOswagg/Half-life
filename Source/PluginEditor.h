#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Ui.h"

/* Half Life — the window. A 1200 × 800 face (the mockup) that scales with the window, over the backlit-ink background
   drawn by the graphics card. Panels read the render thread's latest snapshot (proc.ui()) and post commands back. */

class Display : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Display (HalfLifeProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void resized() override { built = false; }
    void tick();
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    juce::Rectangle<int> waveArea() const { return getLocalBounds().reduced (12, 10); }
    bool dragging = false;
    juce::VBlankAttachment vblank { this, [this] { tick(); } };   // the playhead moves at the screen's refresh rate (60 fps), not the 30 Hz timer
    void rebuild (const UiState&);
    HalfLifeProcessor& proc;
    juce::RectangleList<float> body, core; juce::Path curve; bool hasCurve = false, built = false;   // the waveform as shapes (crisp through OpenGL)
    int shownId = -1; const hl::Out* shownOut = nullptr; double fadeStart = -10000; float lastPh = -1; bool wasFading = false;
};

class SlotCard : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    SlotCard (HalfLifeProcessor& p, int idx);
    void paint (juce::Graphics&) override;
    void resized() override;
    void update (const UiState::SlotUi&);
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { drag = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { drag = false; repaint(); }
private:
    HalfLifeProcessor& proc; int idx;
    UiState::SlotUi s; bool drag = false;
    juce::TextButton pin, clear { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::ComboBox role, lib;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roleAtt;
    std::unique_ptr<juce::FileChooser> chooser;
};

class Keybed : public juce::Component
{
public:
    explicit Keybed (HalfLifeProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    struct Key : juce::TextButton { int i = 0; Keybed* kb = nullptr; bool held = false;
        void mouseDown (const juce::MouseEvent&) override; void mouseUp (const juce::MouseEvent&) override; };
    HalfLifeProcessor& proc;
    juce::OwnedArray<Key> keys; juce::TextButton down { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) }, up { "+" };
    juce::Label oct;
    void setOct (int o);
public:
    void tick();   // lights the key of the last note played
private:
    int lit = -100;
};

class History : public juce::Component
{
public:
    explicit History (HalfLifeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void update (const UiState& u) { items = u.history; repaint(); }
private:
    HalfLifeProcessor& proc; std::vector<UiState::HistUi> items;
    juce::Rectangle<int> cell (int i) const
    {
        const int n = juce::jmax (1, (getWidth() + 4) / 46), w = (getWidth() - (n - 1) * 4) / n;
        return i < n ? juce::Rectangle<int> (i * (w + 4), 18, w, getHeight() - 18) : juce::Rectangle<int> (getWidth() + 10, 0, 1, 1);
    }
};

/* the export, as an object: a small dashed well holding the sound; drag it into the DAW */
class DragWell : public juce::Component
{
public:
    explicit DragWell (HalfLifeProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::shared_ptr<const UiState> u;
private:
    HalfLifeProcessor& proc; bool dragging = false;
};

/* the Shape (ADSR) the sound is built toward, drawn over Max length */
class ShapeView : public juce::Component
{
public:
    explicit ShapeView (HalfLifeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    HalfLifeProcessor& proc;
};

class Graph : public juce::Component, public juce::SettableTooltipClient   // interaction graph
{
public:
    void paint (juce::Graphics&) override;
    std::shared_ptr<const UiState> u; double interact = 0.6;
};

class Curve : public juce::Component   // distortion transfer curve
{
public:
    explicit Curve (HalfLifeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    HalfLifeProcessor& proc;
};

class ChaosView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void push (const HalfLifeProcessor::Live& l);
private:
    std::array<std::deque<juce::Point<float>>, 8> trails;
};

class Bars : public juce::Component   // rows of labelled bars (global drift, glide LFOs, rot recipe)
{
public:
    void paint (juce::Graphics&) override;
    struct Row { juce::String name; float v; bool centred; juce::Colour c; };
    std::vector<Row> rows;
};

class AboutPanel : public ui::Plate
{
public:
    AboutPanel() : ui::Plate ({}, true) { setInterceptsMouseClicks (true, false); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClose;
};

class HalfLifeEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer,
                       private juce::ChangeListener, private juce::Timer, private juce::OpenGLRenderer
{
public:
    explicit HalfLifeEditor (HalfLifeProcessor&);
    ~HalfLifeEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override {}
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void layoutFace();
    void refresh();
    void showTab (int i);
    void showHood (bool on);
    void paintFace (juce::Graphics&);
    void paintFaceOver (juce::Graphics&);
    void layoutBench();
    void updateInfo();
    void paintInfo (juce::Graphics&);

    HalfLifeProcessor& proc;
    std::shared_ptr<const UiState> u;

    struct Face : juce::Component { HalfLifeEditor* ed = nullptr; void paintOverChildren (juce::Graphics& g) override { ed->paintFaceOver (g); } } face;
    // headings, readout, logo: on their own layer above the panels (so the panels don't dim them) and below the pop-ups
    struct Labels : juce::Component { HalfLifeEditor* ed = nullptr; Labels() { setInterceptsMouseClicks (false, false); } void paint (juce::Graphics& g) override { ed->paintFace (g); } } labels;

    // header
    juce::TextButton perish { "PERISH" };
    std::unique_ptr<ui::Knob> volume;
    juce::Rectangle<int> rLayers, rInter, rBench, rToast, rReadout, rSrcHead, rPoolHead, rGensHead, rTakeHead, rDecideHead, rHintsLabel;
    ui::Plate headPlate, srcPlate, stripPlate { {}, false }, takePlate;
    juce::TextButton interBtn;   // interBtn: the collapsed Interaction line in the header
    void layoutInter();
    void layoutLocks();
    // the info line: what's under the mouse, or the three-step workflow
    juce::Rectangle<int> rInfo; juce::String infoText; juce::TextButton infoToggle, aboutBtn; AboutPanel about;   // where the face paints the layer list, the interaction caption, the status line
    // sources
    juce::Label poolInfo; juce::TextButton poolPick { "Choose folder" }, poolFiles { "Add files" }, poolClear { "Clear" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::OwnedArray<SlotCard> slots;
    juce::ToggleButton noiseOn { "Noise" }, clickOn { "Click" }, blipOn { "Blip" };
    juce::ComboBox noiseType, noiseRole;
    std::unique_ptr<ui::Knob> noiseLevel;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> btnAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtts;
    // main
    juce::OwnedArray<juce::TextButton> lockBtns;
    DragWell well { proc };
    Display display { proc };
    juce::TextButton trig { "TRIGGER" }, fresh { "FRESH" }, mutate { "MUTATE" };

    std::unique_ptr<ui::Seg> freshSeg, targetSeg;
    std::unique_ptr<ui::Knob> fitK, gateK, candK;
    Keybed keybed { proc };
    juce::OwnedArray<ui::Knob> macroKnobs;

    History history { proc };
    // bench
    juce::OwnedArray<juce::TextButton> tabs; juce::TextButton hoodBtn;
    ui::Plate bench;
    int tab = 0;
    juce::OwnedArray<ui::Knob> modKnobs; std::vector<int> modKnobTab; std::vector<juce::String> modKnobDist;
    juce::OwnedArray<juce::Label> hints; std::vector<int> hintTab;
    juce::ComboBox resType, resStruct, distType;
    Curve curve { proc };
    ShapeView shapeView { proc };
    // under the hood
    ui::Plate hood { "Under the hood" };
    juce::TextButton hoodClose { "Close" }, rerouteChaos { "Reroute chaos" };
    ChaosView chaosView; Bars drift, glides, rotBars;
    juce::Label hoodHint, driftL, glideL, rotL, chaosL, slotGlideL;
    juce::OwnedArray<ui::Knob> chaosKnobs;
    juce::OwnedArray<juce::Label> glideTexts; juce::OwnedArray<juce::TextButton> glideReroute;
    // interaction
    ui::Plate interPanel { "Interaction" };
    Graph graph; juce::ToggleButton lockLinks { "Lock links" }; juce::TextButton reroll { "Reroll links" };
    juce::OwnedArray<juce::Label> linkRows;
    // status
    juce::String toast; double toastAt = -10000; int toastSerial = 0;

    // background
    juce::OpenGLContext gl;
    std::unique_ptr<juce::OpenGLShaderProgram> prog1, prog2;
    std::unique_ptr<juce::OpenGLFrameBuffer> fbo;
    juce::uint32 vbo = 0;
    std::atomic<bool> glOk { false };
    std::atomic<float> in[12] {};
    std::atomic<float> chaosIn[6] {};
    double T = 0, TM = 0, lastTime = 0, dx = 0, dy = 0;
    float V[12] {};
    std::atomic<int> faceW { 1200 }, faceH { 800 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalfLifeEditor)
};
