#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "AllocGuard.h"
#include "engine/Sha256.h"
#include "engine/PresetArchive.h"
#include "plugin/DubwirbelnProcessor.h"

using namespace dw;

// These tests run the real processor (without a host and without the VST3 wrapper): samples,
// state, .dwpreset files and the audio path.
namespace {

struct JuceRuntime {
    juce::ScopedJuceInitialiser_GUI init;
};

juce::File tempDir() {
    static const auto dir = [] {
        auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dubwirbeln_tests");
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }();
    return dir;
}

// A short test tone as a WAV, AIFF or FLAC file. `frequency` makes different content.
juce::File writeAudio(const juce::String& relativePath, double frequency, juce::AudioFormat& format, int bits = 16) {
    auto file = tempDir().getChildFile(relativePath);
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::AudioBuffer<float> buffer(1, 4410);  // 0.1 s at 44.1 kHz
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * frequency * i / 44100.0)));

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    REQUIRE(stream != nullptr);
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate(44100.0)
                                                     .withNumChannels(1)
                                                     .withBitsPerSample(bits));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
    writer.reset();
    return file;
}

juce::File wavFile(const juce::String& path, double frequency = 440.0) {
    juce::WavAudioFormat wav;
    return writeAudio(path, frequency, wav);
}

void setChoice(DubwirbelnProcessor& p, const char* id, int index) {
    for (auto* parameter : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameter); rp && rp->paramID == id)
            rp->setValueNotifyingHost(rp->convertTo0to1(static_cast<float>(index)));
}

// Renders `blocks` blocks of 512 samples, with a Note-On of the trigger note in the first one.
float renderPeak(DubwirbelnProcessor& p, int blocks, bool noteOn = true) {
    p.prepareToPlay(48000.0, 512);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        if (noteOn && b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(127)), 0);
        p.processBlock(buffer, midi);
        peak = std::max({peak, buffer.getMagnitude(0, 0, 512), buffer.getMagnitude(1, 0, 512)});
    }
    return peak;
}

juce::MemoryBlock stateOf(DubwirbelnProcessor& p) {
    juce::MemoryBlock block;
    p.getStateInformation(block);
    return block;
}

}  // namespace

TEST_CASE("a loaded WAV is stored as it is and plays", "[processor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    const auto file = wavFile("tone.wav");
    REQUIRE(p.loadSample(Sample::A, file));

    const auto preset = p.currentPreset();
    REQUIRE(preset.sampleA.has_value());
    CHECK(preset.sampleA->name == "tone.wav");
    CHECK(preset.sampleA->file == "samples/tone.wav");
    juce::MemoryBlock original;
    REQUIRE(file.loadFileAsData(original));
    const auto* data = static_cast<const std::uint8_t*>(original.getData());
    CHECK(preset.sampleA->sha256 == sha256Hex(Bytes(data, data + original.getSize())));
    CHECK_FALSE(preset.sampleB.has_value());

    setChoice(p, "rollType", 1);  // Build
    CHECK(renderPeak(p, 8) > 0.05f);
}

TEST_CASE("without a roll type the processor stays silent", "[processor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("tone.wav")));
    CHECK(renderPeak(p, 8) == 0.0f);
}

TEST_CASE("without samples nothing sounds", "[processor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setChoice(p, "rollType", 1);
    CHECK(renderPeak(p, 8) == 0.0f);
}

TEST_CASE("a file that is not audio is refused", "[processor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    auto bogus = tempDir().getChildFile("bogus.wav");
    bogus.replaceWithText("this is not audio");
    CHECK_FALSE(p.loadSample(Sample::A, bogus));
    CHECK_FALSE(p.loadSample(Sample::A, tempDir().getChildFile("missing.wav")));
    CHECK_FALSE(p.currentPreset().sampleA.has_value());
}

