#include <catch2/catch_test_macros.hpp>

#include "engine/NoteNames.h"

using namespace dw;

TEST_CASE("note names use C3 = 60, so the default trigger note is C1", "[notes]") {
    CHECK(noteName(36) == "C1");
    CHECK(noteName(60) == "C3");
    CHECK(noteName(69) == "A3");
    CHECK(noteName(0) == "C-2");
    CHECK(noteName(127) == "G8");
    CHECK(noteName(61) == "C#3");
    CHECK(noteName(35) == "B0");
}

TEST_CASE("notes outside 0..127 are clamped when named", "[notes]") {
    CHECK(noteName(-5) == "C-2");
    CHECK(noteName(300) == "G8");
}

TEST_CASE("note names are read back in several spellings", "[notes]") {
    CHECK(parseNote("C1") == 36);
    CHECK(parseNote("c1") == 36);
    CHECK(parseNote("C#1") == 37);
    CHECK(parseNote("Db1") == 37);
    CHECK(parseNote("  A3 ") == 69);
    CHECK(parseNote("C-2") == 0);
    CHECK(parseNote("G8") == 127);
}

TEST_CASE("plain numbers are notes too", "[notes]") {
    CHECK(parseNote("36") == 36);
    CHECK(parseNote("0") == 0);
    CHECK(parseNote("127") == 127);
    CHECK(parseNote(" 60 ") == 60);
}

TEST_CASE("invalid input gives no note", "[notes]") {
    for (const char* text : {"", " ", "H1", "C", "C9", "G#8", "128", "-1", "C1x", "1C", "C##1", "x", "3.5", "Cb-2"}) {
        INFO(text);
        CHECK_FALSE(parseNote(text).has_value());
    }
}

TEST_CASE("every note survives a round trip through its name", "[notes]") {
    for (int n = 0; n <= 127; ++n) {
        INFO(n);
        CHECK(parseNote(noteName(n)) == n);
    }
}
