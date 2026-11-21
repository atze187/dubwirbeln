#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/NoteNames.h"
#include "plugin/DubwirbelnProcessor.h"
#include "plugin/ui/Editor.h"

using namespace dw;

// The real editor, created without a host: the controls, the actions behind the buttons, the status
// bar and the layout. One test also renders the editor to a PNG so the look can be checked by eye.
namespace {

struct JuceRuntime {
    juce::ScopedJuceInitialiser_GUI init;
};

juce::File tempDir() {
    static const auto dir = [] {
        auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dubwirbeln_editor_tests");
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }();
    return dir;
}

juce::File wavFile(const juce::String& name, double frequency = 440.0) {
    auto file = tempDir().getChildFile(name);
    file.deleteFile();
    juce::AudioBuffer<float> buffer(1, 4410);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * frequency * i / 44100.0)));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    REQUIRE(stream != nullptr);
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(44100.0)
                                                  .withNumChannels(1)
                                                  .withBitsPerSample(16));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
    writer.reset();
    return file;
}

template <typename T = juce::Component>
T& find(ui::Editor& editor, const char* id) {
    auto* c = dynamic_cast<T*>(editor.findChildWithID(id));
    REQUIRE(c != nullptr);
    return *c;
}

void click(ui::Editor& editor, const char* id) { find<juce::TextButton>(editor, id).onClick(); }

float param(DubwirbelnProcessor& p, const char* id) {
    auto* parameter = p.parameters().getParameter(id);
    REQUIRE(parameter != nullptr);
    return parameter->convertFrom0to1(parameter->getValue());
}

}  // namespace

TEST_CASE("the editor opens with all its controls", "[editor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    CHECK(editor.getWidth() >= 760);
    CHECK(editor.getHeight() >= 420);
    for (const char* id : {"presetName", "load", "save", "saveAs", "undo", "redo", "variate", "clear", "rollType",
                           "intensity", "blend", "loadA", "loadB", "mode", "window", "quantize", "follow", "humanize",
                           "swing", "note", "learn", "status"}) {
        INFO(id);
        CHECK(editor.findChildWithID(id) != nullptr);
    }
}

TEST_CASE("the processor creates this editor", "[editor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    REQUIRE(p.hasEditor());
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    REQUIRE(editor != nullptr);
    CHECK(dynamic_cast<ui::Editor*>(editor.get()) != nullptr);
}

TEST_CASE("the controls show the parameters and change them", "[editor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);

    auto& roll = find<ui::ChoiceRow>(editor, "rollType");
    CHECK(roll.selected() == 0);
    roll.button(1)->onClick();  // BUILD
    CHECK(param(p, "rollType") == 1.0f);
    CHECK(roll.selected() == 1);
    CHECK(roll.button(1)->getToggleState());
    CHECK_FALSE(roll.button(0)->getToggleState());

    auto& mode = find<ui::ChoiceRow>(editor, "mode");
    mode.button(1)->onClick();  // HOLD
    CHECK(p.currentPreset().mode == PlaybackMode::Hold);
    auto& follow = find<ui::ChoiceRow>(editor, "follow");
    CHECK(follow.selected() == 2);  // the default: velocity + intensity
    follow.button(0)->onClick();
    CHECK(p.currentPreset().velocityFollow == VelocityFollow::Off);

    auto& intensity = find<ui::PercentControl>(editor, "intensity");
    intensity.slider().setValue(72.0, juce::sendNotificationSync);
    CHECK(param(p, "intensity") == 72.0f);
    CHECK(intensity.readout() == "72.0 %");
}

TEST_CASE("parameter changes from the host show up in the controls", "[editor]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    auto* quantize = p.parameters().getParameter("quantize");
    quantize->setValueNotifyingHost(quantize->convertTo0to1(3.0f));  // 1/4
    CHECK(find<ui::ChoiceRow>(editor, "quantize").selected() == 3);
    auto* blend = p.parameters().getParameter("blend");
    blend->setValueNotifyingHost(blend->convertTo0to1(20.0f));
    CHECK(find<ui::PercentControl>(editor, "blend").readout() == "20.0 %");
}

TEST_CASE("the preset name shows a star when the preset has unsaved changes", "[editor][dirty]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    CHECK(editor.presetLabel() == "Init");
    find<ui::ChoiceRow>(editor, "rollType").button(2)->onClick();
    CHECK(editor.presetLabel() == "Init *");
    editor.timerCallbackForTest();  // the 10 Hz timer updates the header
    CHECK(find<juce::Label>(editor, "presetName").getText() == "Init *");
}

