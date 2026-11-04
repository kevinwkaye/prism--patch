#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
static float raw (PrismProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

static void setParam (RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour)
{
    const float r = b.getHeight() * 0.5f;
    if (on) { g.setColour (onColour); g.fillRoundedRectangle (b, r); }
    else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawFittedText (text, b.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
}

// =====================================================================================
// FlipCard
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills and chips
// =====================================================================================
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size)
    : labels (std::move (l)), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { selected = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const int n = labels.size();
    if (stacked)
    {
        const float gap = 10.0f, h = ((float) getHeight() - gap * (float) (n - 1)) / (float) n;
        return { 0.0f, (float) i * (h + gap), (float) getWidth(), h };
    }
    const float gap = 10.0f, w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i) pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position)) { attachment->setValueAsCompleteGesture ((float) i); return; }
}

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, const String& tOn, bool fillOn, float size)
    : text (t), onText (tOn.isEmpty() ? t : tOn), fill (fillOn), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g) { pill (g, getLocalBounds().toFloat().reduced (1.0f), on ? onText : text, on && fill, textSize); }
void ToggleChip::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

MenuChip::MenuChip (AudioProcessorValueTreeState& s, const String& paramId, const String& p) : prefix (p)
{
    param = s.getParameter (paramId);
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { value = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

void MenuChip::paint (Graphics& g)
{
    auto* c = dynamic_cast<AudioParameterChoice*> (param);
    const String name = c != nullptr ? c->choices[value] : param->getCurrentValueAsText();
    pill (g, getLocalBounds().toFloat().reduced (1.0f), (prefix + name).toUpperCase() + "  v", value > 0, 12.0f);
}

void MenuChip::mouseDown (const MouseEvent&)
{
    auto* c = dynamic_cast<AudioParameterChoice*> (param);
    if (c == nullptr) return;
    PopupMenu m;
    for (int i = 0; i < c->choices.size(); ++i)
    {
        // group the twenty effects by module
        if (i == 1 || i == 6 || i == 11 || i == 16) m.addSectionHeader (names::modules[(i - 1) / 5]);
        m.addItem (i + 1, c->choices[i], true, i == value);
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r > 0) attachment->setValueAsCompleteGesture ((float) (r - 1));
    });
}

// =====================================================================================
// Look and feel
// =====================================================================================
void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 7.0f, right = (float) (x + w) - 7.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);
    g.setColour (ff::ink.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.85f));
    g.fillRoundedRectangle (left, cy - 3.0f, jmax (0.0f, pos - left), 6.0f, 3.0f);
    g.setColour (ff::ink);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        g.drawLine ({ { c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f },
                      { c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f } }, 2.0f);
    }

    // a looping Gesture moves the hand without moving the knob's own value
    const float gest = (float) s.getProperties().getWithDefault ("gesture", -1.0f);
    const float shown = gest >= 0.0f ? gest : pos;
    const bool red = s.getProperties()["red"];
    g.setColour (red ? ff::red : ff::ink);
    const float a = start + shown * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    if (gest >= 0.0f)
    {
        // a faint ghost hand where the knob itself sits
        Path ghost;
        ghost.addRoundedRectangle (-1.5f, -r * 0.68f, 3.0f, r * 0.5f, 1.0f);
        g.setColour (ff::faint);
        g.fillPath (ghost, AffineTransform::rotation (start + pos * (end - start)).translated (c));
        g.setColour (red ? ff::red : ff::ink);
    }
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

// =====================================================================================
// Dial and slider
// =====================================================================================
Dial::Dial (PrismProcessor& p, const String& paramId, const String& c, bool redHand, int size)
    : proc (p), id (paramId), caption (c), knobSize (size)
{
    param = p.apvts.getParameter (paramId);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.getProperties().set ("red", redHand);
    slider.onValueChange = [this] { repaint(); };
    slider.onDragStart = [this] { proc.touchKnob (id); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
}

void Dial::showGesture (bool active, float value)
{
    if (active == gestureActive && std::abs (value - gestureValue) < 0.001f) return;
    gestureActive = active;
    gestureValue = value;
    slider.getProperties().set ("gesture", active ? value : -1.0f);
    slider.repaint();
    repaint();
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (24).withSizeKeepingCentre (knobSize, knobSize));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    const auto top = r.removeFromTop (18);
    ff::label (g, caption.toUpperCase(), top, Justification::centred, gestureActive ? ff::ink : ff::dim);
    if (gestureActive)
    {
        // a red dot after the name says a Gesture is looping this knob
        const float w = ff::font (13.0f, 0.08f).getStringWidthFloat (caption.toUpperCase());
        g.setColour (ff::red);
        g.fillEllipse ((float) top.getCentreX() + w * 0.5f + 5.0f, (float) top.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
    String value = gestureActive ? param->getText (gestureValue, 0) : param->getCurrentValueAsText();
    if (valueText.isNotEmpty()) value = valueText;
    ff::label (g, value.toUpperCase(), r.removeFromBottom (20), Justification::centred, ff::ink, 15.0f);
}

LabelledSlider::LabelledSlider (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.onValueChange = [this] { repaint(); };
}

void LabelledSlider::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }

void LabelledSlider::paint (Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (16);
    ff::label (g, caption.toUpperCase(), top, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), top, Justification::centredRight, ff::ink, 11.0f);
}

