#include <catch2/catch_test_macros.hpp>

#include "engine/TriggerInput.h"

using namespace dw;
using Action = TriggerInput::Action;

TEST_CASE("the default trigger note is C1", "[midi]") {
    TriggerInput in;
    CHECK(in.note() == 36);
    CHECK_FALSE(in.isLearning());
}

TEST_CASE("only Note-On and Note-Off of the trigger note count", "[midi]") {
    TriggerInput in;
    CHECK(in.handle(true, false, 36) == Action::NoteOn);
    CHECK(in.handle(false, true, 36) == Action::NoteOff);
    CHECK(in.handle(true, false, 37) == Action::Ignore);
    CHECK(in.handle(false, true, 35) == Action::Ignore);
    CHECK(in.handle(false, false, 36) == Action::Ignore);  // neither (aftertouch, CC, ...)
}

TEST_CASE("the trigger note can be typed in and is kept in 0..127", "[midi]") {
    TriggerInput in;
    in.setNote(60);
    CHECK(in.note() == 60);
    CHECK(in.handle(true, false, 60) == Action::NoteOn);
    CHECK(in.handle(true, false, 36) == Action::Ignore);
    in.setNote(-5);
    CHECK(in.note() == 0);
    in.setNote(300);
    CHECK(in.note() == 127);
}

TEST_CASE("Learn takes the next Note-On as the trigger note and ends", "[midi][learn]") {
    TriggerInput in;
    in.startLearn();
    CHECK(in.isLearning());
    CHECK(in.handle(true, false, 48) == Action::Learned);
    CHECK(in.note() == 48);
    CHECK_FALSE(in.isLearning());
    // from now on 48 triggers, the old note does not
    CHECK(in.handle(true, false, 48) == Action::NoteOn);
    CHECK(in.handle(true, false, 36) == Action::Ignore);
}

TEST_CASE("the learned Note-On does not start a roll, the next one does", "[midi][learn]") {
    TriggerInput in;
    in.startLearn();
    CHECK(in.handle(true, false, 50) == Action::Learned);
    CHECK(in.handle(true, false, 50) == Action::NoteOn);
}

TEST_CASE("while learning, a Note-Off does not end Learn", "[midi][learn]") {
    TriggerInput in;
    in.startLearn();
    CHECK(in.handle(false, true, 70) == Action::Ignore);
    CHECK(in.isLearning());
    CHECK(in.note() == 36);
}

TEST_CASE("Learn can be cancelled and the note stays", "[midi][learn]") {
    TriggerInput in;
    in.setNote(40);
    in.startLearn();
    in.cancelLearn();
    CHECK_FALSE(in.isLearning());
    CHECK(in.handle(true, false, 90) == Action::Ignore);
    CHECK(in.note() == 40);
}

TEST_CASE("starting Learn again while learning changes nothing", "[midi][learn]") {
    TriggerInput in;
    in.startLearn();
    in.startLearn();
    CHECK(in.isLearning());
    CHECK(in.handle(true, false, 61) == Action::Learned);
    CHECK_FALSE(in.isLearning());
}

TEST_CASE("typing a note while learning does not end Learn", "[midi][learn]") {
    TriggerInput in;
    in.startLearn();
    in.setNote(55);
    CHECK(in.isLearning());
    CHECK(in.handle(true, false, 66) == Action::Learned);
    CHECK(in.note() == 66);
}
