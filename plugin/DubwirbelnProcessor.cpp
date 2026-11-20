#include "plugin/DubwirbelnProcessor.h"

namespace dw {

namespace {

constexpr int kParameterVersion = 1;

juce::ParameterID id(const char* name) { return juce::ParameterID{name, kParameterVersion}; }

std::unique_ptr<juce::AudioParameterChoice> choice(const char* name, const juce::String& label,
                                                   const juce::StringArray& options, int defaultIndex) {
    return std::make_unique<juce::AudioParameterChoice>(id(name), label, options, defaultIndex);
}

std::unique_ptr<juce::AudioParameterFloat> percent(const char* name, const juce::String& label, float defaultValue) {
    return std::make_unique<juce::AudioParameterFloat>(id(name), label, juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
                                                       defaultValue, juce::AudioParameterFloatAttributes().withLabel("%"));
}

}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DubwirbelnProcessor::createLayout() {
    // Index order: see ParameterValues in engine/Preset.h.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(choice("rollType", "Roll Type", {"None", "Build", "Drop", "Rush", "Burst", "Wave"}, 0));
    layout.add(percent("intensity", "Intensity", 50.0f));
    layout.add(percent("blend", "Blend", 50.0f));
    layout.add(choice("windowBars", "Window", {"4 Bars", "2 Bars", "1 Bar", "1/2 Bar"}, 0));
    layout.add(choice("mode", "Mode", {"Trigger", "Hold"}, 0));
    layout.add(choice("quantize", "Quantize", {"Off", "1/16", "1/8", "1/4", "1 Bar"}, 0));
    layout.add(percent("humanize", "Humanize", 0.0f));
    layout.add(percent("swing", "Swing", 0.0f));
    layout.add(choice("velocityFollow", "Velocity Follow", {"Off", "Velocity", "Velocity + Intensity"}, 2));
    return layout;
}

DubwirbelnProcessor::DubwirbelnProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "dubwirbeln", createLayout()) {
    rollType_ = apvts_.getRawParameterValue("rollType");
    intensity_ = apvts_.getRawParameterValue("intensity");
    blend_ = apvts_.getRawParameterValue("blend");
    window_ = apvts_.getRawParameterValue("windowBars");
    mode_ = apvts_.getRawParameterValue("mode");
    quantize_ = apvts_.getRawParameterValue("quantize");
    humanize_ = apvts_.getRawParameterValue("humanize");
    swing_ = apvts_.getRawParameterValue("swing");
    velocityFollow_ = apvts_.getRawParameterValue("velocityFollow");

    // The humanize seed is assigned once, when the instance is created, and then stored (spec Part 7).
    storedPreset_ = defaultPreset(static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt64()));
    notes_.reserve(256);
}

void DubwirbelnProcessor::prepareToPlay(double sampleRate, int) {
    scheduler_.setSampleRate(sampleRate);
    scheduler_.reset();
    configApplied_ = false;
}

bool DubwirbelnProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

ParameterValues DubwirbelnProcessor::readParameters() const {
    ParameterValues v;
    v.rollType = juce::roundToInt(rollType_->load());
    v.intensity = intensity_->load();
    v.blend = blend_->load();
    v.window = juce::roundToInt(window_->load());
    v.mode = juce::roundToInt(mode_->load());
    v.quantize = juce::roundToInt(quantize_->load());
    v.humanize = humanize_->load();
    v.swing = swing_->load();
    v.velocityFollow = juce::roundToInt(velocityFollow_->load());
    return v;
}

void DubwirbelnProcessor::writeParameters(const ParameterValues& v) {
    auto set = [this](const char* name, float plainValue) {
        if (auto* p = apvts_.getParameter(name)) p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    };
    set("rollType", static_cast<float>(v.rollType));
    set("intensity", static_cast<float>(v.intensity));
    set("blend", static_cast<float>(v.blend));
    set("windowBars", static_cast<float>(v.window));
    set("mode", static_cast<float>(v.mode));
    set("quantize", static_cast<float>(v.quantize));
    set("humanize", static_cast<float>(v.humanize));
    set("swing", static_cast<float>(v.swing));
    set("velocityFollow", static_cast<float>(v.velocityFollow));
}

// Rebuilds the scheduler config when a parameter or the stored preset part changed.
void DubwirbelnProcessor::syncConfig() {
    const auto values = readParameters();
    const int revision = presetRevision_.load();
    if (configApplied_ && revision == appliedRevision_ && values == appliedValues_) return;

    const juce::SpinLock::ScopedTryLockType lock(presetLock_);
    if (!lock.isLocked()) return;  // the message thread is writing: try again in the next block

    Preset preset = storedPreset_;
    applyParameterValues(preset, values);
    scheduler_.setConfig(toRollConfig(preset));
    appliedValues_ = values;
    appliedRevision_ = revision;
    configApplied_ = true;
}

void DubwirbelnProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    syncConfig();

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
    if (auto* host = getPlayHead()) {
        if (const auto position = host->getPosition()) {
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

Preset DubwirbelnProcessor::currentPreset() const {
    Preset preset;
    {
        const juce::SpinLock::ScopedLockType lock(presetLock_);
        preset = storedPreset_;
    }
    applyParameterValues(preset, readParameters());
    return preset;
}

void DubwirbelnProcessor::getStateInformation(juce::MemoryBlock& destData) {
    PluginState state;
    state.triggerNote = triggerNote_.load();
    state.preset = currentPreset();
    const auto text = pluginStateToJson(state);
    destData.replaceAll(text.data(), text.size());
}

void DubwirbelnProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0) return;
    const std::string text(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes));
    auto parsed = pluginStateFromJson(text);
    if (!parsed.state) return;  // unknown or damaged state: keep what we have

    const auto values = toParameterValues(parsed.state->preset);
    triggerNote_.store(parsed.state->triggerNote);
    {
        const juce::SpinLock::ScopedLockType lock(presetLock_);
        storedPreset_ = std::move(parsed.state->preset);
    }
    writeParameters(values);
    presetRevision_.fetch_add(1);
}

}  // namespace dw

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dw::DubwirbelnProcessor(); }
