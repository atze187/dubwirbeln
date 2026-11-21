#include "plugin/ui/Editor.h"

#include "engine/NoteNames.h"

namespace dw::ui {

using namespace panel;

namespace {

constexpr int kMargin = 10;
constexpr int kHeaderH = 28;
constexpr int kStatusH = 24;
constexpr int kTitleH = 18;
constexpr int kDefaultW = 840;
constexpr int kDefaultH = 440;
constexpr int kMinW = 760;
constexpr int kMinH = 420;

juce::RangedAudioParameter& param(DubwirbelnProcessor& p, const char* id) {
    auto* parameter = p.parameters().getParameter(id);
    jassert(parameter != nullptr);
    return *parameter;
}

void styleButton(juce::TextButton& b, const char* id, const juce::String& tooltip) {
    b.setComponentID(id);
    b.setTooltip(tooltip);
}

}  // namespace

Editor::Editor(DubwirbelnProcessor& processor)
    : juce::AudioProcessorEditor(processor),
      proc_(processor),
      rollType_(param(processor, "rollType"), {"NONE", "BUILD", "DROP", "RUSH", "BURST", "WAVE"},
                [](int i) { return i == 0 ? kDim : rollColour(static_cast<RollType>(i - 1)); }),
      intensity_(param(processor, "intensity"), "INTENSITY", kAmber,
                 "How far the roll escalates: the highest grid level and how long it is held."),
      blend_(param(processor, "blend"), "BLEND", kAccent,
             "How much of sample B plays: 50 % is the original pattern, 0 % all A, 100 % all B."),
      mode_(param(processor, "mode"), {"TRIGGER", "HOLD"}),
      window_(param(processor, "windowBars"), {"4", "2", "1", "1/2"}),
      quantize_(param(processor, "quantize"), {"OFF", "1/16", "1/8", "1/4", "BAR"}),
      follow_(param(processor, "velocityFollow"), {"OFF", "VELOCITY", "VEL + INT"}),
      humanize_(param(processor, "humanize"), "HUMANIZE", kGreen, "Random timing and velocity deviation."),
      swing_(param(processor, "swing"), "SWING", kGreen, "Delays every second step of the grid.") {
    setLookAndFeel(&look_);

    logo_.setText("DUBWIRBELN", juce::dontSendNotification);
    logo_.setFont(mono(12.0f, true));
    logo_.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(logo_);

    styleLcd(presetName_, kAmber, juce::Justification::centredLeft);
    presetName_.setComponentID("presetName");
    addAndMakeVisible(presetName_);

    styleButton(load_, "load", "Load a preset (.dwpreset).");
    styleButton(save_, "save", "Save the preset to its file.");
    styleButton(saveAs_, "saveAs", "Save the preset as a new file.");
    styleButton(undo_, "undo", "Undo the last Variate.");
    styleButton(redo_, "redo", "Redo the last Variate.");
    styleButton(variate_, "variate", "Create an alternative version of the roll.");
    styleButton(clear_, "clear", "Empty the roll. The samples stay.");
    for (auto* b : {&load_, &save_, &saveAs_, &undo_, &redo_, &variate_, &clear_}) addAndMakeVisible(b);

    load_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load preset", juce::File(), "*.dwpreset");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult() != juce::File()) safe->loadPresetFile(fc.getResult());
                              });
    };
    saveAs_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Save preset as", juce::File(), "*.dwpreset");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult() != juce::File()) safe->saveAs(fc.getResult());
                              });
    };
    save_.onClick = [this] { save(); };
    undo_.onClick = [this] { undoVariate(); };
    redo_.onClick = [this] { redoVariate(); };
    variate_.onClick = [this] { variate(); };
    clear_.onClick = [this] { clearRoll(); };

    rollType_.setComponentID("rollType");
    for (int i = 0; i < 6; ++i)
        rollType_.setButtonTooltip(i, i == 0 ? "No roll: the instrument is silent."
                                             : juce::String("Roll type ") + juce::StringArray{"", "BUILD", "DROP", "RUSH", "BURST", "WAVE"}[i] +
                                                   " (a gesture sets it too).");
    addAndMakeVisible(rollType_);
    intensity_.setComponentID("intensity");
    blend_.setComponentID("blend");
    addAndMakeVisible(intensity_);
    addAndMakeVisible(blend_);

    for (int i = 0; i < 2; ++i) {
        styleCaption(sampleCaption_[i], i == 0 ? "A" : "B", juce::Justification::centred);
        sampleCaption_[i].setFont(mono(11.0f, true));
        sampleCaption_[i].setColour(juce::Label::textColourId, kText);
        sampleCaption_[i].setColour(juce::Label::backgroundColourId, kEdge);
        addAndMakeVisible(sampleCaption_[i]);
        styleLcd(sampleName_[i], kAmber, juce::Justification::centredLeft);
        sampleName_[i].setTooltip(i == 0 ? "Sample A: the main sample." : "Sample B: the accent sample.");
        addAndMakeVisible(sampleName_[i]);
        sampleLoad_[i].setButtonText("LOAD");
        styleButton(sampleLoad_[i], i == 0 ? "loadA" : "loadB", "Load a sample (WAV, AIF, MP3 or FLAC).");
        sampleLoad_[i].onClick = [this, i] { chooseSample(i == 0 ? Sample::A : Sample::B); };
        addAndMakeVisible(sampleLoad_[i]);
    }

    styleCaption(modeCaption_, "MODE");
    styleCaption(windowCaption_, "WINDOW (BARS)");
    styleCaption(quantizeCaption_, "QUANTIZE");
    styleCaption(followCaption_, "VELOCITY FOLLOW");
    styleCaption(noteCaption_, "TRIGGER NOTE");
    for (auto* l : {&modeCaption_, &windowCaption_, &quantizeCaption_, &followCaption_, &noteCaption_})
        addAndMakeVisible(l);

    mode_.setComponentID("mode");
    window_.setComponentID("window");
    quantize_.setComponentID("quantize");
    follow_.setComponentID("follow");
    mode_.setButtonTooltip(0, "Trigger: the roll runs to its end, Note-Off is ignored.");
    mode_.setButtonTooltip(1, "Hold: forward, then backward while the key is held; Note-Off ends it.");
    window_.setButtonTooltip(0, "Play the whole roll of 4 bars.");
    follow_.setButtonTooltip(0, "The played velocity changes nothing.");
    follow_.setButtonTooltip(1, "The played velocity scales the trigger velocities.");
    follow_.setButtonTooltip(2, "The played velocity scales the velocities and sets the intensity.");
    quantize_.setButtonTooltip(0, "Start at once on Note-On.");
    for (auto* c : {&mode_, &window_, &quantize_, &follow_}) addAndMakeVisible(c);
    humanize_.setComponentID("humanize");
    swing_.setComponentID("swing");
    addAndMakeVisible(humanize_);
    addAndMakeVisible(swing_);

    styleLcd(note_, kAmber);
    note_.setComponentID("note");
    note_.setEditable(false, true, false);
    note_.setTooltip("The note that starts the roll: type C1 or 36, or press LEARN and play a note.");
    note_.onTextChange = [this] { noteEdited(); };
    addAndMakeVisible(note_);
    styleButton(learn_, "learn", "MIDI Learn: the next note you play becomes the trigger note. Esc cancels.");
    learn_.setColour(juce::TextButton::buttonOnColourId, kAmber.darker(0.55f));
    learn_.setColour(juce::TextButton::textColourOnId, kAmber.brighter(0.3f));
    learn_.onClick = [this] { toggleLearn(); };
    addAndMakeVisible(learn_);

    styleLcd(status_, kDim, juce::Justification::centredLeft);
    status_.setComponentID("status");
    addAndMakeVisible(status_);

    addMouseListener(this, true);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, 1800, 1200);
    setSize(kDefaultW, kDefaultH);
    refresh();
    startTimerHz(10);
}

