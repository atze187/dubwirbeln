#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/PanelLook.h"

namespace dw::ui {

// A row of buttons for a choice parameter. The button of the current value is lit. Clicking one sets
// the parameter as a complete gesture, so the host sees it as one change.
class ChoiceRow final : public juce::Component {
public:
    // `labels` are the button texts in the order of the parameter's choices. `colourFor` (optional)
    // gives a button its own colour.
    ChoiceRow(juce::RangedAudioParameter& parameter, const juce::StringArray& labels,
              std::function<juce::Colour(int)> colourFor = {});

    void resized() override;
    void setButtonTooltip(int index, const juce::String& text);
    int selected() const { return selected_; }
    juce::TextButton* button(int index) { return buttons_[index]; }

private:
    void select(int index);
    void update(float plainValue);

    juce::RangedAudioParameter& parameter_;
    juce::OwnedArray<juce::TextButton> buttons_;
    int selected_ = -1;
    juce::ParameterAttachment attachment_;
};

// A caption, a row of LED segments and an LCD readout for a percent parameter.
class PercentControl final : public juce::Component {
public:
    PercentControl(juce::RangedAudioParameter& parameter, const juce::String& caption, juce::Colour colour,
                   const juce::String& tooltip = {});

    void resized() override;
    juce::Slider& slider() { return slider_; }
    juce::String readout() const { return value_.getText(); }

private:
    juce::Label caption_, value_;
    juce::Slider slider_;
    juce::SliderParameterAttachment attachment_;
};

}  // namespace dw::ui
