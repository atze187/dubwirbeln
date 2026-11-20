#include "plugin/DubwirbelnProcessor.h"

#include "engine/Sha256.h"

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
    {
        const juce::SpinLock::ScopedLockType lock(presetLock_);
        rebuildStoredConfig();
    }
    rtConfig_.expression.curve.reserve(kMaxCurvePoints);  // the audio thread copies into this: no allocation
    formats_.registerBasicFormats();
    markClean();  // a new instance holds an unmodified default preset
}

void DubwirbelnProcessor::prepareToPlay(double sampleRate, int) {
    scheduler_.setSampleRate(sampleRate);
    scheduler_.reset();
    engine_.setSampleRate(sampleRate);
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

// Message thread, with presetLock_ held: builds what the audio thread needs from storedPreset_.
void DubwirbelnProcessor::rebuildStoredConfig() {
    storedConfig_ = toRollConfig(storedPreset_);
    for (size_t i = 0; i < 2; ++i) {
        const auto& slot = i == 0 ? storedPreset_.sampleA : storedPreset_.sampleB;
        storedSampleParams_[i] = slot ? SampleParams{slot->tuneSemitones, slot->start, slot->end, slot->levelDb}
                                      : SampleParams{};
    }
}

// Audio thread, no allocation: takes over the stored preset part when it changed and applies the
// current parameter values (host automation) on top of it.
void DubwirbelnProcessor::syncConfig() {
    const auto values = readParameters();
    const int revision = presetRevision_.load();
    const bool storedChanged = !configApplied_ || revision != appliedRevision_;
    if (!storedChanged && values == appliedValues_) return;

    if (storedChanged) {
        const juce::SpinLock::ScopedTryLockType lock(presetLock_);
        if (!lock.isLocked()) return;  // the message thread is writing: try again in the next block
        rtConfig_ = storedConfig_;     // copies into the curve capacity reserved in the constructor
        for (const auto slot : {Sample::A, Sample::B}) {
            const size_t i = slot == Sample::A ? 0 : 1;
            engine_.setSample(slot, loadedSamples_[i], storedSampleParams_[i]);
        }
        appliedRevision_ = revision;
    }
    applyParameterValues(rtConfig_, values);
    scheduler_.setConfig(rtConfig_);
    appliedValues_ = values;
    configApplied_ = true;
}

void DubwirbelnProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    syncConfig();

    // Only Note-On/Off of the trigger note on any channel matter (spec Part 9); while MIDI Learn is
    // on, the next Note-On becomes the trigger note instead.
    notes_.clear();
    for (const auto meta : midi) {
        const auto message = meta.getMessage();
        const bool on = message.isNoteOn();    // velocity above 0
        const bool off = message.isNoteOff();  // includes Note-On with velocity 0
        if (!on && !off) continue;
        switch (input_.handle(on, off, message.getNoteNumber())) {
            case TriggerInput::Action::NoteOn: notes_.push_back({meta.samplePosition, true, message.getVelocity()}); break;
            case TriggerInput::Action::NoteOff: notes_.push_back({meta.samplePosition, false, 0}); break;
            case TriggerInput::Action::Learned:
            case TriggerInput::Action::Ignore: break;
        }
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

    scheduler_.process(buffer.getNumSamples(), notes_.data(), static_cast<int>(notes_.size()), transport, events_);
    if (buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    engine_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples(), events_.data(),
                    static_cast<int>(events_.size()));
}

namespace {

// Reads a decoded sample (at most two channels) from a reader.
SamplePtr readSampleData(juce::AudioFormatReader& reader) {
    constexpr juce::int64 kMaxFrames = 10'000'000;  // about 3.5 minutes at 48 kHz
    if (reader.lengthInSamples <= 0 || reader.lengthInSamples > kMaxFrames || reader.numChannels == 0) return nullptr;

    const auto frames = static_cast<int>(reader.lengthInSamples);
    const int channels = static_cast<int>(std::min<unsigned>(reader.numChannels, 2u));
    juce::AudioBuffer<float> buffer(channels, frames);
    if (!reader.read(&buffer, 0, frames, 0, true, channels > 1)) return nullptr;

    auto data = std::make_shared<SampleData>();
    data->sampleRate = reader.sampleRate;
    data->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    if (channels > 1) data->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    return data;
}

// Encodes a decoded sample as 24-bit FLAC (used for formats the container does not store: AIFF, MP3).
bool encodeFlac24(const SampleData& data, Bytes& out) {
    const int channels = data.right.empty() ? 1 : 2;
    const auto frames = static_cast<int>(data.left.size());
    juce::AudioBuffer<float> buffer(channels, frames);
    buffer.copyFrom(0, 0, data.left.data(), frames);
    if (channels > 1) buffer.copyFrom(1, 0, data.right.data(), frames);

    juce::MemoryBlock block;
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(block, false);
        auto writer = flac.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate(data.sampleRate)
                                                       .withNumChannels(channels)
                                                       .withBitsPerSample(24)
                                                       .withQualityOptionIndex(5));
        if (!writer) return false;
        if (!writer->writeFromAudioSampleBuffer(buffer, 0, frames)) return false;
    }  // the writer flushes into `block` when it is destroyed
    out.assign(static_cast<const std::uint8_t*>(block.getData()),
               static_cast<const std::uint8_t*>(block.getData()) + block.getSize());
    return !out.empty();
}

}  // namespace