Editor::~Editor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Editor::paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) const {
    g.setColour(kStrip);
    g.fillRect(bounds);
    g.setColour(kEdge);
    g.drawRect(bounds, 1);
    g.setColour(kDim);
    g.setFont(juce::Font(mono(10.0f, true)));
    g.drawText(title, bounds.getX() + 10, bounds.getY() + 4, bounds.getWidth() - 20, 12, juce::Justification::centredLeft);
    g.setColour(kEdge);
    g.drawHorizontalLine(bounds.getY() + kTitleH, static_cast<float>(bounds.getX() + 1),
                         static_cast<float>(bounds.getRight() - 1));
}

void Editor::paint(juce::Graphics& g) {
    g.fillAll(kWindow);
    paintPanel(g, rollPanel_, "ROLL");
    paintPanel(g, samplesPanel_, "SAMPLES");
    paintPanel(g, playbackPanel_, "PLAYBACK");
}

void Editor::resized() {
    auto area = getLocalBounds().reduced(kMargin, 8);

    // header: logo, preset name, file buttons, toolbar
    auto header = area.removeFromTop(kHeaderH);
    logo_.setBounds(header.removeFromLeft(112));
    auto toolbar = header.removeFromRight(4 * 62 + 3 * 4);
    header.removeFromRight(12);
    for (auto* b : {&undo_, &redo_, &variate_, &clear_}) {
        b->setBounds(toolbar.removeFromLeft(62));
        toolbar.removeFromLeft(4);
    }
    saveAs_.setBounds(header.removeFromRight(66));
    header.removeFromRight(4);
    save_.setBounds(header.removeFromRight(52));
    header.removeFromRight(4);
    load_.setBounds(header.removeFromRight(52));
    header.removeFromRight(8);
    presetName_.setBounds(header);

    area.removeFromTop(10);
    status_.setBounds(area.removeFromBottom(kStatusH));
    area.removeFromBottom(10);

    // two columns: roll and samples on the left, playback on the right
    auto left = area.removeFromLeft(static_cast<int>(area.getWidth() * 0.55f));
    area.removeFromLeft(10);
    playbackPanel_ = area;
    rollPanel_ = left.removeFromTop(138);
    left.removeFromTop(10);
    samplesPanel_ = left.removeFromTop(86);

    auto roll = rollPanel_.reduced(10, 6);
    roll.removeFromTop(kTitleH + 4);
    rollType_.setBounds(roll.removeFromTop(24));
    roll.removeFromTop(8);
    intensity_.setBounds(roll.removeFromTop(34));
    roll.removeFromTop(2);
    blend_.setBounds(roll.removeFromTop(34));

    auto samples = samplesPanel_.reduced(10, 6);
    samples.removeFromTop(kTitleH + 4);
    for (int i = 0; i < 2; ++i) {
        auto row = samples.removeFromTop(22);
        sampleCaption_[i].setBounds(row.removeFromLeft(22));
        row.removeFromLeft(4);
        sampleLoad_[i].setBounds(row.removeFromRight(56));
        row.removeFromRight(4);
        sampleName_[i].setBounds(row);
        samples.removeFromTop(6);
    }

    auto pb = playbackPanel_.reduced(10, 6);
    pb.removeFromTop(kTitleH + 4);
    auto choice = [&pb](juce::Label& caption, ChoiceRow& row) {
        caption.setBounds(pb.removeFromTop(12));
        row.setBounds(pb.removeFromTop(24));
        pb.removeFromTop(6);
    };
    choice(modeCaption_, mode_);
    choice(windowCaption_, window_);
    choice(quantizeCaption_, quantize_);
    choice(followCaption_, follow_);
    humanize_.setBounds(pb.removeFromTop(36));
    pb.removeFromTop(2);
    swing_.setBounds(pb.removeFromTop(36));
    pb.removeFromTop(4);
    noteCaption_.setBounds(pb.removeFromTop(12));
    auto noteRow = pb.removeFromTop(24);
    learn_.setBounds(noteRow.removeFromRight(70));
    noteRow.removeFromRight(6);
    note_.setBounds(noteRow);
}

