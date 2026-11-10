// "Atelier" look: dark instrument panel with wood cheeks, knurled and chrome knobs,
// a brass-capped big knob, faders, LED buttons, copper pads and orange VFD windows.
// Everything is drawn in code (no bitmaps), so it stays sharp at any scale.

#pragma once

#include <JuceHeader.h>

namespace atelier
{
//==============================================================================
namespace colours
{
    const juce::Colour panelTop   { 0xff2b2e32 }, panel { 0xff222428 }, panelLow { 0xff1d1f22 };
    const juce::Colour inset      { 0xff1c1e21 }, insetLow { 0xff17181b };
    const juce::Colour text       { 0xffe6e7e9 }, label { 0xffc3c6ca }, dim { 0xff9a9ea4 }, faint { 0xff7d8085 };
    const juce::Colour vfd        { 0xffff9b5e }, vfdDim { 0xffd9875a }, vfdBg { 0xff160904 }, valueText { 0xffe58a55 };
    const juce::Colour ledOn      { 0xffff3b2a }, ledOff { 0xff3a1512 };
    const juce::Colour copper     { 0xffe0703a }, copperLight { 0xfff7a36e }, copperDark { 0xff8f3c18 };
    const juce::Colour warn       { 0xffffb347 }, ok { 0xff8fd18a };
    const juce::Colour groupLine  { 0x33ffffff };
    const juce::Colour silk       { 0xffb4b8be }, silkDim { 0xff80848a };   // printed scales and legends
}

//==============================================================================
// Fonts bundled with the plugin (SIL Open Font License, see Resources/Fonts).
struct Fonts
{
    static juce::Font title (float h);    // Cormorant SC SemiBold
    static juce::Font label (float h);    // Barlow Semi Condensed SemiBold
    static juce::Font bold (float h);     // Barlow Semi Condensed Bold
    static juce::Font mono (float h);     // Share Tech Mono
};

//==============================================================================
// Drawing helpers
void drawWoodCheek (juce::Graphics&, juce::Rectangle<float> area, bool leftSide, int seed);
void drawVents (juce::Graphics&, juce::Rectangle<float> area);
void drawInset (juce::Graphics&, juce::Rectangle<float> area);                       // darker column band
void drawGroup (juce::Graphics&, juce::Rectangle<float> area, const juce::String& bracketLabel);
void drawBracketLabel (juce::Graphics&, juce::Rectangle<float> row, const juce::String& text);
void drawLed (juce::Graphics&, juce::Point<float> centre, float diameter, bool on);
void drawVfdWindow (juce::Graphics&, juce::Rectangle<float> area);
void drawHardButton (juce::Graphics&, juce::Rectangle<float> area, bool down, bool highlighted);
void drawCopper (juce::Graphics&, juce::Rectangle<float> area, float corner, bool lit, bool down);

enum class KnobStyle { Knurled, Chrome, Brass };
void drawKnob (juce::Graphics&, juce::Rectangle<float> area, float proportion, KnobStyle);
void drawSwitchKnob (juce::Graphics&, juce::Point<float> centre, float radius, float angleFromTop);   // pointer knob of a rotary switch

//==============================================================================
class Look : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
};

//==============================================================================
// Widgets

// Rotary knob with a printed scale around it, its label below and its value underneath.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& labelText, KnobStyle, int maxValue = 254);

    void setValue (int v);                          // no callback
    int getValue() const                            { return (int) slider.getValue(); }
    std::function<void (int)> onChange;
    void setLabel (const juce::String& t)           { labelText = t; repaint(); }
    void setValueText (std::function<juce::String (int)> f) { valueText = std::move (f); repaint(); }
    void setTooltip (const juce::String& t)         { slider.setTooltip (t); }
    void setScale (const juce::String& low, const juce::String& high) { scaleLow = low; scaleHigh = high; repaint(); }

    juce::Slider slider;                            // public: for host-parameter attachments

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int knobSize (KnobStyle s)     { return s == KnobStyle::Brass ? 130 : (s == KnobStyle::Knurled ? 56 : 54); }
    static constexpr int topRoom (KnobStyle s)      { return s == KnobStyle::Brass ? 0 : 8; }      // for the printed scale
    static constexpr int height (KnobStyle s)       { return s == KnobStyle::Brass ? 166 : topRoom (s) + knobSize (s) + 41; }
    static constexpr int centreY (KnobStyle s)      { return topRoom (s) + knobSize (s) / 2; }

private:
    juce::String labelText, scaleLow { "0" }, scaleHigh { "10" };
    KnobStyle style;
    std::function<juce::String (int)> valueText;
    bool updating = false;
};

// Vertical fader (envelopes).
class Fader : public juce::Component
{
public:
    explicit Fader (const juce::String& labelText);
    void setValue (int v);
    std::function<void (int)> onChange;
    void setTooltip (const juce::String& t)         { slider.setTooltip (t); }
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String labelText;
    bool updating = false;
};

