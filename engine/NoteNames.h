#pragma once

#include <optional>
#include <string>

namespace dw {

// MIDI note names with C3 = 60, so the default trigger note 36 is C1 (spec Part 9): C-2 is note 0
// and G8 is note 127. Sharps are written with '#'.
std::string noteName(int note);

// Reads "C1", "c#2", "Db3" or a plain number 0..127 (spaces around it are fine). Returns nothing
// for anything else and for notes outside 0..127.
std::optional<int> parseNote(const std::string& text);

}  // namespace dw