// =====================================================================================
// Effect grid
// =====================================================================================
EffectGrid::EffectGrid (AudioProcessorValueTreeState& s, int m) : module (m)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (ParamIDs::fx[m]), [this] (float v) { selected = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

Rectangle<float> EffectGrid::cell (int i) const
{
    const float gap = 10.0f, w = ((float) getWidth() - 2.0f * gap) / 3.0f, h = ((float) getHeight() - gap) / 2.0f;
    return { (float) (i % 3) * (w + gap), (float) (i / 3) * (h + gap), w, h };
}

void EffectGrid::paint (Graphics& g)
{
    for (int i = 0; i < 6; ++i)
    {
        const auto b = cell (i).reduced (1.0f);
        const bool on = i == selected;
        const float r = b.getHeight() * 0.5f;
        if (on) { g.setColour (i < 5 ? ff::module (module) : ff::ink); g.fillRoundedRectangle (b, r); }
        else { g.setColour (ff::line); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
        g.setColour (on ? ff::bg : ff::dim);
        g.setFont (ff::font (11.0f, 0.05f));
        g.drawFittedText (names::effects[module][i].toUpperCase(), b.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

void EffectGrid::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < 6; ++i)
        if (cell (i).contains (e.position)) { attachment->setValueAsCompleteGesture ((float) i); return; }
}

// =====================================================================================
// Module section
// =====================================================================================
ModuleSection::ModuleSection (PrismProcessor& p, int m)
    : proc (p), module (m),
      left ("<", [this] { proc.moveModule (slot, -1); }, 13.0f),
      right (">", [this] { proc.moveModule (slot, 1); }, 13.0f),
      grid (p.apvts, m),
      hinge (p.apvts, ParamIDs::on[m], "BYPASSED", "MODULE ON")
{
    auto& s = p.apvts;
    card.setTextHeight (30.0f);
    card.setMouseCursor (MouseCursor::PointingHandCursor);
    card.onClick = [this] (const MouseEvent&)
    {
        auto* fxp = proc.apvts.getParameter (ParamIDs::fx[module]);
        setParam (fxp, (float) ((roundToInt (raw (proc, ParamIDs::fx[module])) + 1) % 6));
    };
    for (auto* c : std::initializer_list<Component*> { &left, &right, &card, &grid, &hinge }) addAndMakeVisible (c);

    if (m < 3) knobDial = std::make_unique<Dial> (p, ParamIDs::knob[m], names::knobNames[m]);
    amountDial = std::make_unique<Dial> (p, ParamIDs::amount[m], "Amount");

    switch (m)
    {
        case pp::ModCharacter:
            deepA = std::make_unique<Dial> (p, ParamIDs::sens, "Sensitivity");
            deepB = std::make_unique<Dial> (p, ParamIDs::vol[m], "Effect Vol");
            deepLabel1 = "INPUT HEADROOM";
            deepPills = std::make_unique<Pills> (s, ParamIDs::calLevel, StringArray { "LOW", "MED", "HIGH", "V HIGH" }, 12.0f);
            deepAction = std::make_unique<ActionChip> ("AUTO CALIBRATE: PLAY FOR 3 SECONDS", [this] { proc.autoCalibrate(); });
            break;
        case pp::ModMovement:
            deepA = std::make_unique<Dial> (p, ParamIDs::driftM, "Drift");
            deepB = std::make_unique<Dial> (p, ParamIDs::vol[m], "Effect Vol");
            deepLabel1 = "GESTURE QUANTISE";
            deepLabel2 = "GESTURE SPEED";
            deepPills = std::make_unique<Pills> (s, ParamIDs::gestQuant, StringArray { "FREE", "1 BAR", "2 BAR", "4 BAR" }, 12.0f);
            deepPills2 = std::make_unique<Pills> (s, ParamIDs::gestSpeed, StringArray { "1/4", "1/2", "1X", "2X", "4X" }, 12.0f);
            syncToggle = std::make_unique<ToggleChip> (s, ParamIDs::rateSync, "RATE IS FREE", "RATE FOLLOWS TEMPO", true, 12.0f);
            break;
        case pp::ModDiffusion:
            deepA = std::make_unique<Dial> (p, ParamIDs::driftD, "Drift");
            deepB = std::make_unique<Dial> (p, ParamIDs::vol[m], "Effect Vol");
            deepLabel1 = "CAPTURE";
            deepPills = std::make_unique<Pills> (s, ParamIDs::capturePre, StringArray { "POST FX", "PRE FX" }, 12.0f);
            deepToggle1 = std::make_unique<ToggleChip> (s, ParamIDs::captureOverdub, "OVERDUB OFF", "OVERDUB ON", true, 12.0f);
            deepToggle2 = std::make_unique<ToggleChip> (s, ParamIDs::captureQuantise, "FREE LENGTH", "QUANTISE", true, 12.0f);
            deepToggle3 = std::make_unique<ToggleChip> (s, ParamIDs::trails, "BYPASS CUTS TAILS", "BYPASS KEEPS TRAILS", false, 12.0f);
            syncToggle = std::make_unique<ToggleChip> (s, ParamIDs::timeSync, "TIME IS FREE", "TIME FOLLOWS TEMPO", true, 12.0f);
            break;
        default:
            mainPills = std::make_unique<Pills> (s, ParamIDs::filterStyle, StringArray { "TILT", "LOW PASS", "HIGH PASS" }, 11.0f);
            mainPills->setStacked (true);
            deepA = std::make_unique<Dial> (p, ParamIDs::vol[m], "Effect Vol");
            deepB = std::make_unique<Dial> (p, ParamIDs::output, "Output");
            deepLabel1 = "SPARE SLOT";
            deepMenu = std::make_unique<MenuChip> (s, ParamIDs::spareFx);
            deepSlider1 = std::make_unique<LabelledSlider> (s, ParamIDs::spareAmount, "Spare amount");
            deepSlider2 = std::make_unique<LabelledSlider> (s, ParamIDs::spareKnob, "Spare knob");
            deepToggle1 = std::make_unique<ToggleChip> (s, ParamIDs::trueDry, "MIX AT ZERO: PEDAL COLOUR", "MIX AT ZERO: TRUE DRY", false, 12.0f);
            break;
    }
    for (Component* c : std::initializer_list<Component*> { knobDial.get(), amountDial.get(), deepA.get(), deepB.get(), mainPills.get(), deepPills.get(),
                                                           deepPills2.get(), deepToggle1.get(), deepToggle2.get(), deepToggle3.get(),
                                                           deepAction.get(), deepMenu.get(), deepSlider1.get(), deepSlider2.get(), syncToggle.get() })
        if (c != nullptr) addChildComponent (c);
    setView (View::Main);
}

void ModuleSection::setView (View v)
{
    view = v;
    const bool main = v == View::Main, deep = v == View::Deep;
    card.setVisible (main);
    grid.setVisible (main);
    for (Component* c : std::initializer_list<Component*> { knobDial.get(), amountDial.get(), mainPills.get() })
        if (c != nullptr) c->setVisible (main);
    for (Component* c : std::initializer_list<Component*> { deepA.get(), deepB.get(), deepPills.get(), deepPills2.get(), deepToggle1.get(),
                                                           deepToggle2.get(), deepToggle3.get(), deepAction.get(), deepMenu.get(),
                                                           deepSlider1.get(), deepSlider2.get(), syncToggle.get() })
        if (c != nullptr) c->setVisible (deep);
    resized();
    repaint();
}

void ModuleSection::resized()
{
    left.setBounds (208, 14, 28, 24);
    right.setBounds (246, 14, 28, 24);
    card.setBounds (16, 58, 258, 64);
    grid.setBounds (16, 134, 258, 62);
    if (knobDial) knobDial->setBounds (20, 210, 110, 128);
    if (mainPills)
    {
        amountDial->setBounds (20, 210, 110, 128);
        mainPills->setBounds (150, 236, 124, 98);
    }
    else amountDial->setBounds (160, 210, 110, 128);

    deepA->setBounds (20, 52, 110, 128);
    deepB->setBounds (160, 52, 110, 128);
    if (deepPills) deepPills->setBounds (16, 214, 258, 28);
    if (deepPills2) deepPills2->setBounds (16, 274, 258, 28);
    if (deepAction) deepAction->setBounds (16, 252, 258, 30);
    if (module == pp::ModDiffusion)
    {
        deepToggle1->setBounds (16, 252, 124, 30);
        deepToggle2->setBounds (150, 252, 124, 30);
        deepToggle3->setBounds (16, 292, 258, 28);
        syncToggle->setBounds (16, 330, 258, 28);
    }
    if (module == pp::ModMovement) syncToggle->setBounds (16, 316, 258, 28);
    if (deepMenu)
    {
        deepMenu->setBounds (16, 214, 258, 28);
        deepSlider1->setBounds (16, 252, 124, 34);
        deepSlider2->setBounds (150, 252, 124, 34);
        deepToggle1->setBounds (16, 298, 258, 30);
    }
    hinge.setBounds (16, view == View::Main ? 352 : 368, 258, view == View::Main ? 32 : 22);
}

void ModuleSection::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    const bool on = raw (proc, ParamIDs::on[module]) > 0.5f;
    g.setColour (on ? ff::module (module) : ff::faint);
    g.fillEllipse (16.0f, 20.0f, 12.0f, 12.0f);
    ff::label (g, names::modules[module].toUpperCase(), { 36, 14, 170, 18 }, Justification::centredLeft, ff::ink);
    ff::label (g, view == View::Deep ? "SLOT " + String (slot + 1) + " · DEEP" : "SLOT " + String (slot + 1), { 36, 32, 170, 14 },
               Justification::centredLeft, ff::dim, 11.0f);
    if (view == View::Deep)
    {
        ff::label (g, deepLabel1, { 16, 194, 258, 16 }, Justification::centredLeft, ff::dim, 11.0f);
        if (deepLabel2.isNotEmpty()) ff::label (g, deepLabel2, { 16, 254, 258, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    }
    if (view == View::Main && mainPills)
        ff::label (g, "FILTER STYLE", { 150, 214, 124, 16 }, Justification::centredLeft, ff::dim, 11.0f);
}

void ModuleSection::tick (double dt)
{
    // the dot follows the module's on switch
    const bool on = raw (proc, ParamIDs::on[module]) > 0.5f;
    if (on != lastOn) { lastOn = on; repaint(); }

    // Rate and Time show what they really do: Hz or a note value, milliseconds, a room, a speed
    const int fx = jlimit (0, 5, roundToInt (raw (proc, ParamIDs::fx[module])));
    if (knobDial && module == pp::ModMovement)
    {
        const float k = proc.laneActive (1) ? proc.laneValue (1) : raw (proc, ParamIDs::knob[1]);
        knobDial->setValueText (fx == 0 ? String (roundToInt (8.0f + 52.0f * k)) + " MS GAP"
                                        : (fx == 4 ? String ((k - 0.5f) * 24.0f, 1) + " ST" : names::rateText (k, raw (proc, ParamIDs::rateSync) > 0.5f)));
    }
    if (knobDial && module == pp::ModDiffusion)
    {
        const float k = proc.laneActive (2) ? proc.laneValue (2) : raw (proc, ParamIDs::knob[2]);
        knobDial->setValueText (names::timeText (fx, k, raw (proc, ParamIDs::timeSync) > 0.5f));
    }

    card.advance (dt);
    card.setValue (names::effects[module][jlimit (0, 5, roundToInt (raw (proc, ParamIDs::fx[module])))].toUpperCase());
    if (knobDial) knobDial->showGesture (proc.laneActive (module), proc.laneValue (module));
    amountDial->showGesture (proc.laneActive (4 + module), proc.laneValue (4 + module));
    left.setVisible (slot > 0);
    right.setVisible (slot < 3);
}

// =====================================================================================
// Footswitch cards for the Pedal page
// =====================================================================================
FootswitchCard::FootswitchCard (PrismProcessor& p, int s)
    : proc (p), sw (s),
      learn ("LEARN", [this] { proc.learnFootswitch (sw); }),
      reset ("DEFAULTS", [this] { proc.resetFootswitches(); })
{
    letter.setTextHeight (54.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    addAndMakeVisible (letter);
    addAndMakeVisible (learn);
    addAndMakeVisible (reset);
    if (s == 0)
        for (int m = 0; m < 4; ++m)
        {
            static const char* const shortNames[4] = { "CHAR", "MOVE", "DIFF", "TEX" };
            group.push_back (std::make_unique<ToggleChip> (p.apvts, ParamIDs::group[m], shortNames[m], String(), true, 11.0f));
            addAndMakeVisible (*group.back());
        }
}

void FootswitchCard::resized()
{
    letter.setBounds (16, 42, 258, 96);
    for (size_t m = 0; m < group.size(); ++m) group[m]->setBounds (16 + (int) m * 67, 290, 57, 24);
    learn.setBounds (16, 350, 124, 34);
    reset.setBounds (150, 350, 124, 34);
}

void FootswitchCard::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    ff::label (g, "FOOTSWITCH", { 16, 14, 258, 18 });

    const bool classic = proc.footLayout() == PrismProcessor::LayoutClassic;
    static const char* const tapClassic[4] = { "BYPASS", "TAP TEMPO", "GESTURE REC", "NEXT PRESET" };
    static const char* const holdClassic[4] = { "BYPASS ALL", "CAPTURE", "CLEAR GESTURES", "PREV PRESET" };
    static const char* const comboClassic[4] = { "A+B  DEEP PANEL", "", "C+D  NEXT PLUGIN, HOLD TO KEEP", "" };
    static const char* const holdModules[4] = { "BYPASS ALL", "CAPTURE", "GESTURE", "TAP TEMPO" };
    static const char* const comboModules[4] = { "A+B  NEXT PRESET", "", "C+D  NEXT PLUGIN, HOLD TO KEEP", "" };
    static const char* const noteModules[4] = { "", "HOLD AGAIN TO CLEAR A LOOP", "HOLD: RECORD, LOOP, ERASE", "HOLD, THEN TAP D IN TIME" };
    const String tapJob = classic ? String (tapClassic[sw]) : "SLOT " + String (sw + 1) + " ON/OFF";
    const String holdJob = classic ? holdClassic[sw] : holdModules[sw];
    const String comboJob = classic ? comboClassic[sw] : comboModules[sw];
    struct Row { const char* k; String v; };
    const Row rows[3] = { { "TAP", tapJob }, { "HOLD", holdJob }, { "LISTENS FOR", sends } };
    int y = 150;
    for (const auto& r : rows)
    {
        g.setColour (ff::ring);
        g.fillRect (16, y, 258, 2);
        ff::label (g, r.k, { 16, y + 4, 120, 30 }, Justification::centredLeft, ff::dim, 13.0f);
        ff::label (g, r.v, { 120, y + 4, 154, 30 }, Justification::centredRight, ff::ink, 14.0f);
        y += 38;
    }
    g.setColour (ff::ring);
    g.fillRect (16, y, 258, 2);
    if (classic)
    {
        if (sw == 0) ff::label (g, "BYPASS GROUP", { 16, y + 6, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
        else if (sw == 1) ff::label (g, "TAP CLEARS A PLAYING CAPTURE", { 16, y + 6, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
        if (comboJob.isNotEmpty()) ff::label (g, comboJob, { 16, y + (sw == 0 ? 56 : 6), 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    }
    else
    {
        // which module this footswitch reaches right now, in its colour
        const int m = proc.currentOrder()[(size_t) sw];
        static const char* const moduleNames[4] = { "CHARACTER", "MOVEMENT", "DIFFUSION", "TEXTURE" };
        g.setColour (ff::module (m));
        g.fillEllipse (16.0f, (float) y + 10.0f, 10.0f, 10.0f);
        ff::label (g, String ("NOW ") + moduleNames[m], { 34, y + 6, 240, 18 }, Justification::centredLeft, ff::ink, 11.0f);
        if (String (noteModules[sw]).isNotEmpty()) ff::label (g, noteModules[sw], { 16, y + 30, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
        if (comboJob.isNotEmpty()) ff::label (g, comboJob, { 16, y + 54, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    }
}

void FootswitchCard::tick()
{
    const auto& s = proc.pedals();
    const String t = "CH " + String (s.fsChannel[(size_t) sw]) + String::fromUTF8 (" · CC ") + String (s.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    const bool classic = proc.footLayout() == PrismProcessor::LayoutClassic;
    const int state = (classic ? 1 : 0) + 2 * proc.currentOrder()[(size_t) sw];
    if (state != lastState) { lastState = state; repaint(); }
    for (auto& chip : group) chip->setVisible (classic); // the bypass group only matters when A is a bypass
    learn.setLit (proc.learningWhat() == sw);
    learn.setText (proc.learningWhat() == sw ? "PRESS IT NOW" : "LEARN");
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    letter.setAlpha (down ? 0.6f : 1.0f);
}

// =====================================================================================
// Strip controls
// =====================================================================================
void BigButton::set (const String& m, const String& s, Colour c)
{
    if (m == mainText && s == smallText && c == borderColour) return;
    mainText = m; smallText = s; borderColour = c;
    repaint();
}

void BigButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (pressed ? ff::ring : ff::bg);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
    // two lines centred as a pair, whatever the button's height
    const float big = b.getHeight() < 50.0f ? 15.0f : 17.0f, small = b.getHeight() < 50.0f ? 10.5f : 11.0f;
    const float top = b.getCentreY() - (big + small + 2.0f) * 0.5f;
    ff::label (g, mainText, Rectangle<float> (b.getX(), top, b.getWidth(), big).toNearestInt(), Justification::centred, ff::ink, big);
    ff::label (g, smallText, Rectangle<float> (b.getX(), top + big + 2.0f, b.getWidth(), small).toNearestInt(), Justification::centred, ff::dim, small);
}

void BigButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    pressed = true; repaint();
    if (onDown) onDown();
}

void BigButton::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! pressed) return;
    pressed = false; repaint();
    if (onUp) onUp();
}

void MiniFoot::paint (Graphics& g)
{
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? ff::ink : ff::bg);
    g.fillRoundedRectangle (b, 8.0f);
    // Modules layout: lit in the colour of the module it switches, while that module is on
    bool lit = false;
    Colour col = ff::line;
    if (proc.footLayout() == PrismProcessor::LayoutModules)
    {
        const int m = proc.currentOrder()[(size_t) sw];
        lit = raw (proc, ParamIDs::on[m]) > 0.5f && raw (proc, ParamIDs::bypass) < 0.5f;
        if (lit) col = ff::module (m);
    }
    g.setColour (down ? ff::ink : col);
    g.drawRoundedRectangle (b.reduced (lit ? 1.0f : 0.0f), 8.0f, lit ? 3.0f : 2.0f);
    ff::label (g, String::charToString ((juce_wchar) ('A' + sw)), getLocalBounds(), Justification::centred, down ? ff::bg : ff::ink, 14.0f);
}

void FootLayoutLabel::paint (Graphics& g)
{
    const bool classic = proc.footLayout() == PrismProcessor::LayoutClassic;
    ff::label (g, String::fromUTF8 ("FOOTSWITCHES · ") + (classic ? "CLASSIC" : "MODULES") + "  v", getLocalBounds(), Justification::centredLeft, ff::dim, 12.0f);
}

void FootLayoutLabel::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    const int now = proc.footLayout();
    m.addItem (1, "Modules: tap A to D to switch slots 1 to 4", true, now == PrismProcessor::LayoutModules);
    m.addItem (2, "Classic: laid out like the original pedal", true, now == PrismProcessor::LayoutClassic);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1 || r == 2) { proc.setFootLayout (r - 1); repaint(); if (auto* p = getParentComponent()) p->repaint(); }
    });
}

void ExpressionMeter::paint (Graphics& g)
{
    const auto& t = names::expTargets();
    const int target = jlimit (0, (int) t.size() - 1, roundToInt (raw (proc, ParamIDs::expTarget)));
    String text = String::fromUTF8 ("EXPRESSION · ") + String (t[(size_t) target].label).toUpperCase() + "  v";
    if (proc.isCalibrating()) text = "ROCK HEEL TO TOE";
    else if (proc.learningWhat() == PrismProcessor::LearnExpression) text = "MOVE THE PEDAL";
    ff::label (g, text, { 0, 0, getWidth(), 18 }, Justification::centredLeft, proc.isCalibrating() ? ff::ink : ff::dim, 12.0f);
    const float v = proc.expressionLevel();
    const float x0 = 10.0f, w = (float) getWidth() - 20.0f, y = 30.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (x0, y, w, 10.0f, 5.0f);
    const bool fresh = nowSeconds() - proc.lastExpressionMove() < 1.5;
    g.setColour (fresh ? ff::ink : ff::dim);
    g.fillRoundedRectangle (x0, y, w * v, 10.0f, 5.0f);
    g.fillEllipse (x0 + w * v - 10.0f, y - 5.0f, 20.0f, 20.0f);
}

void ExpressionMeter::mouseDown (const MouseEvent&)
{
    PopupMenu targets;
    const auto& t = names::expTargets();
    const int target = roundToInt (raw (proc, ParamIDs::expTarget));
    for (int i = 0; i < (int) t.size(); ++i) targets.addItem (100 + i, t[(size_t) i].label, true, i == target);
    PopupMenu curves;
    const char* const curveNames[3] = { "Linear", "Slow start", "Fast start" };
    for (int i = 0; i < 3; ++i) curves.addItem (200 + i, curveNames[i], true, proc.pedals().curve == i);
    PopupMenu map;
    map.addItem (300, "Off", true, proc.pedals().pedalMapChannel == 0);
    for (int ch = 1; ch <= 16; ++ch) map.addItem (300 + ch, "Channel " + String (ch), true, proc.pedals().pedalMapChannel == ch);

    PopupMenu m;
    m.addSubMenu ("Expression moves", targets);
    m.addItem (1, "Calibrate heel and toe");
    m.addItem (2, "Learn expression pedal");
    m.addSubMenu ("Curve", curves);
    m.addSeparator();
    m.addSubMenu ("Original pedal MIDI map", map);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) proc.calibrateExpression();
        else if (r == 2) proc.learnExpression();
        else if (r >= 100 && r < 200) setParam (proc.apvts.getParameter (ParamIDs::expTarget), (float) (r - 100));
        else if (r >= 200 && r < 300) { proc.pedals().curve = r - 200; proc.pedals().save(); }
        else if (r >= 300) { proc.pedals().pedalMapChannel = r - 300; proc.pedals().save(); }
        repaint();
    });
}

void CaptureBar::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ff::ring);
    g.fillRoundedRectangle (b, 5.0f);
    const auto& cap = proc.getEngine().getCapture();
    const int st = cap.uiState.load();
    float frac = cap.uiPhase.load();
    Colour c = ff::modG;
    if (st == pp::Capture::Recording) { c = ff::red; frac = jmin (1.0f, cap.uiSeconds.load() / pp::Capture::kMaxSeconds); }
    else if (st == pp::Capture::Pad) c = ff::modB;
    else if (st == pp::Capture::Overdubbing) c = ff::coral;
    else if (st == pp::Capture::Empty) return;
    g.setColour (c);
    g.fillRoundedRectangle (b.withWidth (jmax (10.0f, b.getWidth() * frac)), 5.0f);
}

// =====================================================================================
// Pebble
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    auto& eng = proc.getEngine();
    level = eng.uiOutput.load();
    capState = eng.getCapture().uiState.load();
    watching = proc.gestureRecording();
    const bool bypassed = raw (proc, ParamIDs::bypass) > 0.5f;
    if (lastSound <= 0.0 || eng.uiInput.load() > 0.004f || capState != pp::Capture::Empty) lastSound = t;
    asleep = bypassed || t - lastSound > 8.0;

    // taps its foot whenever a footswitch goes down
    const int pulse = proc.uiFootPulse.load();
    if (pulse != lastPulse) { lastPulse = pulse; hop = 1.0f; }
    hop *= (float) std::exp (-dt / 0.12);
    flash *= (float) std::exp (-dt / 0.9);

    // sways in time with looping Gestures, leans with the expression pedal
    const float swayTarget = proc.gestureLoops() > 0 ? (float) std::sin (t * proc.currentBpm() / 60.0 * MathConstants<double>::pi) * 7.0f : 0.0f;
    sway += (swayTarget - sway) * (float) (1.0 - std::exp (-dt / 0.15));
    const bool expFresh = t - proc.lastExpressionMove() < 2.0;
    const float leanTarget = expFresh ? (proc.expressionLevel() - 0.5f) * 16.0f : 0.0f;
    lean += (leanTarget - lean) * (float) (1.0 - std::exp (-dt / 0.1));
    const float drift = jmax (raw (proc, ParamIDs::driftM), raw (proc, ParamIDs::driftD));
    wobble = drift > 0.5f ? (drift - 0.5f) * 2.0f : 0.0f;

    look *= (float) std::exp (-dt / 1.2);
    if (watching) look = (float) std::sin (t * 1.7) * 0.8f;

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;
    repaint();
}

