#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/RollGenerator.h"

namespace dw::ui {

// The look of the dub* plugins, taken from dubcycling: dark flat surfaces, 1 px edges, monospace
// capitals, LCD-style readouts (amber on near-black), one blue accent.
namespace panel {

inline const juce::Colour kWindow{0xff14161a};
inline const juce::Colour kStrip{0xff191c21};
inline const juce::Colour kWave{0xff101215};
inline const juce::Colour kLcd{0xff0b0d0f};
inline const juce::Colour kEdge{0xff2a2e35};
inline const juce::Colour kEdgeLight{0xff3a3f47};
inline const juce::Colour kButton{0xff23272e};
inline const juce::Colour kText{0xffd7dbe0};
inline const juce::Colour kDim{0xff7b828d};
inline const juce::Colour kAmber{0xffffb347};
inline const juce::Colour kGreen{0xff7fe3a0};
inline const juce::Colour kWarn{0xffe0816a};
inline const juce::Colour kAccent{0xff5aa9e6};

juce::FontOptions mono(float height, bool bold = false);

// Gives a label the look of an LCD readout.
void styleLcd(juce::Label& label, juce::Colour text, juce::Justification justification = juce::Justification::centred);

// Small dim caption above or beside a control.
void styleCaption(juce::Label& label, const juce::String& text,
                  juce::Justification justification = juce::Justification::centredLeft);

// One colour per roll type, taken from the pad palette of dubcycling.
juce::Colour rollColour(RollType type);

}  // namespace panel

// Flat buttons, monospace button text, and sliders drawn as a row of LED-style segments.
class PanelLook final : public juce::LookAndFeel_V4 {
public:
    PanelLook();

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted,
                              bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    int getSliderThumbRadius(juce::Slider&) override { return 0; }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override;
};

}  // namespace dw::ui