TEST_CASE("save as names the preset after the file, save overwrites it", "[editor][file]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    auto& save = find<juce::TextButton>(editor, "save");
    CHECK_FALSE(save.isEnabled());  // no file yet
    editor.save();
    CHECK(editor.statusText().startsWith("NO FILE YET"));

    find<ui::ChoiceRow>(editor, "rollType").button(3)->onClick();
    auto file = tempDir().getChildFile("Night Drive");
    file.withFileExtension("dwpreset").deleteFile();
    editor.saveAs(file);  // the extension is added
    const auto saved = tempDir().getChildFile("Night Drive.dwpreset");
    CHECK(saved.existsAsFile());
    CHECK(editor.presetLabel() == "Night Drive");
    CHECK(editor.statusText() == "SAVED Night Drive.dwpreset");
    CHECK(save.isEnabled());

    find<ui::ChoiceRow>(editor, "mode").button(1)->onClick();
    CHECK(editor.presetLabel() == "Night Drive *");
    const auto sizeBefore = saved.getSize();
    editor.save();
    CHECK(editor.presetLabel() == "Night Drive");
    CHECK(saved.existsAsFile());
    (void) sizeBefore;
}

TEST_CASE("loading a preset file shows it, a broken file is reported", "[editor][file]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    {
        ui::Editor first(a);
        find<ui::ChoiceRow>(first, "rollType").button(5)->onClick();
        first.saveAs(tempDir().getChildFile("Wave Preset.dwpreset"));
    }
    DubwirbelnProcessor b;
    ui::Editor editor(b);
    editor.loadPresetFile(tempDir().getChildFile("Wave Preset.dwpreset"));
    CHECK(editor.statusText() == "LOADED Wave Preset");
    CHECK(find<ui::ChoiceRow>(editor, "rollType").selected() == 5);
    CHECK(editor.presetLabel() == "Wave Preset");

    auto bad = tempDir().getChildFile("bad.dwpreset");
    bad.replaceWithText("nothing");
    editor.loadPresetFile(bad);
    CHECK(editor.statusText().startsWith("CANNOT LOAD"));
    CHECK(editor.presetLabel() == "Wave Preset");  // the current preset stays
}

TEST_CASE("samples are loaded into their slots", "[editor][samples]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    editor.loadSampleFile(Sample::A, wavFile("kick.wav"));
    CHECK(editor.statusText() == "SAMPLE A: kick.wav");
    editor.loadSampleFile(Sample::B, wavFile("snare.wav", 800.0));
    CHECK(editor.statusText() == "SAMPLE B: snare.wav");
    editor.loadSampleFile(Sample::A, tempDir().getChildFile("missing.wav"));
    CHECK(editor.statusText().startsWith("CANNOT LOAD"));
    CHECK(p.currentPreset().sampleA->name == "kick.wav");
}

TEST_CASE("variate, undo and redo work from the toolbar", "[editor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    auto& variate = find<juce::TextButton>(editor, "variate");
    auto& undo = find<juce::TextButton>(editor, "undo");
    auto& redo = find<juce::TextButton>(editor, "redo");

    CHECK_FALSE(variate.isEnabled());  // no roll yet
    CHECK_FALSE(undo.isEnabled());
    CHECK_FALSE(redo.isEnabled());

    find<ui::ChoiceRow>(editor, "rollType").button(1)->onClick();
    editor.variate();  // refresh is part of the action
    CHECK(variate.isEnabled());
    CHECK(editor.statusText().startsWith("VARIATE"));
    CHECK(undo.isEnabled());
    CHECK_FALSE(redo.isEnabled());
    CHECK(p.currentPreset().variationSeed != 0u);

    undo.onClick();
    CHECK(editor.statusText() == "UNDO VARIATE");
    CHECK_FALSE(undo.isEnabled());
    CHECK(redo.isEnabled());
    CHECK(p.currentPreset().variationSeed == 0u);

    redo.onClick();
    CHECK(editor.statusText() == "REDO VARIATE");
    CHECK(p.currentPreset().variationSeed != 0u);
}

TEST_CASE("variate without a roll says why it did nothing", "[editor][variate]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    editor.variate();
    CHECK(editor.statusText().startsWith("NOTHING TO VARY"));
}

TEST_CASE("clear empties the roll at once when nothing is unsaved", "[editor][clear]") {
    JuceRuntime runtime;
    DubwirbelnProcessor a;
    {
        ui::Editor first(a);
        find<ui::ChoiceRow>(first, "rollType").button(2)->onClick();
        first.saveAs(tempDir().getChildFile("clear me.dwpreset"));
    }
    DubwirbelnProcessor b;
    ui::Editor editor(b);
    editor.loadPresetFile(tempDir().getChildFile("clear me.dwpreset"));
    REQUIRE_FALSE(b.needsClearConfirmation());
    editor.clearRoll();  // no dialog: the preset is clean
    CHECK(editor.statusText() == "ROLL CLEARED");
    CHECK_FALSE(b.currentPreset().rollType.has_value());
    CHECK(find<ui::ChoiceRow>(editor, "rollType").selected() == 0);
}