SamplePtr DubwirbelnProcessor::decodeSample(const Bytes& bytes) {
    if (bytes.empty()) return nullptr;
    std::unique_ptr<juce::InputStream> stream =
        std::make_unique<juce::MemoryInputStream>(bytes.data(), bytes.size(), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(std::move(stream)));
    return reader ? readSampleData(*reader) : nullptr;
}

bool DubwirbelnProcessor::loadSample(Sample slot, const juce::File& file) {
    juce::MemoryBlock raw;
    if (!file.loadFileAsData(raw) || raw.getSize() == 0) return false;

    // WAV and FLAC are stored as they are; AIFF, MP3 and the rest become 24-bit FLAC.
    const auto extension = file.getFileExtension().toLowerCase();
    Bytes bytes;
    std::string path;
    if (extension == ".wav" || extension == ".flac") {
        bytes.assign(static_cast<const std::uint8_t*>(raw.getData()),
                     static_cast<const std::uint8_t*>(raw.getData()) + raw.getSize());
        path = "samples/" + file.getFileName().toStdString();
    } else {
        std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
        const auto decoded = reader ? readSampleData(*reader) : nullptr;
        if (!decoded || !encodeFlac24(*decoded, bytes)) return false;
        path = "samples/" + file.getFileNameWithoutExtension().toStdString() + ".flac";
    }

    auto data = decodeSample(bytes);  // what a reload of the saved preset will play
    if (!data) return false;

    SampleSlot stored;
    stored.name = file.getFileName().toStdString();
    stored.sha256 = sha256Hex(bytes);
    stored.file = path;

    {
        const juce::SpinLock::ScopedLockType lock(presetLock_);
        auto& target = slot == Sample::A ? storedPreset_.sampleA : storedPreset_.sampleB;
        if (target) {  // keep tune, start, end and level of the slot
            stored.tuneSemitones = target->tuneSemitones;
            stored.start = target->start;
            stored.end = target->end;
            stored.levelDb = target->levelDb;
        }
        target = stored;
        rebuildStoredConfig();
        auto& loaded = loadedSamples_[slot == Sample::A ? 0 : 1];
        if (loaded) retiredSamples_.push_back(std::move(loaded));
        // The audio thread has long moved on from samples retired several loads ago.
        if (retiredSamples_.size() > 4) retiredSamples_.erase(retiredSamples_.begin());
        loaded = std::move(data);
    }
    {
        const juce::ScopedLock lock(filesLock_);
        storedFiles_[slot == Sample::A ? 0 : 1] = StoredFile{path, std::move(bytes)};
    }
    presetRevision_.fetch_add(1);
    return true;
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

// The preset as it is saved, with the files of its samples. A slot without sample data cannot be
// saved and is dropped; two different files with the same name get distinct archive paths.
Preset DubwirbelnProcessor::presetForSave(FileMap& samples) const {
    Preset preset = currentPreset();
    const juce::ScopedLock lock(filesLock_);
    std::string firstPath;
    for (int i = 0; i < 2; ++i) {
        auto& slot = i == 0 ? preset.sampleA : preset.sampleB;
        const auto& file = storedFiles_[static_cast<size_t>(i)];
        if (!slot) continue;
        if (!file) {
            slot.reset();
            continue;
        }
        std::string path = file->path;
        if (i == 1 && path == firstPath && samples.at(path) != file->bytes) {
            const auto dot = path.find_last_of('.');
            path = path.substr(0, dot) + "_B" + path.substr(dot);
        }
        samples[path] = file->bytes;
        slot->file = path;
        if (i == 0) firstPath = path;
    }
    return preset;
}

// Replaces the stored preset, its samples and the parameters (message thread).
void DubwirbelnProcessor::installPreset(Preset preset, const FileMap& files) {
    std::array<SamplePtr, 2> decoded;
    std::array<std::optional<StoredFile>, 2> stored;
    for (int i = 0; i < 2; ++i) {
        auto& slot = i == 0 ? preset.sampleA : preset.sampleB;
        if (!slot) continue;
        const auto it = files.find(slot->file);
        if (it != files.end()) decoded[static_cast<size_t>(i)] = decodeSample(it->second);
        if (decoded[static_cast<size_t>(i)]) stored[static_cast<size_t>(i)] = StoredFile{slot->file, it->second};
        else slot.reset();  // no readable audio for this slot
    }

    const auto values = toParameterValues(preset);
    {
        const juce::SpinLock::ScopedLockType lock(presetLock_);
        storedPreset_ = std::move(preset);
        rebuildStoredConfig();
        for (size_t i = 0; i < 2; ++i) {
            if (loadedSamples_[i]) retiredSamples_.push_back(std::move(loadedSamples_[i]));
            loadedSamples_[i] = std::move(decoded[i]);
        }
        if (retiredSamples_.size() > 4) retiredSamples_.erase(retiredSamples_.begin(), retiredSamples_.end() - 4);
    }
    {
        const juce::ScopedLock lock(filesLock_);
        storedFiles_ = std::move(stored);
    }
    history_.clear();  // a loaded preset starts without undo state
    writeParameters(values);
    presetRevision_.fetch_add(1);
    markClean();  // a loaded preset is not dirty (parameters may have rounded its values, so measure after)
}

void DubwirbelnProcessor::markClean() {
    const Preset now = currentPreset();
    const juce::ScopedLock lock(baselineLock_);
    baseline_ = now;
}

bool DubwirbelnProcessor::isDirty() const {
    const Preset now = currentPreset();
    const juce::ScopedLock lock(baselineLock_);
    return !presetsEquivalent(now, baseline_);
}

// Takes over the fields Variate, Undo and Clear change; the samples stay as they are.
void DubwirbelnProcessor::applyEditedPreset(const Preset& preset) {
    const auto values = toParameterValues(preset);
    {
        const juce::SpinLock::ScopedLockType lock(presetLock_);
        storedPreset_.gesture = preset.gesture;
        storedPreset_.curve = preset.curve;
        storedPreset_.variationSeed = preset.variationSeed;
        rebuildStoredConfig();
    }
    writeParameters(values);
    presetRevision_.fetch_add(1);
}

bool DubwirbelnProcessor::variate() {
    Preset preset = currentPreset();
    const auto before = variateSnapshot(preset);
    const auto passSeed = static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt64());
    if (!applyVariate(preset, passSeed)) return false;
    history_.record(before, variateSnapshot(preset));
    applyEditedPreset(preset);
    return true;
}