TEST_CASE("AIFF is converted to FLAC for storage", "[processor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    juce::AiffAudioFormat aiff;
    REQUIRE(p.loadSample(Sample::A, writeAudio("loop.aiff", 220.0, aiff)));

    const auto preset = p.currentPreset();
    REQUIRE(preset.sampleA.has_value());
    CHECK(preset.sampleA->name == "loop.aiff");
    CHECK(preset.sampleA->file == "samples/loop.flac");

    const auto block = stateOf(p);
    const auto* begin = static_cast<const std::uint8_t*>(block.getData());
    const auto read = pluginStateFromBytes(Bytes(begin, begin + block.getSize()));
    INFO(read.error);
    REQUIRE(read.state.has_value());
    REQUIRE(read.samples.count("samples/loop.flac") == 1);
    const auto& flac = read.samples.at("samples/loop.flac");
    CHECK(sha256Hex(flac) == read.state->preset.sampleA->sha256);
    CHECK(flac.size() > 4);
    CHECK(std::string(flac.begin(), flac.begin() + 4) == "fLaC");
}

TEST_CASE("the state carries parameters, trigger note and samples to a new instance", "[processor][state]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    REQUIRE(a.loadSample(Sample::A, wavFile("kick.wav", 100.0)));
    REQUIRE(a.loadSample(Sample::B, wavFile("snare.wav", 800.0)));
    setChoice(a, "rollType", 3);  // Rush
    setChoice(a, "mode", 1);      // Hold
    const auto seed = a.currentPreset().humanizeSeed;
    const auto state = stateOf(a);
    REQUIRE(state.getSize() > 0);

    DubwirbelnProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto preset = b.currentPreset();
    REQUIRE(preset.rollType.has_value());
    CHECK(*preset.rollType == RollType::Rush);
    CHECK(preset.mode == PlaybackMode::Hold);
    CHECK(preset.humanizeSeed == seed);
    REQUIRE(preset.sampleA.has_value());
    REQUIRE(preset.sampleB.has_value());
    CHECK(preset.sampleA->name == "kick.wav");
    CHECK(preset.sampleB->name == "snare.wav");

    // the restored instance plays, and saves the same samples again
    CHECK(renderPeak(b, 8) > 0.05f);
    const auto again = stateOf(b);
    const auto* x = static_cast<const std::uint8_t*>(state.getData());
    const auto* y = static_cast<const std::uint8_t*>(again.getData());
    const auto first = pluginStateFromBytes(Bytes(x, x + state.getSize()));
    const auto second = pluginStateFromBytes(Bytes(y, y + again.getSize()));
    REQUIRE(first.state.has_value());
    REQUIRE(second.state.has_value());
    CHECK(first.samples == second.samples);
}

TEST_CASE("a damaged state is ignored", "[processor][state]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setChoice(p, "rollType", 2);
    const std::string garbage = "PK-not-a-real-state";
    p.setStateInformation(garbage.data(), static_cast<int>(garbage.size()));
    p.setStateInformation(nullptr, 0);
    REQUIRE(p.currentPreset().rollType.has_value());
    CHECK(*p.currentPreset().rollType == RollType::Drop);
}

TEST_CASE("two different files with the same name are saved separately", "[processor][state]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("one/hit.wav", 150.0)));
    REQUIRE(p.loadSample(Sample::B, wavFile("two/hit.wav", 900.0)));

    const auto block = stateOf(p);
    const auto* begin = static_cast<const std::uint8_t*>(block.getData());
    const auto read = pluginStateFromBytes(Bytes(begin, begin + block.getSize()));
    INFO(read.error);
    REQUIRE(read.state.has_value());
    CHECK(read.samples.size() == 2);
    REQUIRE(read.state->preset.sampleA.has_value());
    REQUIRE(read.state->preset.sampleB.has_value());
    CHECK(read.state->preset.sampleA->file != read.state->preset.sampleB->file);
    CHECK(read.state->preset.sampleA->sha256 != read.state->preset.sampleB->sha256);
}

TEST_CASE("a preset file round-trips with its samples", "[processor][file]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    REQUIRE(a.loadSample(Sample::A, wavFile("kick.wav", 100.0)));
    setChoice(a, "rollType", 4);  // Burst
    const auto target = tempDir().getChildFile("saved.dwpreset");
    target.deleteFile();
    const auto saved = a.savePreset(target);
    INFO(saved.getErrorMessage());
    REQUIRE(saved.wasOk());
    REQUIRE(target.existsAsFile());

    DubwirbelnProcessor b;
    const auto loaded = b.loadPreset(target);
    INFO(loaded.getErrorMessage());
    REQUIRE(loaded.wasOk());
    const auto preset = b.currentPreset();
    REQUIRE(preset.rollType.has_value());
    CHECK(*preset.rollType == RollType::Burst);
    REQUIRE(preset.sampleA.has_value());
    CHECK(preset.sampleA->name == "kick.wav");
    CHECK(preset.humanizeSeed == a.currentPreset().humanizeSeed);
    CHECK(renderPeak(b, 8) > 0.05f);
}

