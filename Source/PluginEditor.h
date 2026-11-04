#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour teal      { 0xff3fd6c8 };
const juce::Colour coral     { 0xffff7a59 };
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };
// the pedal's four module colours
const juce::Colour modR      { 0xffe8392f };
const juce::Colour modY      { 0xfff2c230 };
const juce::Colour modG      { 0xff4fcf6a };
const juce::Colour modB      { 0xff4d8dff };
inline juce::Colour module (int m) { return m == 0 ? modR : (m == 1 ? modY : (m == 2 ? modG : modB)); }

juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
void pill (juce::Graphics&, juce::Rectangle<float> b, const juce::String& text, bool on, float size, juce::Colour onColour = ff::ink);

// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (onClick) onClick (e); }
    std::function<void (const juce::MouseEvent&)> onClick;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 34.0f;
};

// Rounded pill buttons bound to a choice parameter.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setStacked (bool s) { stacked = s; repaint(); }

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    bool stacked = false;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A single pill that turns ink-white when its switch is on. It can show different words on and off.
class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                const juce::String& onText = {}, bool fillWhenOn = true, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const { return on; }

private:
    juce::String text, onText;
    bool fill;
    float textSize;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A pill that opens a menu of a choice parameter's options.
class MenuChip : public juce::Component
{
public:
    MenuChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& prefix = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param;
    juce::String prefix;
    int value = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A plain pill button that runs a function.
class ActionChip : public juce::Component
{
public:
    ActionChip (const juce::String& t, std::function<void()> fn, float textSize = 12.0f)
        : text (t), action (std::move (fn)), size (textSize) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { pill (g, getLocalBounds().toFloat().reduced (1.0f), text, lit, size); }
    void mouseDown (const juce::MouseEvent&) override { if (action) action(); }
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }

private:
    juce::String text;
    std::function<void()> action;
    float size;
    bool lit = false;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A knob drawn as a small clock face, with its name above and value below.
// While a Gesture loops this knob, the hand follows the loop and the name gets a red dot.
class Dial : public juce::Component
{
public:
    Dial (PrismProcessor&, const juce::String& paramId, const juce::String& caption, bool redHand = false, int knobSize = 80);
    void paint (juce::Graphics&) override;
    void resized() override;
    void showGesture (bool active, float value);
    void setValueText (const juce::String& t) { if (t != valueText) { valueText = t; repaint(); } }
    juce::String getId() const { return id; }

private:
    PrismProcessor& proc;
    juce::Slider slider;
    juce::String valueText;
    juce::RangedAudioParameter* param = nullptr;
    juce::String id, caption;
    int knobSize;
    bool gestureActive = false;
    float gestureValue = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A slim slider with its name on the left and value on the right.
class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Five effects and Off for one module, three to a row, the chosen one lit in the module colour.
class EffectGrid : public juce::Component
{
public:
    EffectGrid (juce::AudioProcessorValueTreeState&, int module);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    int module, selected = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

enum class View { Main, Deep, Pedal };

// One module's section. The editor slides it into whichever slot it holds in the chain.
class ModuleSection : public juce::Component
{
public:
    ModuleSection (PrismProcessor&, int module);
    void setView (View);
    void setSlot (int s) { if (s != slot) { slot = s; repaint(); } }
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double dt);