bool DubwirbelnProcessor::undoVariate() {
    Preset preset = currentPreset();
    if (!history_.undo(preset)) return false;
    applyEditedPreset(preset);
    return true;
}

bool DubwirbelnProcessor::redoVariate() {
    Preset preset = currentPreset();
    if (!history_.redo(preset)) return false;
    applyEditedPreset(preset);
    return true;
}

void DubwirbelnProcessor::clearRoll() {
    Preset preset = currentPreset();
    dw::clearRoll(preset);
    history_.clear();
    applyEditedPreset(preset);
}

void DubwirbelnProcessor::getStateInformation(juce::MemoryBlock& destData) {
    PluginState state;
    state.triggerNote = input_.note();
    FileMap samples;
    state.preset = presetForSave(samples);
    {
        const juce::ScopedLock lock(baselineLock_);
        state.baseline = baseline_;  // so a project that is saved with unsaved changes still shows them
    }
    const auto written = pluginStateToBytes(state, samples);
    if (!written.ok()) return;
    destData.replaceAll(written.bytes.data(), written.bytes.size());
}

void DubwirbelnProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0) return;
    const auto* begin = static_cast<const std::uint8_t*>(data);
    auto parsed = pluginStateFromBytes(Bytes(begin, begin + sizeInBytes));
    if (!parsed.state) return;  // unknown or damaged state: keep what we have

    input_.cancelLearn();
    input_.setNote(parsed.state->triggerNote);
    installPreset(std::move(parsed.state->preset), parsed.samples);
    if (parsed.state->baseline) {
        const juce::ScopedLock lock(baselineLock_);
        baseline_ = *parsed.state->baseline;
    }
}

juce::Result DubwirbelnProcessor::savePreset(const juce::File& file) const {
    PresetArchive archive;
    archive.preset = presetForSave(archive.samples);
    const auto written = writePresetArchive(archive);
    if (!written.ok()) return juce::Result::fail(written.error);
    if (!file.replaceWithData(written.bytes.data(), written.bytes.size()))
        return juce::Result::fail("cannot write " + file.getFullPathName());
    {
        const juce::ScopedLock lock(baselineLock_);
        baseline_ = archive.preset;  // saved: the preset is clean again
    }
    return juce::Result::ok();
}

juce::Result DubwirbelnProcessor::loadPreset(const juce::File& file) {
    juce::MemoryBlock raw;
    if (!file.loadFileAsData(raw)) return juce::Result::fail("cannot read " + file.getFullPathName());
    const auto* begin = static_cast<const std::uint8_t*>(raw.getData());
    auto read = readPresetArchive(Bytes(begin, begin + raw.getSize()));
    if (!read.archive) return juce::Result::fail(read.error);
    installPreset(std::move(read.archive->preset), read.archive->samples);
    return juce::Result::ok();
}

}  // namespace dw

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dw::DubwirbelnProcessor(); }