TEST_CASE("loading a broken preset file fails and keeps the current preset", "[processor][file]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setChoice(p, "rollType", 5);
    auto bad = tempDir().getChildFile("bad.dwpreset");
    bad.replaceWithText("not a zip");
    const auto result = p.loadPreset(bad);
    CHECK(result.failed());
    CHECK_FALSE(result.getErrorMessage().isEmpty());
    CHECK(p.loadPreset(tempDir().getChildFile("nope.dwpreset")).failed());
    REQUIRE(p.currentPreset().rollType.has_value());
    CHECK(*p.currentPreset().rollType == RollType::Wave);
}

TEST_CASE("a slot without sample data is not saved", "[processor][file]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    REQUIRE(a.loadSample(Sample::A, wavFile("kick.wav")));
    const auto state = stateOf(a);

    // a state whose sample file is gone: the slot is dropped on load, saving still works
    DubwirbelnProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(b.currentPreset().sampleA.has_value());
    const auto target = tempDir().getChildFile("noaudio.dwpreset");
    target.deleteFile();
    CHECK(b.savePreset(target).wasOk());
}

// ---- Variate, Undo, Clear (spec Part 10) ----

TEST_CASE("variate changes the roll values and undo and redo walk the last pass", "[processor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setChoice(p, "rollType", 1);  // Build
    const auto before = p.currentPreset();
    CHECK_FALSE(p.canUndoVariate());

    REQUIRE(p.variate());
    const auto varied = p.currentPreset();
    CHECK(varied.variationSeed != 0u);
    CHECK((varied.intensity != before.intensity || varied.blend != before.blend || varied.curve != before.curve));
    CHECK(*varied.rollType == RollType::Build);
    CHECK(varied.mode == before.mode);
    CHECK(p.canUndoVariate());
    CHECK_FALSE(p.canRedoVariate());

    REQUIRE(p.undoVariate());
    const auto undone = p.currentPreset();
    CHECK(undone.intensity == before.intensity);
    CHECK(undone.blend == before.blend);
    CHECK(undone.variationSeed == 0u);
    CHECK(p.canRedoVariate());
    CHECK_FALSE(p.undoVariate());

    REQUIRE(p.redoVariate());
    const auto redone = p.currentPreset();
    CHECK(redone.intensity == varied.intensity);
    CHECK(redone.blend == varied.blend);
    CHECK(redone.variationSeed == varied.variationSeed);
}

TEST_CASE("variate does nothing without a roll", "[processor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    CHECK_FALSE(p.variate());
    CHECK_FALSE(p.canUndoVariate());
}

TEST_CASE("the variation is part of the saved state", "[processor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    setChoice(a, "rollType", 2);  // Drop
    REQUIRE(a.variate());
    const auto expected = a.currentPreset();
    const auto state = stateOf(a);

    DubwirbelnProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto restored = b.currentPreset();
    CHECK(restored.variationSeed == expected.variationSeed);
    CHECK(restored.intensity == expected.intensity);
    CHECK(restored.blend == expected.blend);
    CHECK_FALSE(b.canUndoVariate());  // the undo state is not saved
}

TEST_CASE("clear empties the roll and keeps the samples", "[processor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("tone.wav")));
    setChoice(p, "rollType", 1);
    setChoice(p, "mode", 1);
    REQUIRE(p.variate());
    REQUIRE(p.canUndoVariate());
    const auto seed = p.currentPreset().humanizeSeed;

    p.clearRoll();
    const auto cleared = p.currentPreset();
    CHECK_FALSE(cleared.rollType.has_value());
    CHECK(cleared.intensity == 50.0);
    CHECK(cleared.blend == 50.0);
    CHECK(cleared.variationSeed == 0u);
    CHECK(cleared.mode == PlaybackMode::Hold);
    CHECK(cleared.humanizeSeed == seed);
    CHECK(cleared.sampleA.has_value());
    CHECK_FALSE(p.canUndoVariate());  // clear is not undoable and drops the variate undo state
    CHECK(renderPeak(p, 8) == 0.0f);
}

