#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/PluginState.h"
#include "engine/RollScheduler.h"
#include "engine/SampleEngine.h"
#include "engine/TriggerInput.h"
#include "engine/Variate.h"

namespace dw {

// Monophonic instrument. MIDI and host transport drive the roll scheduler; the sample engine and
// UI follow (see docs/SPEC.md), so the output is still silent.
//
// The scalar preset values are host parameters (automatable). Everything else of the preset (name,
// velocity curve, seeds, gesture, samples) and the trigger note are plugin state; the state is
// saved as JSON (engine/PluginState.h), with the preset in the format of schemas/preset.schema.json.
class DubwirbelnProcessor : public juce::AudioProcessor {
public:
    DubwirbelnProcessor();
    ~DubwirbelnProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dubwirbeln"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // The preset as it currently sounds: stored fields plus the current parameter values.
    Preset currentPreset() const;

    // Loads WAV, AIFF, FLAC or MP3 into a sample slot (message thread). The audio is kept in memory
    // only: saving samples comes with the preset container.
    bool loadSample(Sample slot, const juce::File& file);

    // Saves and loads a .dwpreset (preset, samples). Not real-time safe: call from the message thread.
    juce::Result savePreset(const juce::File& file) const;
    juce::Result loadPreset(const juce::File& file);

    // Variate, Undo and Clear (spec Part 10). Message thread only.
    bool variate();                 // false when there is no roll
    bool undoVariate();             // only the last pass
    bool redoVariate();
    bool canUndoVariate() const { return history_.canUndo(); }
    bool canRedoVariate() const { return history_.canRedo(); }
    void clearRoll();               // not undoable, drops the variate undo state

    // The trigger note (spec Part 9): default C1, stored in the plugin state, not in the preset.
    int triggerNote() const { return input_.note(); }
    void setTriggerNote(int note) { input_.setNote(note); }
    // MIDI Learn: the next Note-On on any channel becomes the trigger note (it does not start a roll).
    void startMidiLearn() { input_.startLearn(); }
    void cancelMidiLearn() { input_.cancelLearn(); }
    bool isMidiLearning() const { return input_.isLearning(); }

    // Dirty: the preset differs from the one that was last saved or loaded (spec Part 11).
    // Clear only asks for confirmation when the preset is dirty (spec Part 10).
    bool isDirty() const;
    bool needsClearConfirmation() const { return isDirty(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ParameterValues readParameters() const;
    void writeParameters(const ParameterValues& values);
    void syncConfig();
    void rebuildStoredConfig();  // the caller holds presetLock_
    SamplePtr decodeSample(const Bytes& bytes);
    Preset presetForSave(FileMap& samples) const;
    void installPreset(Preset preset, const FileMap& files);
    void applyEditedPreset(const Preset& preset);  // takes over fields edited by Variate, Undo or Clear

    juce::AudioProcessorValueTreeState apvts_;

    // Raw parameter values, read on the audio thread.
    std::atomic<float>* rollType_ = nullptr;
    std::atomic<float>* intensity_ = nullptr;
    std::atomic<float>* blend_ = nullptr;
    std::atomic<float>* window_ = nullptr;
    std::atomic<float>* mode_ = nullptr;
    std::atomic<float>* quantize_ = nullptr;
    std::atomic<float>* humanize_ = nullptr;
    std::atomic<float>* swing_ = nullptr;
    std::atomic<float>* velocityFollow_ = nullptr;

    // The non-parameter part of the preset. Written on the message thread, read (try-lock) on the audio thread.
    mutable juce::SpinLock presetLock_;
    Preset storedPreset_;
    // storedPreset_ as the scheduler and the sample engine need it, built on the message thread
    // (under presetLock_) whenever the stored preset changes, so the audio thread only copies it.
    RollConfig storedConfig_;
    std::array<SampleParams, 2> storedSampleParams_;
    std::atomic<int> presetRevision_{1};

    // Audio thread only.
    RollConfig rtConfig_;  // storedConfig_ with the current parameter values
    int appliedRevision_ = 0;
    ParameterValues appliedValues_;
    bool configApplied_ = false;

    // Decoded samples. Owned here so that the audio thread never drops the last reference.
    std::array<SamplePtr, 2> loadedSamples_;
    std::vector<SamplePtr> retiredSamples_;
    juce::AudioFormatManager formats_;

    // The files of the loaded samples as they are saved (WAV or FLAC bytes), by slot.
    struct StoredFile {
        std::string path;  // archive path, e.g. "samples/kick.wav"
        Bytes bytes;
    };
    mutable juce::CriticalSection filesLock_;
    std::array<std::optional<StoredFile>, 2> storedFiles_;

    VariateHistory history_;

    // What "dirty" is measured against. Guarded by baselineLock_ (hosts may save the state from any thread).
    mutable juce::CriticalSection baselineLock_;
    mutable Preset baseline_;
    void markClean();  // the current preset becomes the baseline

    RollScheduler scheduler_{44100.0};
    SampleEngine engine_{44100.0};
    TriggerInput input_;  // trigger note and MIDI Learn (lock-free)
    NoteList notes_;     // fixed capacity: processBlock never allocates
    EventList events_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DubwirbelnProcessor)
};

}  // namespace dw
