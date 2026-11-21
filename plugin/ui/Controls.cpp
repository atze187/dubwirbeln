#include "plugin/ui/Controls.h"

namespace dw::ui {

using namespace panel;

ChoiceRow::ChoiceRow(juce::RangedAudioParameter& parameter, const juce::StringArray& labels,
                     std::function<juce::Colour(int)> colourFor)
    : parameter_(parameter), attachment_(parameter, [this](float value) { update(value); }) {
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = buttons_.add(new juce::TextButton(labels[i]));
        b->setClickingTogglesState(false);
        const auto colour = colourFor ? colourFor(i) : kAccent;
        b->setColour(juce::TextButton::buttonOnColourId, colour.darker(0.55f));
        b->setColour(juce::TextButton::textColourOnId, colour.brighter(0.3f));
        b->onClick = [this, i] { select(i); };
        addAndMakeVisible(b);
    }
    attachment_.sendInitialUpdate();
}

void ChoiceRow::resized() {
    auto r = getLocalBounds();
    const int n = buttons_.size();
    if (n == 0) return;
    constexpr int kGap = 3;
    const int w = (r.getWidth() - kGap * (n - 1)) / n;
    for (auto* b : buttons_) {
        b->setBounds(r.removeFromLeft(w));
        r.removeFromLeft(kGap);
    }
}

void ChoiceRow::setButtonTooltip(int index, const juce::String& text) {
    if (auto* b = buttons_[index]) b->setTooltip(text);
}

void ChoiceRow::select(int index) {
    attachment_.setValueAsCompleteGesture(static_cast<float>(index));
}

void ChoiceRow::update(float plainValue) {
    selected_ = juce::roundToInt(plainValue);
    for (int i = 0; i < buttons_.size(); ++i) buttons_[i]->setToggleState(i == selected_, juce::dontSendNotification);
}

PercentControl::PercentControl(juce::RangedAudioParameter& parameter, const juce::String& caption,
                               juce::Colour colour, const juce::String& tooltip)
    : attachment_(parameter, slider_) {
    styleCaption(caption_, caption);
    addAndMakeVisible(caption_);

    slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setColour(juce::Slider::trackColourId, colour);
    slider_.setColour(juce::Slider::backgroundColourId, kEdge);
    slider_.setColour(juce::Slider::thumbColourId, kText);
    slider_.setDoubleClickReturnValue(true, parameter.convertFrom0to1(parameter.getDefaultValue()));
    slider_.setTooltip(tooltip);
    addAndMakeVisible(slider_);

    styleLcd(value_, kAmber, juce::Justification::centredRight);
    value_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(value_);

    auto showValue = [this] { value_.setText(juce::String(slider_.getValue(), 1) + " %", juce::dontSendNotification); };
    slider_.onValueChange = showValue;
    showValue();
}

void PercentControl::resized() {
    auto r = getLocalBounds();
    caption_.setBounds(r.removeFromTop(12));
    value_.setBounds(r.removeFromRight(64));
    r.removeFromRight(6);
    slider_.setBounds(r);
}

}  // namespace dw::ui