// Orange VFD window with previous / next arrows; a click on the value opens the list.
class Selector : public juce::Component
{
public:
    struct Item { int value; juce::String text; };

    Selector (const juce::String& labelText, bool labelOnLeft = false);
    void setItems (std::vector<Item> items);
    void setValue (int v);                          // shows "Value n" for values outside the list
    int getValue() const                            { return value; }
    std::function<void (int)> onChange;
    void setLabel (const juce::String& t)           { labelText = t; repaint(); }
    void setTooltip (const juce::String& t)         { tip = t; window.setTooltip (t); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Window : juce::Component, juce::SettableTooltipClient
    {
        std::function<void()> onClick;
        std::function<void (int)> onWheel;
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& d) override
        {
            if (onWheel && std::abs (d.deltaY) > 0.0f) onWheel (d.deltaY > 0 ? -1 : 1);
        }
    };
    struct Arrow : juce::Button
    {
        explicit Arrow (bool left) : juce::Button (left ? "previous" : "next"), isLeft (left) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool isLeft;
    };

    void step (int delta);
    void showList();

    juce::String labelText, tip;
    bool labelLeft;
    std::vector<Item> items;
    int value = 0;
    Window window;
    Arrow prev { true }, next { false };
};

// A number in an orange display window (tempo): drag up or down on it, or use the wheel.
class VfdNumber : public juce::Component
{
public:
    VfdNumber (const juce::String& labelText, int minValue, int maxValue);
    void setValueText (std::function<juce::String (int)> f) { valueText = std::move (f); repaint(); }
    void setTooltip (const juce::String& t)         { slider.setTooltip (t); }

    juce::Slider slider;                            // invisible, over the window: for host-parameter attachments

    static constexpr int windowHeight = 46, height = windowHeight + 20;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String labelText;
    std::function<juce::String (int)> valueText;
};

// Rotary switch: a pointer knob with a printed legend at each position; the chosen one is lit.
// Click a legend to choose it, drag or use the wheel to turn, click the knob for the full list.
class RotarySwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Item { int value; juce::String legend, name; };   // an empty legend is printed only when chosen

    explicit RotarySwitch (const juce::String& labelText);
    void setItems (std::vector<Item> items, float spanDegrees = 0.0f);   // 0: from the number of positions
    void setLegendSize (float h)                    { legendSize = h; repaint(); }
    void setValue (int v);
    int getValue() const                            { return value; }
    std::function<void (int)> onChange;
    void setLabel (const juce::String& t)           { labelText = t; repaint(); }

    static constexpr float radius = 20.0f, legendRoom = 26.0f;            // room above the knob for legends
    static constexpr int centreY = (int) (legendRoom + radius);           // knob centre from the top

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Point<float> centre() const               { return { (float) getWidth() * 0.5f, (float) centreY }; }
    float angleOf (int index) const;
    juce::String shownLegend (int index) const;
    juce::Rectangle<float> legendBox (int index) const;
    int indexOf (int v) const;
    void choose (int index);
    void showList();

    juce::String labelText;
    std::vector<Item> items;
    float span = 90.0f, legendSize = 10.0f;
    int value = 0, dragFrom = -1;
    bool dragged = false, pickedLegend = false;
};

// Push button with an LED: inside on the right (wide) or underneath (small).
class LedButton : public juce::Button
{
public:
    enum class LedPlace { Right, Below, None };
    LedButton (const juce::String& buttonText, LedPlace place = LedPlace::Right);
    void setLit (bool b)                            { lit = b; repaint(); }
    bool isLit() const                              { return lit; }
    void setTextColour (juce::Colour c)             { textColour = c; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    LedPlace place;
    bool lit = false;
    std::optional<juce::Colour> textColour;
};

// Big copper button (Send, Generate, Keep): glows like a lit pad.
class CopperButton : public juce::Button
{
public:
    explicit CopperButton (const juce::String& buttonText) : juce::Button (buttonText) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    float fontHeight = 14.0f;
};

// Square rubber pad with its label underneath (generator families).
class Pad : public juce::Button
{
public:
    explicit Pad (const juce::String& buttonText) : juce::Button (buttonText) {}
    void setLit (bool b)                            { lit = b; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool lit = false;
};

// Wood side cheek (procedural grain, cached per size).
class WoodCheek : public juce::Component
{
public:
    WoodCheek (bool left, int seed) : isLeft (left), grainSeed (seed) { setInterceptsMouseClicks (false, false); setOpaque (true); }
    void paint (juce::Graphics& g) override;

private:
    bool isLeft;
    int grainSeed;
    juce::Image cache;   // regenerated only when the size or the display scale changes
};

} // namespace atelier
