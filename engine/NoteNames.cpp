#include "engine/NoteNames.h"

#include <algorithm>
#include <cctype>

namespace dw {

namespace {

const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string trimmed(const std::string& s) {
    size_t begin = 0, end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

}  // namespace

std::string noteName(int note) {
    note = std::clamp(note, 0, 127);
    return std::string(kNames[note % 12]) + std::to_string(note / 12 - 2);
}

std::optional<int> parseNote(const std::string& text) {
    const std::string s = trimmed(text);
    if (s.empty()) return std::nullopt;

    if (std::isdigit(static_cast<unsigned char>(s[0]))) {
        if (!allDigits(s) || s.size() > 3) return std::nullopt;
        const int n = std::stoi(s);
        return n <= 127 ? std::optional<int>(n) : std::nullopt;
    }

    int semitone = 0;
    switch (std::toupper(static_cast<unsigned char>(s[0]))) {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default: return std::nullopt;
    }
    size_t i = 1;
    if (i < s.size() && s[i] == '#') {
        ++semitone;
        ++i;
    } else if (i < s.size() && s[i] == 'b') {
        --semitone;
        ++i;
    }

    bool negative = false;
    if (i < s.size() && s[i] == '-') {
        negative = true;
        ++i;
    }
    const std::string digits = s.substr(i);
    if (!allDigits(digits) || digits.size() > 2) return std::nullopt;
    const int octave = (negative ? -1 : 1) * std::stoi(digits);

    const int note = (octave + 2) * 12 + semitone;
    return note >= 0 && note <= 127 ? std::optional<int>(note) : std::nullopt;
}

}  // namespace dw