TEST_CASE("loading a preset forgets the variate undo state", "[processor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setChoice(p, "rollType", 3);
    const auto target = tempDir().getChildFile("variate.dwpreset");
    target.deleteFile();
    REQUIRE(p.savePreset(target).wasOk());
    REQUIRE(p.variate());
    REQUIRE(p.canUndoVariate());
    REQUIRE(p.loadPreset(target).wasOk());
    CHECK_FALSE(p.canUndoVariate());
}

TEST_CASE("a varied roll still plays", "[processor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    // Both slots are loaded: Variate swaps A and B for some triggers (at random), and a trigger of an
    // empty slot is silent, so with only A loaded the first trigger could land on B and the test flake.
    REQUIRE(p.loadSample(Sample::A, wavFile("tone.wav")));
    REQUIRE(p.loadSample(Sample::B, wavFile("tone2.wav", 660.0)));
    setChoice(p, "rollType", 1);
    REQUIRE(p.variate());
    CHECK(renderPeak(p, 8) > 0.05f);
}

// ---- real-time safety ----

TEST_CASE("processBlock does not allocate", "[processor][realtime]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("kick.wav", 100.0)));
    REQUIRE(p.loadSample(Sample::B, wavFile("snare.wav", 800.0)));
    setChoice(p, "rollType", 1);  // Build
    setChoice(p, "mode", 1);      // Hold
    p.prepareToPlay(48000.0, 512);

    // set up everything that may allocate: buffers and the first config sync
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    p.processBlock(buffer, midi);

    long allocations = 0;
    float peak = 0.0f;
    {
        dw::test::AllocGuard guard;
        for (int block = 0; block < 100; ++block) {
            buffer.clear();
            midi.clear();
            if (block == 0 || block == 40) midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 10);
            if (block == 70) midi.addEvent(juce::MidiMessage::noteOff(1, 36), 20);
            p.processBlock(buffer, midi);
            peak = std::max(peak, buffer.getMagnitude(0, 512));
        }
        allocations = guard.allocations();
    }
    CHECK(allocations == 0);
    CHECK(peak > 0.05f);
}

TEST_CASE("parameter changes and preset edits do not make processBlock allocate", "[processor][realtime]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("kick.wav", 100.0)));
    setChoice(p, "rollType", 4);  // Burst
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    p.processBlock(buffer, midi);

    // these run on the message thread and may allocate
    const auto change = [&](int index) {
        setChoice(p, "quantize", index % 5);
        setChoice(p, "windowBars", index % 4);
        if (index % 3 == 0) p.variate();
        if (index % 5 == 0) p.undoVariate();
    };

    long allocations = 0;
    for (int round = 0; round < 12; ++round) {
        change(round);
        dw::test::AllocGuard guard;
        for (int block = 0; block < 6; ++block) {
            buffer.clear();
            midi.clear();
            if (block == 1) midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(90)), 0);
            p.processBlock(buffer, midi);
        }
        allocations += guard.allocations();
    }
    CHECK(allocations == 0);
}

// ---- MIDI Learn and the trigger note (spec Part 9) ----

namespace {

void setPercent(DubwirbelnProcessor& p, const char* id, float plain) {
    for (auto* parameter : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameter); rp && rp->paramID == id)
            rp->setValueNotifyingHost(rp->convertTo0to1(plain));
}

// One block of 512 samples with a Note-On of `note` in it (if note >= 0); returns the peak.
float blockWithNote(DubwirbelnProcessor& p, int note) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    if (note >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0);
    p.processBlock(buffer, midi);
    return buffer.getMagnitude(0, 512);  // both channels
}

float quietBlocks(DubwirbelnProcessor& p, int blocks) {
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i) peak = std::max(peak, blockWithNote(p, -1));
    return peak;
}