TEST_CASE("clear asks first when the preset has unsaved changes", "[editor][clear]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    find<ui::ChoiceRow>(editor, "rollType").button(2)->onClick();
    CHECK(p.needsClearConfirmation());  // clearRoll() would open the dialog
    editor.clearRollNow();
    CHECK_FALSE(p.currentPreset().rollType.has_value());
}

TEST_CASE("MIDI Learn is started from the button, shown in the status bar and cancelled by Esc", "[editor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    auto& learn = find<juce::TextButton>(editor, "learn");
    CHECK_FALSE(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));  // nothing to cancel

    learn.onClick();
    CHECK(p.isMidiLearning());
    CHECK(learn.getToggleState());
    CHECK(editor.statusText().startsWith("MIDI LEARN: PLAY A NOTE"));

    CHECK(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(p.isMidiLearning());
    CHECK_FALSE(learn.getToggleState());

    learn.onClick();
    learn.onClick();  // clicking again cancels
    CHECK_FALSE(p.isMidiLearning());
}

TEST_CASE("the trigger note can be typed as a name or a number", "[editor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    auto& note = find<juce::Label>(editor, "note");
    CHECK(note.getText() == "C1");

    note.setText("D#2", juce::sendNotificationSync);
    CHECK(p.triggerNote() == 51);
    CHECK(editor.statusText() == "TRIGGER NOTE D#2");

    note.setText("60", juce::sendNotificationSync);
    CHECK(p.triggerNote() == 60);
    CHECK(note.getText() == "C3");  // shown as a name

    note.setText("nonsense", juce::sendNotificationSync);
    CHECK(p.triggerNote() == 60);
    CHECK(editor.statusText().startsWith("NOT A NOTE"));
    CHECK(note.getText() == "C3");
}

TEST_CASE("a learned note shows in the trigger note field", "[editor][midi]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    p.prepareToPlay(48000.0, 512);
    click(editor, "learn");
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 52, static_cast<juce::uint8>(100)), 0);
    p.processBlock(buffer, midi);
    editor.timerCallbackForTest();
    CHECK(find<juce::Label>(editor, "note").getText() == juce::String(noteName(52)));
    CHECK_FALSE(find<juce::TextButton>(editor, "learn").getToggleState());
}

TEST_CASE("at the smallest size nothing overlaps or leaves the window", "[editor][layout]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    ui::Editor editor(p);
    editor.setSize(760, 420);
    const auto bounds = editor.getLocalBounds();
    const int statusTop = find<juce::Label>(editor, "status").getY();
    for (const char* id : {"presetName", "load", "save", "saveAs", "undo", "redo", "variate", "clear", "rollType",
                           "intensity", "blend", "loadA", "loadB", "mode", "window", "quantize", "follow", "humanize",
                           "swing", "note", "learn", "status"}) {
        INFO(id);
        auto* c = editor.findChildWithID(id);
        REQUIRE(c != nullptr);
        const auto b = editor.getLocalArea(c->getParentComponent(), c->getBounds());
        CHECK(bounds.contains(b));
        if (juce::String(id) != "status") CHECK(b.getBottom() <= statusTop);
    }
    // the header buttons do not overlap each other
    juce::Array<juce::Rectangle<int>> row;
    for (const char* id : {"load", "save", "saveAs", "undo", "redo", "variate", "clear"})
        row.add(find<juce::TextButton>(editor, id).getBounds());
    for (int i = 0; i < row.size(); ++i)
        for (int j = i + 1; j < row.size(); ++j) CHECK_FALSE(row[i].intersects(row[j]));
    CHECK(find<juce::Label>(editor, "presetName").getWidth() >= 100);
}

TEST_CASE("the editor renders to a picture", "[editor][look]") {
    JuceRuntime runtime;
    DubwirbelnProcessor p;
    p.loadSample(Sample::A, wavFile("kick.wav"));
    ui::Editor editor(p);
    editor.setSize(840, 440);
    find<ui::ChoiceRow>(editor, "rollType").button(1)->onClick();
    find<ui::PercentControl>(editor, "intensity").slider().setValue(78.0, juce::sendNotificationSync);
    find<ui::PercentControl>(editor, "blend").slider().setValue(61.0, juce::sendNotificationSync);
    find<ui::PercentControl>(editor, "humanize").slider().setValue(25.0, juce::sendNotificationSync);
    editor.variate();

    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true);
    REQUIRE(image.getWidth() == 840);
    REQUIRE(image.getHeight() == 440);
    CHECK(image.getPixelAt(2, 2) == ui::panel::kWindow);  // the window colour

    const auto file = tempDir().getChildFile("editor.png");
    file.deleteFile();
    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
    REQUIRE(out != nullptr);
    CHECK(png.writeImageToStream(image, *out));
}
