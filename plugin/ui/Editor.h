#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/DubwirbelnProcessor.h"
#include "plugin/ui/Controls.h"
#include "plugin/ui/PanelLook.h"

namespace dw::ui {

// The editor in the look of dubcycling (spec Part 3): a header with the preset and the toolbar
// (Undo, Redo, Variate, Clear), the roll and the samples on the left, the playback settings with
// the trigger note on the right, and a status bar. The Roll Painter and the Trigger Preview
// follow in their own steps.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(DubwirbelnProcessor& processor);
    ~Editor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // The actions behind the buttons. File dialogs call these; tests call them directly.
    void loadPresetFile(const juce::File& file);
    void saveAs(const juce::File& file);
    void save();
    void loadSampleFile(Sample slot, const juce::File& file);
    void variate();
    void undoVariate();
    void redoVariate();
    void clearRoll();             // asks first when the preset has unsaved changes
    void clearRollNow();
    void toggleLearn();

    void timerCallbackForTest() { refresh(); }  // what the 10 Hz timer does

    // What the status bar shows now, and what the header shows as preset name.
    juce::String statusText() const;
    juce::String presetLabel() const;

private:
    enum class Tone { Info, Good, Warn };

    void timerCallback() override;
    void refresh();
    void showStatus(const juce::String& text, Tone tone = Tone::Info);
    void paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) const;
    void chooseSample(Sample slot);
    void noteEdited();

    DubwirbelnProcessor& proc_;
    PanelLook look_;

    // header and toolbar
    juce::Label logo_, presetName_;
    juce::TextButton load_{"LOAD"}, save_{"SAVE"}, saveAs_{"SAVE AS"};
    juce::TextButton undo_{"UNDO"}, redo_{"REDO"}, variate_{"VARIATE"}, clear_{"CLEAR"};

    // roll
    ChoiceRow rollType_;
    PercentControl intensity_, blend_;

    // samples
    juce::Label sampleCaption_[2], sampleName_[2];
    juce::TextButton sampleLoad_[2];

    // playback
    juce::Label modeCaption_, windowCaption_, quantizeCaption_, followCaption_, noteCaption_;
    ChoiceRow mode_, window_, quantize_, follow_;
    PercentControl humanize_, swing_;
    juce::Label note_;
    juce::TextButton learn_{"LEARN"};

    juce::Label status_;
    juce::String statusMessage_;
    Tone statusTone_ = Tone::Info;
    juce::uint32 statusUntil_ = 0;
    juce::String hint_;

    std::unique_ptr<juce::FileChooser> chooser_;

    // layout of the panels, filled by resized() and used by paint()
    juce::Rectangle<int> rollPanel_, samplesPanel_, playbackPanel_;
};

}  // namespace dw::ui