void Pebble::paint (Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    const float s = a.getWidth() / 150.0f;
    const bool rec = capState == pp::Capture::Recording || capState == pp::Capture::Overdubbing;
    const bool pad = capState == pp::Capture::Pad, loop = capState == pp::Capture::Loop;
    const float bob = asleep ? 0.0f : 1.5f * (float) std::sin (now * 2.4);
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : (rec ? 1.06f : 1.0f); // puffs up holding its breath
    const float dy = bob - hop * 10.0f;
    const float tilt = degreesToRadians (sway + lean + wobble * 4.0f * (float) std::sin (now * 11.0));
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / std::sqrt (breathe))
                        .rotated (tilt, 0.0f, 50.0f * s).translated (a.getCentreX(), a.getCentreY() + 6.0f * s);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col) { Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2); g.setColour (col); g.fillPath (p, tf); };
    auto stroke = [&] (Path& p, float w, Colour col = ff::faceInk) { g.setColour (col); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf); };

    // the glow: red while Capture records, blue for a pad, green for a loop, a module's colour when it changes
    Colour glowColour = ff::teal;
    float glow = jlimit (0.0f, 1.0f, level * 3.0f);
    if (rec) { glowColour = ff::red; glow = 1.0f; }
    else if (pad) { glowColour = ff::modB; glow = 1.0f; }
    else if (loop) { glowColour = ff::modG; glow = 1.0f; }
    if (flash > 0.05f) { glowColour = flashColour; glow = jmax (glow, flash); }
    if (glow > 0.02f && ! asleep)
    {
        Path ringPath; ringPath.addEllipse (100 - 66, 100 - 56 + dy, 132, 112);
        stroke (ringPath, 5.0f, glowColour.withAlpha (0.7f * glow));
    }

    ell (107, 109 + dy, 58, 48, ff::blobShade);
    ell (100, 100 + dy, 58, 48, ff::blob);

    const bool eyesShut = asleep || blinking || pad;
    if (eyesShut)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, 94 + dy); lid.quadraticTo (ex, 102 + dy, ex + 12, 94 + dy);
            stroke (lid, 6.0f);
        }
    }
    else
    {
        const float pupil = watching ? 8.5f : 7.0f; // wide-eyed while it learns a Gesture
        for (float ex : { 80.0f, 120.0f })
        {
            ell (ex, 92 + dy, 14.0f, 14.0f, ff::eye);
            ell (ex + look * 5.0f, 94 + dy, pupil, pupil, ff::faceInk);
        }
    }

    if (asleep)
    {
        Path m; m.startNewSubPath (94, 124 + dy); m.lineTo (106, 124 + dy); stroke (m, 6.0f);
        g.setFont (ff::font (15.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 30.0f + ph * 8.0f, a.getY() + 8.0f - ph * 14.0f, 16.0f, 16.0f), Justification::centred, false);
        }
    }
    else if (rec) ell (100, 123 + dy, 6.0f, 6.0f, ff::faceInk);          // holding its breath
    else if (pad) { Path m; m.startNewSubPath (92, 122 + dy); m.quadraticTo (100, 127 + dy, 108, 122 + dy); stroke (m, 6.0f); }
    else if (level > 0.08f) ell (100, 124 + dy, 7.0f + 3.0f * level, 4.0f + 8.0f * jmin (1.0f, level), ff::faceInk);
    else { Path m; m.startNewSubPath (90, 120 + dy); m.quadraticTo (100, 130 + dy, 110, 120 + dy); stroke (m, 6.0f); }
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

