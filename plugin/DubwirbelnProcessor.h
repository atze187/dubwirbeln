#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>

#include "engine/PluginState.h"
#include "engine/RollScheduler.h"
#include "engine/SampleEngine.h"

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ParameterValues readParameters() const;
    void writeParameters(const ParameterValues& values);
    void syncConfig();

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
    std::atomic<int> presetRevision_{1};
    int appliedRevision_ = 0;
    ParameterValues appliedValues_;
    bool configApplied_ = false;

    // Decoded samples. Owned here so that the audio thread never drops the last reference.
    std::array<SamplePtr, 2> loadedSamples_;
    std::vector<SamplePtr> retiredSamples_;
    juce::AudioFormatManager formats_;

    RollScheduler scheduler_{44100.0};
    SampleEngine engine_{44100.0};
    std::atomic<int> triggerNote_{kDefaultTriggerNote};
    std::vector<NoteEvent> notes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DubwirbelnProcessor)
};

}  // namespace dw
