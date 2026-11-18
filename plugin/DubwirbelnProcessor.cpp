#include "plugin/DubwirbelnProcessor.h"

namespace dw {

DubwirbelnProcessor::DubwirbelnProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void DubwirbelnProcessor::prepareToPlay(double, int) {}

bool DubwirbelnProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DubwirbelnProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();
}

juce::AudioProcessorEditor* DubwirbelnProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

void DubwirbelnProcessor::getStateInformation(juce::MemoryBlock&) {}
void DubwirbelnProcessor::setStateInformation(const void*, int) {}

}  // namespace dw

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dw::DubwirbelnProcessor(); }