PrismEditor::PrismEditor (PrismProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      inputMode (p.apvts, ParamIDs::inputMode, { "GUITAR", "SYNTH", "BASS" }),
      sync (p.apvts, ParamIDs::sync, "SYNC"),
      viewMain ("MAIN", [this] { setView (View::Main); }, 13.0f),
      viewDeep ("DEEP", [this] { setView (View::Deep); }, 13.0f),
      viewPedal ("PEDAL", [this] { setView (View::Pedal); }, 13.0f),
      pebble (p), captureBar (p),
      mix (p, ParamIDs::mix, "Mix", true, 92),
      bankA ("A", [this] { presetMenu(); }, 11.0f), bankB ("B", [this] { presetMenu(); }, 11.0f),
      bankC ("C", [this] { presetMenu(); }, 11.0f), bankD ("D", [this] { presetMenu(); }, 11.0f),
      expression (p)
{
    setLookAndFeel (&lnf);
    for (int m = 0; m < 4; ++m)
    {
        sections[(size_t) m] = std::make_unique<ModuleSection> (p, m);
        addAndMakeVisible (*sections[(size_t) m]);
        footCards[(size_t) m] = std::make_unique<FootswitchCard> (p, m);
        addChildComponent (*footCards[(size_t) m]);
        minis[(size_t) m] = std::make_unique<MiniFoot> (p, m);
        addAndMakeVisible (*minis[(size_t) m]);
    }
    footLayout = std::make_unique<FootLayoutLabel> (p);
    addAndMakeVisible (*footLayout);
    for (auto* c : std::initializer_list<Component*> { &inputMode, &tempoCard, &sync, &viewMain, &viewDeep, &viewPedal,
                                                       &pebble, &gestureButton, &captureButton, &bypassButton, &captureBar, &mix,
                                                       &presetCard, &bankA, &bankB, &bankC, &bankD, &expression,
                                                       &keepButton, &contextCard })
        addAndMakeVisible (c);

    // Keeper: click to log the moment, right-click for notes, export and the folder
    keepButton.set ("KEEP", "HOLD C+D", ff::line);
    keepButton.onDown = [this] { proc.keepNow(); };
    keepButton.onRightClick = [this] { keeperMenu(); };
    contextCard.setTextHeight (12.0f);
    contextCard.setMouseCursor (MouseCursor::PointingHandCursor);
    contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO");
    contextCard.onClick = [this] (const MouseEvent&)
    {
        proc.setKeeperLive (! proc.keeperLive());
        contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO");
        proc.uiMessage = proc.keeperLive() ? "LIVE" : "STUDIO";
        proc.uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
    };
    addChildComponent (noteEditor);
    noteEditor.setTextToShowWhenEmpty ("Optional, Enter to save", ff::faint);
    noteEditor.setFont (FontOptions (13.0f));
    noteEditor.setIndents (10, 8);
    noteEditor.setColour (TextEditor::backgroundColourId, ff::bg);
    noteEditor.setColour (TextEditor::textColourId, ff::ink);
    noteEditor.setColour (TextEditor::outlineColourId, ff::line);
    noteEditor.setColour (TextEditor::focusedOutlineColourId, ff::ink);
    noteEditor.setColour (CaretComponent::caretColourId, ff::ink);
    noteEditor.setInputRestrictions (200);
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    noteEditor.onTextChange = [this] { noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0; };

    tempoCard.setTextHeight (22.0f);
    tempoCard.setMouseCursor (MouseCursor::PointingHandCursor);
    tempoCard.onClick = [this] (const MouseEvent&) { proc.tap(); };
    presetCard.setTextHeight (24.0f);
    presetCard.setMouseCursor (MouseCursor::PointingHandCursor);
    presetCard.onClick = [this] (const MouseEvent& e) { if (e.mods.isPopupMenu()) presetMenu(); else proc.stepPreset (1); };

    gestureButton.onDown = [this] { proc.toggleGestureRecord(); };
    gestureButton.onRightClick = [this] { proc.clearGestures(); };
    captureButton.onDown = [this] { proc.captureDown(); };
    captureButton.onUp = [this] { proc.captureUp(); };
    bypassButton.onDown = [this] { proc.footDown (0); };
    bypassButton.onUp = [this] { proc.footUp (0); };

    setView (View::Main);
    setSize (1260, 660);
    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

PrismEditor::~PrismEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrismEditor::setView (View v)
{
    view = v;
    for (int m = 0; m < 4; ++m)
    {
        sections[(size_t) m]->setVisible (v != View::Pedal);
        sections[(size_t) m]->setView (v);
        footCards[(size_t) m]->setVisible (v == View::Pedal);
    }
    viewMain.setLit (v == View::Main);
    viewDeep.setLit (v == View::Deep);
    viewPedal.setLit (v == View::Pedal);
}

void PrismEditor::layoutModules()
{
    const auto order = proc.currentOrder();
    for (int s = 0; s < 4; ++s)
    {
        auto& sec = *sections[(size_t) order[(size_t) s]];
        sec.setBounds (20 + s * 310, 76, 290, 400);
        sec.setSlot (s);
        footCards[(size_t) s]->setBounds (20 + s * 310, 76, 290, 400);
    }
    lastOrder = order;
}

void PrismEditor::presetMenu()
{
    PopupMenu save, load;
    for (int b = 0; b < 4; ++b)
    {
        PopupMenu sb, lb;
        for (int i = 0; i < PrismProcessor::kPerBank; ++i)
        {
            const int slot = b * PrismProcessor::kPerBank + i;
            const String name = String::charToString ((juce_wchar) ('A' + b)) + " " + String (i + 1).paddedLeft ('0', 2);
            sb.addItem (1000 + slot, name + (proc.hasPreset (slot) ? "  (replace)" : ""), true, slot == proc.selectedPreset());
            lb.addItem (2000 + slot, name, proc.hasPreset (slot), slot == proc.selectedPreset());
        }
        save.addSubMenu ("Bank " + String::charToString ((juce_wchar) ('A' + b)), sb);
        load.addSubMenu ("Bank " + String::charToString ((juce_wchar) ('A' + b)), lb, true);
    }
    PopupMenu m;
    const int sel = proc.selectedPreset();
    if (sel >= 0) m.addItem (1, "Save over the current preset");
    m.addSubMenu ("Save to", save);
    m.addSubMenu ("Load", load);
    if (sel >= 0) m.addItem (2, "Clear the current preset");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetCard), [this, sel] (int r)
    {
        if (r == 1) proc.savePreset (sel);
        else if (r == 2) proc.clearPreset (sel);
        else if (r >= 2000) proc.loadPreset (r - 2000);
        else if (r >= 1000) proc.savePreset (r - 1000);
    });
}

