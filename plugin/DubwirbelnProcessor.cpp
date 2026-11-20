#include "plugin/DubwirbelnProcessor.h"

namespace dw {

DubwirbelnProcessor::DubwirbelnProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    RollConfig config;
    config.expression.curve = curveTemplate(CurveType::Flat);
    scheduler_.setConfig(config);
    notes_.reserve(256);
}

void DubwirbelnProcessor::prepareToPlay(double sampleRate, int) {
    scheduler_.setSampleRate(sampleRate);
    scheduler_.reset();
}

bool DubwirbelnProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DubwirbelnProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    // Only Note-On/Off of the trigger note on any channel matter (spec Part 9).
    const int triggerNote = triggerNote_.load();
    notes_.clear();
    for (const auto meta : midi) {
        const auto message = meta.getMessage();
        if (message.getNoteNumber() != triggerNote) continue;
        if (message.isNoteOn())
            notes_.push_back({meta.samplePosition, true, message.getVelocity()});
        else if (message.isNoteOff())
            notes_.push_back({meta.samplePosition, false, 0});
    }
    midi.clear();

    Transport transport;  // no host: 120 BPM, stopped
    if (auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            if (const auto bpm = position->getBpm()) transport.bpm = *bpm;
            transport.playing = position->getIsPlaying();
            if (const auto ppq = position->getPpqPosition()) transport.ppqPosition = *ppq;
            else transport.playing = false;  // no beat position: no grid to quantize to
        }
    }

    // The triggers have no sample engine to play them yet.
    scheduler_.process(buffer.getNumSamples(), notes_, transport);
    buffer.clear();
}

juce::AudioProcessorEditor* DubwirbelnProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

void DubwirbelnProcessor::getStateInformation(juce::MemoryBlock&) {}
void DubwirbelnProcessor::setStateInformation(const void*, int) {}

}  // namespace dw

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dw::DubwirbelnProcessor(); }