bool Editor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && proc_.isMidiLearning()) {
        toggleLearn();
        return true;
    }
    return false;
}

void Editor::mouseEnter(const juce::MouseEvent& e) {
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(e.eventComponent)) hint_ = client->getTooltip();
    else hint_.clear();
}

void Editor::mouseExit(const juce::MouseEvent&) { hint_.clear(); }

// ---- actions ----

void Editor::loadPresetFile(const juce::File& file) {
    const auto result = proc_.loadPreset(file);
    if (result.wasOk()) showStatus("LOADED " + file.getFileNameWithoutExtension(), Tone::Good);
    else showStatus("CANNOT LOAD: " + result.getErrorMessage(), Tone::Warn);
    refresh();
}

void Editor::saveAs(const juce::File& chosen) {
    const auto file = chosen.hasFileExtension("dwpreset") ? chosen : chosen.withFileExtension("dwpreset");
    proc_.setPresetName(file.getFileNameWithoutExtension());
    const auto result = proc_.savePreset(file);
    if (result.wasOk()) showStatus("SAVED " + file.getFileName(), Tone::Good);
    else showStatus("CANNOT SAVE: " + result.getErrorMessage(), Tone::Warn);
    refresh();
}

void Editor::save() {
    const auto file = proc_.presetFile();
    if (file == juce::File()) {
        showStatus("NO FILE YET: USE SAVE AS", Tone::Warn);
        return;
    }
    const auto result = proc_.savePreset(file);
    if (result.wasOk()) showStatus("SAVED " + file.getFileName(), Tone::Good);
    else showStatus("CANNOT SAVE: " + result.getErrorMessage(), Tone::Warn);
    refresh();
}

void Editor::chooseSample(Sample slot) {
    chooser_ = std::make_unique<juce::FileChooser>(slot == Sample::A ? "Load sample A" : "Load sample B",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<Editor>(this), slot](const juce::FileChooser& fc) {
                              if (safe != nullptr && fc.getResult() != juce::File()) safe->loadSampleFile(slot, fc.getResult());
                          });
}