DubwirbelnProcessor& playable(DubwirbelnProcessor& p) {
    REQUIRE(p.loadSample(Sample::A, wavFile("tone.wav")));
    REQUIRE(p.loadSample(Sample::B, wavFile("tone2.wav", 660.0)));
    setChoice(p, "rollType", 1);  // Build
    p.prepareToPlay(48000.0, 512);
    return p;
}

}  // namespace

TEST_CASE("the trigger note is C1 by default and can be typed in", "[processor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    CHECK(p.triggerNote() == 36);
    playable(p);
    p.setTriggerNote(60);
    CHECK(p.triggerNote() == 60);
    CHECK(blockWithNote(p, 36) == 0.0f);  // the old note does nothing now
    CHECK(quietBlocks(p, 4) == 0.0f);
    CHECK(blockWithNote(p, 60) > 0.0f);
}

TEST_CASE("MIDI Learn takes the next Note-On, which does not start a roll", "[processor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    playable(p);
    p.startMidiLearn();
    CHECK(p.isMidiLearning());

    CHECK(blockWithNote(p, 52) == 0.0f);
    CHECK(quietBlocks(p, 4) == 0.0f);
    CHECK_FALSE(p.isMidiLearning());
    CHECK(p.triggerNote() == 52);

    CHECK(blockWithNote(p, 36) == 0.0f);  // the old note is out
    CHECK(blockWithNote(p, 52) > 0.0f);   // the learned one plays
}

TEST_CASE("MIDI Learn can be cancelled", "[processor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    playable(p);
    p.startMidiLearn();
    p.cancelMidiLearn();
    CHECK_FALSE(p.isMidiLearning());
    CHECK(blockWithNote(p, 52) == 0.0f);
    CHECK(p.triggerNote() == 36);
}

TEST_CASE("the trigger note is saved with the state, MIDI Learn is not", "[processor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    a.setTriggerNote(47);
    a.startMidiLearn();
    const auto state = stateOf(a);

    DubwirbelnProcessor b;
    b.startMidiLearn();
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(b.triggerNote() == 47);
    CHECK_FALSE(b.isMidiLearning());
}

TEST_CASE("the trigger note does not belong to the preset", "[processor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    a.setTriggerNote(47);
    const auto target = tempDir().getChildFile("note.dwpreset");
    target.deleteFile();
    REQUIRE(a.savePreset(target).wasOk());

    DubwirbelnProcessor b;
    b.setTriggerNote(55);
    REQUIRE(b.loadPreset(target).wasOk());
    CHECK(b.triggerNote() == 55);  // loading a preset never changes it
}

// ---- dirty tracking (spec Part 11) ----

TEST_CASE("a new instance is not dirty, a change is, changing back is not", "[processor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    CHECK_FALSE(p.isDirty());
    setPercent(p, "intensity", 71.0f);
    CHECK(p.isDirty());
    setPercent(p, "intensity", 50.0f);
    CHECK_FALSE(p.isDirty());
}

TEST_CASE("saving makes the preset clean, a further change makes it dirty again", "[processor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("tone.wav")));
    CHECK(p.isDirty());  // a loaded sample is a change
    const auto target = tempDir().getChildFile("dirty.dwpreset");
    target.deleteFile();
    REQUIRE(p.savePreset(target).wasOk());
    CHECK_FALSE(p.isDirty());
    setChoice(p, "mode", 1);
    CHECK(p.isDirty());
}

TEST_CASE("a loaded preset is clean, even when the parameters round its values", "[processor][dirty]") {
    JuceRuntime runtime;
    PresetArchive archive;
    archive.preset = defaultPreset(3);
    archive.preset.rollType = RollType::Burst;
    archive.preset.intensity = 78.34;  // the parameter has steps of 0.1
    archive.preset.blend = 12.07;
    const auto written = writePresetArchive(archive);
    REQUIRE(written.ok());
    auto file = tempDir().getChildFile("rounded.dwpreset");
    REQUIRE(file.replaceWithData(written.bytes.data(), written.bytes.size()));

    DubwirbelnProcessor p;
    setChoice(p, "mode", 1);
    REQUIRE(p.isDirty());
    REQUIRE(p.loadPreset(file).wasOk());
    CHECK_FALSE(p.isDirty());
}