    FlipCard card;

private:
    PrismProcessor& proc;
    int module, slot = 0;
    View view = View::Main;
    ActionChip left, right;
    EffectGrid grid;
    std::unique_ptr<Dial> knobDial, amountDial, deepA, deepB;
    std::unique_ptr<Pills> mainPills;           // Texture's filter style
    std::unique_ptr<Pills> deepPills, deepPills2;
    std::unique_ptr<ToggleChip> deepToggle1, deepToggle2, deepToggle3, syncToggle;
    bool lastOn = true;
    std::unique_ptr<ActionChip> deepAction;
    std::unique_ptr<MenuChip> deepMenu;
    std::unique_ptr<LabelledSlider> deepSlider1, deepSlider2;
    ToggleChip hinge;
    juce::String deepLabel1, deepLabel2;
};

// One footswitch on the Pedal page: its jobs, what it sends, Learn and Default.
class FootswitchCard : public juce::Component
{
public:
    FootswitchCard (PrismProcessor&, int sw);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    PrismProcessor& proc;
    int sw;
    FlipCard letter;
    ActionChip learn, reset;
    std::vector<std::unique_ptr<ToggleChip>> group;
    juce::String sends;
    int lastState = -1;
};

// Gesture, Capture and Bypass: the big round buttons in the strip.
class BigButton : public juce::Component
{
public:
    std::function<void()> onDown, onUp, onRightClick;
    void set (const juce::String& main, const juce::String& small, juce::Colour border);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String mainText, smallText;
    juce::Colour borderColour = ff::line;
    bool pressed = false;
};

// Small A to D squares that act exactly like the footswitches, and light when one is pressed.
class MiniFoot : public juce::Component
{
public:
    MiniFoot (PrismProcessor& p, int s) : proc (p), sw (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (sw); }

private:
    PrismProcessor& proc;
    int sw;
};

// "FOOTSWITCHES · MODULES": click to choose what the four footswitches do.
class FootLayoutLabel : public juce::Component
{
public:
    explicit FootLayoutLabel (PrismProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    PrismProcessor& proc;
};

// The expression pedal's live position. Click for its target, Learn and Calibrate.
class ExpressionMeter : public juce::Component
{
public:
    explicit ExpressionMeter (PrismProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    PrismProcessor& proc;
};

// Where the Capture loop is, blue for a pad, green for a loop, red while recording.
class CaptureBar : public juce::Component
{
public:
    explicit CaptureBar (PrismProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    PrismProcessor& proc;
};

// Pebble from Flip Fella, reacting to module changes, Gesture, Capture and the pedals.
class Pebble : public juce::Component
{
public:
    explicit Pebble (PrismProcessor& p) : proc (p) {}
    void react (juce::Colour c, float lookDir) { flash = 1.0f; flashColour = c; look = lookDir; hop = 1.0f; }
    void update (double now, double dt);
    void paint (juce::Graphics&) override;

private:
    PrismProcessor& proc;
    float level = 0, look = 0, hop = 0, flash = 0, sway = 0, lean = 0, wobble = 0;
    juce::Colour flashColour = ff::teal;
    int capState = 0, lastPulse = 0;
    bool asleep = false, blinking = false, watching = false;
    double lastSound = 0, nextBlink = 2.0, blinkUntil = 0, now = 0;
};
} // namespace ui

class PrismEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismEditor (PrismProcessor&);
    ~PrismEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setView (ui::View);
    void presetMenu();
    void layoutModules();

    PrismProcessor& proc;
    ui::FlipLookAndFeel lnf;
    double lastTick = 0;
    ui::View view = ui::View::Main;

    // header
    ui::Pills inputMode;
    ui::FlipCard tempoCard;
    ui::ToggleChip sync;
    ui::ActionChip viewMain, viewDeep, viewPedal;

    std::array<std::unique_ptr<ui::ModuleSection>, 4> sections;
    std::array<std::unique_ptr<ui::FootswitchCard>, 4> footCards;

    // strip
    ui::Pebble pebble;
    ui::BigButton gestureButton, captureButton, bypassButton;
    ui::CaptureBar captureBar;
    ui::Dial mix;
    ui::FlipCard presetCard;
    ui::ActionChip bankA, bankB, bankC, bankD;
    std::array<std::unique_ptr<ui::MiniFoot>, 4> minis;
    std::unique_ptr<ui::FootLayoutLabel> footLayout;
    ui::ExpressionMeter expression;

    // Keeper
    ui::BigButton keepButton;
    ui::FlipCard contextCard;
    juce::TextEditor noteEditor;
    int noteNumber = 0, lastKeepPulse = 0;
    double keptUntil = 0.0, noteUntil = 0.0;
    void keeperMenu();
    void closeNote (bool save);

    std::array<int, 4> lastFx { -1, -1, -1, -1 };
    std::array<int, 4> lastOrder { -1, -1, -1, -1 };
    int lastDeepToggle = 0;
    juce::String mood, moodSub, captureText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismEditor)
};
