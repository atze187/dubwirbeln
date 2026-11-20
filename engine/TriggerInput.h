#pragma once

#include <atomic>

namespace dw {

constexpr int kDefaultTriggerNoteNumber = 36;  // C1 (spec Part 9)

// The trigger note and MIDI Learn (spec Part 9). The note is state of the plugin instance, not of a
// preset. All members are lock-free atomics: the UI thread starts and cancels Learn and sets the
// note, the audio thread classifies the incoming messages.
class TriggerInput {
public:
    enum class Action {
        Ignore,   // not for us
        NoteOn,   // start a roll
        NoteOff,  // release (Hold mode)
        Learned   // this Note-On became the trigger note; it does not start a roll
    };

    int note() const { return note_.load(); }
    void setNote(int note) { note_.store(note < 0 ? 0 : note > 127 ? 127 : note); }

    void startLearn() { learning_.store(true); }
    void cancelLearn() { learning_.store(false); }
    bool isLearning() const { return learning_.load(); }

    // Audio thread. `noteOn` is a Note-On with a velocity above 0, `noteOff` a Note-Off (or a Note-On
    // with velocity 0). Any channel counts (omni).
    Action handle(bool noteOn, bool noteOff, int noteNumber);

private:
    std::atomic<int> note_{kDefaultTriggerNoteNumber};
    std::atomic<bool> learning_{false};
};

}  // namespace dw
