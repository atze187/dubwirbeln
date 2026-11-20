#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/RollScheduler.h"

namespace dw {

// Monophonic instrument. MIDI and host transport drive the roll scheduler; the sample engine,
// parameters and UI follow (see docs/SPEC.md), so the output is still silent.
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

private:
    RollScheduler scheduler_{44100.0};
    std::atomic<int> triggerNote_{36};  // C1, plugin state (spec Part 9)
    std::vector<NoteEvent> notes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DubwirbelnProcessor)
};

}  // namespace dw