TEST_CASE("variate makes the preset dirty and undo makes it clean again", "[processor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setChoice(p, "rollType", 1);
    REQUIRE(p.variate());
    CHECK(p.isDirty());
    REQUIRE(p.undoVariate());
    CHECK(p.isDirty());  // the roll type was chosen after creation, so it still differs from the baseline
    setChoice(p, "rollType", 0);
    CHECK_FALSE(p.isDirty());
}

TEST_CASE("clear asks for confirmation only when the preset is dirty", "[processor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    setChoice(a, "rollType", 2);
    setPercent(a, "intensity", 80.0f);
    const auto target = tempDir().getChildFile("clear.dwpreset");
    target.deleteFile();
    REQUIRE(a.savePreset(target).wasOk());

    DubwirbelnProcessor p;
    REQUIRE(p.loadPreset(target).wasOk());
    CHECK_FALSE(p.isDirty());
    CHECK_FALSE(p.needsClearConfirmation());  // an unmodified preset is cleared without a dialog

    setPercent(p, "swing", 10.0f);
    CHECK(p.needsClearConfirmation());

    p.clearRoll();
    CHECK(p.isDirty());  // clear marks the preset dirty
}

TEST_CASE("clearing an empty preset changes nothing", "[processor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    p.clearRoll();
    CHECK_FALSE(p.isDirty());
}

TEST_CASE("unsaved changes survive the project: dirty stays dirty, clean stays clean", "[processor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    setChoice(a, "rollType", 3);
    const auto target = tempDir().getChildFile("project.dwpreset");
    target.deleteFile();
    REQUIRE(a.savePreset(target).wasOk());
    const auto cleanState = stateOf(a);
    setPercent(a, "blend", 20.0f);
    REQUIRE(a.isDirty());
    const auto dirtyState = stateOf(a);

    DubwirbelnProcessor b;
    b.setStateInformation(cleanState.getData(), static_cast<int>(cleanState.getSize()));
    CHECK_FALSE(b.isDirty());

    DubwirbelnProcessor c;
    c.setStateInformation(dirtyState.getData(), static_cast<int>(dirtyState.getSize()));
    CHECK(c.isDirty());
}

// ---- the Roll Painter: gestures (spec Parts 4 and 5) ----

namespace {

Stroke gestureLine(double x0, double y0, double x1, double y1, int n = 40) {
    Stroke s;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        s.push_back({x0 + (x1 - x0) * t, y0 + (y1 - y0) * t});
    }
    return s;
}

Gesture bentRamp() {  // BUILD with about 70 %, RUSH as the alternative
    Stroke s = gestureLine(0.0, 0.0, 0.5, 0.3, 25);
    const Stroke t = gestureLine(0.5, 0.3, 1.0, 1.0, 25);
    s.insert(s.end(), t.begin() + 1, t.end());
    return {s};
}

Gesture dots() {
    return {{{0.1, 0.5}, {0.11, 0.5}}, {{0.4, 0.5}, {0.41, 0.5}}, {{0.7, 0.5}, {0.71, 0.5}}, {{0.95, 0.5}, {0.96, 0.5}}};
}

}  // namespace

TEST_CASE("a clear gesture sets the roll type and is stored", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    setPercent(p, "intensity", 63.0f);
    setPercent(p, "blend", 22.0f);
    const auto before = p.currentPreset();

    const auto r = p.drawGesture({gestureLine(0.1, 0.1, 0.9, 0.9)});
    CHECK(r.decision == GestureDecision::Apply);

    const auto preset = p.currentPreset();
    REQUIRE(preset.rollType.has_value());
    CHECK(*preset.rollType == RollType::Build);
    CHECK(preset.intensity == before.intensity);  // a gesture sets only the roll type
    CHECK(preset.blend == before.blend);
    CHECK(preset.gesture.size() >= 2);
    CHECK(preset.gesture.size() <= 512);
    for (const auto& pt : preset.gesture) {
        CHECK(pt.x >= 0.0);
        CHECK(pt.x <= 1.0);
        CHECK(pt.y >= 0.0);
        CHECK(pt.y <= 1.0);
    }
    CHECK(p.isDirty());
}