void PrismEditor::mouseDown (const MouseEvent&) { proc.touchKnob ({}); }

void PrismEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepButton), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0;
            repaint (150, 500, 200, 80);
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
    });
}

void PrismEditor::closeNote (bool save)
{
    if (save) proc.addKeeperNote (noteNumber, noteEditor.getText());
    if (save && noteEditor.getText().trim().isNotEmpty())
    {
        proc.uiMessage = "NOTED";
        proc.uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
    }
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
    repaint (150, 500, 200, 80);
}

void PrismEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.04f));
    g.drawText ("PRISM PATCH", 20, 20, 160, 40, Justification::centredLeft, false);
    ff::label (g, "COLOURFUL CHAINS, CAPTURED CHARACTER", { 178, 20, 300, 40 });
    ff::label (g, "INPUT", { 480, 20, 50, 40 });
    ff::label (g, "TEMPO", { 764, 20, 56, 40 });

    g.setColour (ff::face);
    g.fillRoundedRectangle (20.0f, 492.0f, 1220.0f, 152.0f, 12.0f);
    if (noteEditor.isVisible())
        ff::label (g, "KEPT No. " + String (noteNumber) + String::fromUTF8 (" · ADD A NOTE?"), { 156, 508, 180, 18 }, Justification::centredLeft, ff::ink, 12.0f);
    else
    {
        ff::label (g, "PEBBLE", { 156, 506, 180, 18 });
        ff::label (g, mood, { 156, 524, 180, 32 }, Justification::centredLeft, ff::ink, 22.0f);
        ff::label (g, moodSub, { 156, 556, 180, 16 }, Justification::centredLeft, ff::dim, 12.0f);
    }
    ff::label (g, captureText, { 360, 602, 260, 18 }, Justification::centredLeft, ff::dim, 12.0f);
    ff::label (g, "PRESET", { 782, 510, 110, 18 });
}

