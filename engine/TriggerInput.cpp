#include "engine/TriggerInput.h"

namespace dw {

TriggerInput::Action TriggerInput::handle(bool noteOn, bool noteOff, int noteNumber) {
    // The first Note-On after startLearn() takes over as trigger note. exchange() makes sure that
    // only one Note-On can win when Learn is cancelled or restarted at the same time.
    if (noteOn && learning_.exchange(false)) {
        setNote(noteNumber);
        return Action::Learned;
    }
    if (noteNumber != note_.load()) return Action::Ignore;
    if (noteOn) return Action::NoteOn;
    if (noteOff) return Action::NoteOff;
    return Action::Ignore;
}

}  // namespace dw