TEST_CASE("every gesture of the spec reaches its roll type", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    struct Case {
        Gesture gesture;
        RollType expected;
    };
    Stroke rush = gestureLine(0.0, 0.1, 0.7, 0.1, 30);
    const Stroke rise = gestureLine(0.7, 0.1, 1.0, 0.5, 15);
    rush.insert(rush.end(), rise.begin() + 1, rise.end());
    Stroke wave;
    for (int i = 0; i < 200; ++i) {
        const double t = i / 199.0;
        wave.push_back({t, 0.5 + 0.25 * std::sin(2.0 * 3.14159265358979 * 3.0 * t)});
    }
    const Case cases[] = {{{gestureLine(0.1, 0.1, 0.9, 0.9)}, RollType::Build},
                          {{gestureLine(0.1, 0.9, 0.9, 0.1)}, RollType::Drop},
                          {{rush}, RollType::Rush},
                          {dots(), RollType::Burst},
                          {{wave}, RollType::Wave}};
    for (const auto& c : cases) {
        INFO("expected roll type " << static_cast<int>(c.expected));
        p.drawGesture(c.gesture);
        REQUIRE(p.currentPreset().rollType.has_value());
        CHECK(*p.currentPreset().rollType == c.expected);
    }
}

TEST_CASE("an unclear gesture changes nothing", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.drawGesture({gestureLine(0.1, 0.1, 0.9, 0.9)}).decision == GestureDecision::Apply);
    const auto before = p.currentPreset();

    const auto r = p.drawGesture({gestureLine(0.0, 0.5, 1.0, 0.5)});  // a flat line is nothing
    CHECK(r.decision == GestureDecision::Reject);
    const auto after = p.currentPreset();
    CHECK(*after.rollType == RollType::Build);
    CHECK(after.gesture.size() == before.gesture.size());  // the old gesture stays
    CHECK(presetsEquivalent(before, after));
}

TEST_CASE("a medium confidence applies the best guess and offers the alternative", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    const auto r = p.drawGesture(bentRamp());
    CHECK(r.decision == GestureDecision::ApplyWithAlternative);
    REQUIRE(r.best.has_value());
    REQUIRE(r.alternative.has_value());
    CHECK(*p.currentPreset().rollType == r.best->type);  // applied at once

    REQUIRE(p.chooseGestureAlternative());
    CHECK(*p.currentPreset().rollType == r.alternative->type);
    CHECK(p.currentPreset().gesture.size() >= 2);  // the drawing stays

    REQUIRE(p.chooseGestureAlternative());  // the previous best is the alternative now
    CHECK(*p.currentPreset().rollType == r.best->type);
}

TEST_CASE("without an alternative there is nothing to choose", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    CHECK_FALSE(p.chooseGestureAlternative());  // nothing drawn yet
    p.drawGesture({gestureLine(0.1, 0.1, 0.9, 0.9)});
    CHECK_FALSE(p.chooseGestureAlternative());  // clear gesture: no alternative
    p.drawGesture({gestureLine(0.0, 0.5, 1.0, 0.5)});
    CHECK_FALSE(p.chooseGestureAlternative());  // rejected
}

TEST_CASE("a drawn gesture makes the instrument play", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.loadSample(Sample::A, wavFile("tone.wav")));
    REQUIRE(p.loadSample(Sample::B, wavFile("tone2.wav", 660.0)));
    p.prepareToPlay(48000.0, 512);
    CHECK(blockWithNote(p, 36) == 0.0f);  // no roll yet: silent until a gesture is drawn
    p.prepareToPlay(48000.0, 512);
    p.drawGesture({gestureLine(0.1, 0.1, 0.9, 0.9)});
    CHECK(blockWithNote(p, 36) > 0.0f);
}

TEST_CASE("the gesture is saved with the preset and removed by clear", "[processor][gesture]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    a.drawGesture({gestureLine(0.1, 0.1, 0.9, 0.9)});
    const auto expected = a.currentPreset().gesture;
    const auto state = stateOf(a);

    DubwirbelnProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(b.currentPreset().gesture.size() == expected.size());
    CHECK(b.isDirty());  // the gesture was never saved to a file: the baseline travels with the state

    b.clearRoll();
    CHECK(b.currentPreset().gesture.empty());
    CHECK_FALSE(b.currentPreset().rollType.has_value());
}