void PrismEditor::resized()
{
    inputMode.setBounds (534, 24, 210, 32);
    tempoCard.setBounds (822, 20, 80, 40);
    sync.setBounds (912, 24, 66, 32);
    viewMain.setBounds (996, 24, 74, 32);
    viewDeep.setBounds (1081, 24, 74, 32);
    viewPedal.setBounds (1166, 24, 74, 32);
    layoutModules();

    // the strip: at least 10 px between buttons in a group, 24 px between groups
    pebble.setBounds (30, 504, 112, 120);
    keepButton.setBounds (156, 586, 100, 44);
    contextCard.setBounds (268, 586, 68, 44);
    noteEditor.setBounds (156, 530, 180, 32);
    gestureButton.setBounds (360, 512, 124, 58);
    captureButton.setBounds (496, 512, 124, 58);
    captureBar.setBounds (360, 586, 260, 10);
    mix.setBounds (644, 498, 114, 140);
    presetCard.setBounds (782, 532, 104, 52);
    const int bw = 20, bg = 8;
    bankA.setBounds (782, 598, bw, 24);
    bankB.setBounds (782 + (bw + bg), 598, bw, 24);
    bankC.setBounds (782 + 2 * (bw + bg), 598, bw, 24);
    bankD.setBounds (782 + 3 * (bw + bg), 598, bw, 24);
    for (int k = 0; k < 4; ++k) minis[(size_t) k]->setBounds (910 + k * 46, 532, 34, 34);
    footLayout->setBounds (910, 506, 180, 22);
    expression.setBounds (910, 578, 172, 46);
    bypassButton.setBounds (1106, 532, 118, 64);
}

void PrismEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;

    tempoCard.advance (dt);
    presetCard.advance (dt);
    tempoCard.setValue (String (roundToInt (proc.currentBpm())));
    sync.setAlpha (proc.hostSynced() || raw (proc, ParamIDs::sync) < 0.5f ? 1.0f : 0.6f);

    if (proc.currentOrder() != lastOrder) layoutModules();
    for (auto& s : sections) s->tick (dt);
    for (auto& c : footCards) c->tick();
    mix.showGesture (proc.laneActive (3), proc.laneValue (3));

    // A+B on the footswitch flips between the Main and Deep pages
    const int deepToggle = proc.uiDeepToggle.load();
    if (deepToggle != lastDeepToggle) { lastDeepToggle = deepToggle; setView (view == View::Deep ? View::Main : View::Deep); }

    // Pebble glows in a module's colour and glances towards it when its effect changes
    const auto order = proc.currentOrder();
    String event;
    for (int m = 0; m < 4; ++m)
    {
        const int fx = roundToInt (raw (proc, ParamIDs::fx[m]));
        if (lastFx[(size_t) m] >= 0 && fx != lastFx[(size_t) m])
        {
            const int slot = (int) (std::find (order.begin(), order.end(), m) - order.begin());
            pebble.react (ff::module (m), ((float) slot - 1.5f) / 1.5f);
            event = names::effects[m][fx].toUpperCase();
        }
        lastFx[(size_t) m] = fx;
    }
    if (event.isNotEmpty()) proc.uiMessage = event, proc.uiMessageAt = now;

    // status lines
    const auto& cap = proc.getEngine().getCapture();
    const int cs = cap.uiState.load();
    const float secs = cap.uiSeconds.load();
    String m, sub;
    if (now - proc.uiMessageAt < 2.5 && proc.uiMessage.isNotEmpty()) { m = proc.uiMessage; sub = "PEBBLE IS ON IT"; }
    else if (cs == pp::Capture::Recording) { m = "HOLDING BREATH"; sub = "CAPTURE IS RECORDING"; }
    else if (cs == pp::Capture::Pad) { m = "DRIFTING OFF"; sub = "SHORT HOLD MADE A PAD"; }
    else if (cs == pp::Capture::Loop) { m = "GROOVING"; sub = "CAPTURE IS LOOPING"; }
    else if (proc.gestureRecording()) { m = "WATCHING"; sub = "TURN A KNOB TO RECORD IT"; }
    else if (proc.gestureLoops() > 0) { m = "SWAYING"; sub = String (proc.gestureLoops()) + (proc.gestureLoops() == 1 ? " GESTURE LOOPING" : " GESTURES LOOPING"); }
    else if (raw (proc, ParamIDs::bypass) > 0.5f) { m = "NAPPING"; sub = "BYPASSED"; }
    else
    {
        int count = 0;
        for (int k = 0; k < 4; ++k) count += (raw (proc, ParamIDs::on[k]) > 0.5f && roundToInt (raw (proc, ParamIDs::fx[k])) != pp::kOff) ? 1 : 0;
        m = "LISTENING";
        sub = count == 4 ? "ALL FOUR MODULES ON" : String (count) + (count == 1 ? " MODULE ON" : " MODULES ON");
    }
    String ct;
    const bool pre = raw (proc, ParamIDs::capturePre) > 0.5f;
    if (cs == pp::Capture::Empty) ct = String (pre ? "PRE FX" : "POST FX") + String::fromUTF8 (" · 30 S MAX · SAVES WITH YOUR SET");
    else if (cs == pp::Capture::Recording) ct = "RECORDING " + String (secs, 1) + " S";
    else if (cs == pp::Capture::Overdubbing) ct = "OVERDUBBING";
    else ct = (cs == pp::Capture::Pad ? "PAD " : "LOOP ") + String (secs, 1) + String::fromUTF8 (" S · TAP TO CLEAR");
    if (m != mood || sub != moodSub || ct != captureText) { mood = m; moodSub = sub; captureText = ct; repaint (140, 500, 480, 130); }

    // the big buttons
    if (proc.gestureRecording()) gestureButton.set ("RECORDING", "MOVE ANY KNOB", ff::red);
    else if (proc.gestureLoops() > 0) gestureButton.set ("PLAYING", String (proc.gestureLoops()) + " LOOPS " + String::fromUTF8 ("· RIGHT-CLICK CLEARS"), ff::ink);
    else gestureButton.set ("GESTURE", "TAP TO RECORD", ff::line);
    if (cs == pp::Capture::Recording) captureButton.set ("RECORDING", proc.captureLatched() ? "CLICK TO STOP" : "RELEASE TO LOOP", ff::red);
    else if (cs == pp::Capture::Overdubbing) captureButton.set ("OVERDUB", "RELEASE TO PLAY", ff::coral);
    else if (cs == pp::Capture::Pad) captureButton.set ("SUSTAIN PAD", "TAP TO CLEAR", ff::modB);
    else if (cs == pp::Capture::Loop) captureButton.set ("LOOPING", raw (proc, ParamIDs::captureOverdub) > 0.5f ? "HOLD TO OVERDUB" : "TAP TO CLEAR", ff::modG);
    else captureButton.set ("CAPTURE", "HOLD, OR CLICK TWICE", ff::line);
    const bool bypassed = raw (proc, ParamIDs::bypass) > 0.5f;
    bool allGrouped = true;
    for (int k = 0; k < 4; ++k) allGrouped = allGrouped && raw (proc, ParamIDs::group[k]) > 0.5f;
    bypassButton.set (bypassed ? "BYPASSED" : "ON", allGrouped ? "TAP TO BYPASS" : "DUAL BYPASS", bypassed ? ff::line : ff::ink);

    // presets
    const int sel = proc.selectedPreset();
    presetCard.setValue (sel < 0 ? "NONE" : String::charToString ((juce_wchar) ('A' + sel / PrismProcessor::kPerBank)) + " "
                                                 + String (sel % PrismProcessor::kPerBank + 1).paddedLeft ('0', 2));
    const int bank = sel < 0 ? -1 : sel / PrismProcessor::kPerBank;
    bankA.setLit (bank == 0); bankB.setLit (bank == 1); bankC.setLit (bank == 2); bankD.setLit (bank == 3);

    // Keeper: flip to KEPT, Pebble nods, offer a note for ten seconds without taking the keyboard
    contextCard.advance (dt);
    const int kp = proc.uiKeepPulse.load();
    if (kp != lastKeepPulse)
    {
        lastKeepPulse = kp;
        keptUntil = now + 1.8;
        contextCard.setValue ("KEPT");
        pebble.react (ff::modY, 0.0f);
        noteNumber = 0;
        noteEditor.clear();
        noteEditor.setVisible (true);
        noteUntil = now + 10.0;
        repaint (150, 500, 200, 80);
    }
    if (noteNumber == 0 && proc.lastKeeper() > 0 && noteEditor.isVisible()) { noteNumber = proc.lastKeeper(); repaint (150, 500, 200, 80); }
    if (keptUntil > 0.0 && now > keptUntil) { keptUntil = 0.0; contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO"); }
    if (noteEditor.isVisible() && now > noteUntil && ! noteEditor.hasKeyboardFocus (true)) closeNote (false);
    keepButton.set (proc.learningWhat() == PrismProcessor::LearnKeeper ? "PRESS IT" : "KEEP",
                    proc.learningWhat() == PrismProcessor::LearnKeeper ? "YOUR KEEPER SWITCH" : "HOLD C+D", ff::line);

    pebble.update (now, dt);
    captureBar.repaint();
    expression.repaint();
    for (auto& mf : minis) mf->repaint();
}