void Editor::loadSampleFile(Sample slot, const juce::File& file) {
    const juce::String name = slot == Sample::A ? "A" : "B";
    if (proc_.loadSample(slot, file)) showStatus("SAMPLE " + name + ": " + file.getFileName(), Tone::Good);
    else showStatus("CANNOT LOAD " + file.getFileName() + " (WAV, AIF, MP3, FLAC)", Tone::Warn);
    refresh();
}

void Editor::variate() {
    if (!proc_.variate()) {
        showStatus("NOTHING TO VARY: CHOOSE A ROLL TYPE FIRST", Tone::Warn);
    } else {
        const auto p = proc_.currentPreset();
        showStatus("VARIATE  INTENSITY " + juce::String(p.intensity, 1) + " %  BLEND " + juce::String(p.blend, 1) + " %");
    }
    refresh();
}

void Editor::undoVariate() {
    if (proc_.undoVariate()) showStatus("UNDO VARIATE");
    refresh();
}

void Editor::redoVariate() {
    if (proc_.redoVariate()) showStatus("REDO VARIATE");
    refresh();
}

void Editor::clearRoll() {
    if (!proc_.needsClearConfirmation()) {
        clearRollNow();
        return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle("Clear the roll")
                                     .withMessage("The preset has unsaved changes. Empty the roll anyway?")
                                     .withButton("CLEAR")
                                     .withButton("CANCEL")
                                     .withAssociatedComponent(this),
                                 [safe = juce::Component::SafePointer<Editor>(this)](int result) {
                                     if (safe != nullptr && result == 1) safe->clearRollNow();
                                 });
}

void Editor::clearRollNow() {
    proc_.clearRoll();
    showStatus("ROLL CLEARED");
    refresh();
}

void Editor::toggleLearn() {
    if (proc_.isMidiLearning()) {
        proc_.cancelMidiLearn();
        showStatus("MIDI LEARN CANCELLED");
    } else {
        proc_.startMidiLearn();
    }
    refresh();
}

void Editor::noteEdited() {
    if (const auto note = parseNote(note_.getText().toStdString())) {
        proc_.setTriggerNote(*note);
        showStatus("TRIGGER NOTE " + juce::String(noteName(*note)), Tone::Good);
    } else {
        showStatus("NOT A NOTE: TRY C1 OR 36", Tone::Warn);
    }
    refresh();
}

// ---- status and refresh ----

void Editor::showStatus(const juce::String& text, Tone tone) {
    statusMessage_ = text;
    statusTone_ = tone;
    statusUntil_ = juce::Time::getMillisecondCounter() + 5000;
}

juce::String Editor::statusText() const {
    if (proc_.isMidiLearning()) return "MIDI LEARN: PLAY A NOTE (ESC CANCELS)";
    if (juce::Time::getMillisecondCounter() < statusUntil_ && statusMessage_.isNotEmpty()) return statusMessage_;
    if (hint_.isNotEmpty()) return hint_;
    return "READY";
}

juce::String Editor::presetLabel() const {
    return juce::String(proc_.currentPreset().name) + (proc_.isDirty() ? " *" : "");
}

void Editor::timerCallback() { refresh(); }

void Editor::refresh() {
    const auto preset = proc_.currentPreset();
    presetName_.setText(juce::String(preset.name) + (proc_.isDirty() ? " *" : ""), juce::dontSendNotification);

    undo_.setEnabled(proc_.canUndoVariate());
    redo_.setEnabled(proc_.canRedoVariate());
    variate_.setEnabled(preset.rollType.has_value());
    save_.setEnabled(proc_.presetFile() != juce::File());

    if (!note_.isBeingEdited()) note_.setText(noteName(proc_.triggerNote()), juce::dontSendNotification);
    learn_.setToggleState(proc_.isMidiLearning(), juce::dontSendNotification);

    for (int i = 0; i < 2; ++i) {
        const auto& slot = i == 0 ? preset.sampleA : preset.sampleB;
        sampleName_[i].setText(slot ? juce::String(slot->name) : juce::String("- NO SAMPLE -"), juce::dontSendNotification);
        sampleName_[i].setColour(juce::Label::textColourId, slot ? kAmber : kDim);
    }

    const bool learning = proc_.isMidiLearning();
    const bool message = juce::Time::getMillisecondCounter() < statusUntil_ && statusMessage_.isNotEmpty();
    status_.setText(statusText(), juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId,
                      learning ? kAmber
                      : message ? (statusTone_ == Tone::Warn ? kWarn : statusTone_ == Tone::Good ? kGreen : kAmber)
                                : kDim);
}

}  // namespace dw::ui
