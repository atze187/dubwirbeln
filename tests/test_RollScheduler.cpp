#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "engine/RollScheduler.h"

using namespace dw;

// Test setup: 48 kHz, 120 BPM -> one 1/64 unit is 1500 samples, one beat 24000 samples.
// BUILD at intensity 100 with a 0.5 bar window is 32 triggers of 1/64, one per unit
// (reference positions 224..255), sample row BABA at blend 50.
namespace {

constexpr double kRate = 48000.0;
constexpr int kUnit = 1500;

RollConfig baseConfig() {
    RollConfig c;
    c.spec = {RollType::Build, 100.0, 50.0};
    c.expression.curve = {{0.0, 100.0}, {1.0, 100.0}};
    c.playback.windowBars = 0.5;
    c.playback.follow = VelocityFollow::Off;
    return c;
}

NoteEvent noteOn(int offset, int velocity = 100) { return {offset, true, velocity}; }
NoteEvent noteOff(int offset) { return {offset, false, 0}; }

Transport stopped(double bpm = 120.0) {
    Transport t;
    t.bpm = bpm;
    return t;
}

Transport playingAt(double ppq, double bpm = 120.0) {
    Transport t;
    t.bpm = bpm;
    t.playing = true;
    t.ppqPosition = ppq;
    return t;
}

std::vector<int> triggerOffsets(const std::vector<SchedulerEvent>& events) {
    std::vector<int> out;
    for (const auto& e : events)
        if (e.kind == SchedulerEvent::Kind::Trigger) out.push_back(e.sampleOffset);
    return out;
}

std::vector<int> unitMultiples(int first, int count, int base = 0) {
    std::vector<int> out;
    for (int i = 0; i < count; ++i) out.push_back(base + (first + i) * kUnit);
    return out;
}

}  // namespace

TEST_CASE("a roll starts at the note-on sample and follows the 1/64 grid", "[scheduler]") {
    RollScheduler s(kRate);
    s.setConfig(baseConfig());
    const auto ev = s.process(4096, {noteOn(10)}, stopped());
    REQUIRE(triggerOffsets(ev) == std::vector<int>{10, 10 + kUnit, 10 + 2 * kUnit});
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].sample == Sample::B);
    CHECK(ev[1].sample == Sample::A);
    CHECK(ev[2].sample == Sample::B);
    for (const auto& e : ev) CHECK(e.velocity == 100);
    CHECK(s.isRolling());
}

TEST_CASE("the roll continues across blocks without drift", "[scheduler]") {
    RollScheduler s(kRate);
    s.setConfig(baseConfig());
    s.process(4096, {noteOn(10)}, stopped());
    CHECK(triggerOffsets(s.process(4096, {}, stopped())) == std::vector<int>{414, 1914, 3414});
    CHECK(triggerOffsets(s.process(4096, {}, stopped())) == std::vector<int>{818, 2318, 3818});
}

TEST_CASE("Trigger mode plays the whole window and ignores Note-Off", "[scheduler][mode]") {
    RollScheduler s(kRate);
    s.setConfig(baseConfig());
    int count = 0;
    auto run = [&](std::vector<NoteEvent> notes) {
        const auto ev = s.process(4800, notes, stopped());
        for (const auto& e : ev) {
            CHECK(e.kind == SchedulerEvent::Kind::Trigger);
            ++count;
        }
    };
    run({noteOn(0), noteOff(100)});
    for (int i = 0; i < 20; ++i) run({});
    CHECK(count == 32);
    CHECK_FALSE(s.isRolling());
}

TEST_CASE("Hold mode: Note-Off stops the roll at once", "[scheduler][mode]") {
    auto cfg = baseConfig();
    cfg.playback.mode = PlaybackMode::Hold;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(8192, {noteOn(0), noteOff(4000)}, stopped());
    REQUIRE(ev.size() == 4);  // triggers at 0, 1500, 3000 and the stop at 4000
    CHECK(ev[3].kind == SchedulerEvent::Kind::Stop);
    CHECK(ev[3].sampleOffset == 4000);
    CHECK_FALSE(s.isRolling());
    CHECK(s.process(8192, {}, stopped()).empty());
}

TEST_CASE("Hold mode rolls forward, then backward, then stops by itself", "[scheduler][mode]") {
    auto cfg = baseConfig();
    cfg.playback.mode = PlaybackMode::Hold;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    std::vector<int> times;
    std::vector<Sample> samples;
    int base = 0;
    auto run = [&](std::vector<NoteEvent> notes) {
        for (const auto& e : s.process(6000, notes, stopped())) {
            REQUIRE(e.kind == SchedulerEvent::Kind::Trigger);
            times.push_back(base + e.sampleOffset);
            samples.push_back(e.sample);
        }
        base += 6000;
    };
    run({noteOn(0)});
    for (int i = 0; i < 20; ++i) run({});
    // forward: units 0..31, backward: mirrored units 33..63 (the first trigger is not repeated)
    REQUIRE(times.size() == 63);
    CHECK(times[31] == 31 * kUnit);
    CHECK(times[32] == 33 * kUnit);
    CHECK(times[62] == 63 * kUnit);
    CHECK(samples[32] == Sample::A);  // the last forward trigger (unit 31) is an A
    CHECK(samples[62] == Sample::A);  // unit 1 of the row BABA
    CHECK_FALSE(s.isRolling());
}

TEST_CASE("Follow VELOCITY scales the trigger velocities", "[scheduler][follow]") {
    auto cfg = baseConfig();
    cfg.playback.follow = VelocityFollow::Velocity;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(2000, {noteOn(0, 64)}, stopped());
    REQUIRE_FALSE(ev.empty());
    for (const auto& e : ev) CHECK(e.velocity == 50);
}

TEST_CASE("Follow VELOCITY + INTENSITY regenerates the roll from the played velocity", "[scheduler][follow]") {
    auto cfg = baseConfig();
    cfg.playback.follow = VelocityFollow::VelocityIntensity;
    cfg.playback.windowBars = 4.0;
    RollScheduler s(kRate);
    s.setConfig(cfg);

    // Velocity 1 means intensity 0: BUILD 1/8 -> 1/16, so the first trigger is on the 1/8 grid.
    auto expected = generateRoll({RollType::Build, 0.0, 50.0});
    const auto ev = s.process(30000, {noteOn(0, 1)}, stopped());
    REQUIRE(ev.size() >= 3);
    CHECK(ev[0].sampleOffset == 0);
    CHECK(ev[1].sampleOffset == expected[1].position * kUnit);
    CHECK(ev[2].sampleOffset == expected[2].position * kUnit);
    CHECK(expected[1].position == 8);
}

TEST_CASE("a new note replaces the running roll immediately", "[scheduler]") {
    RollScheduler s(kRate);
    s.setConfig(baseConfig());
    const auto ev = s.process(6000, {noteOn(0), noteOn(3000)}, stopped());
    CHECK(triggerOffsets(ev) == std::vector<int>{0, 1500, 3000, 4500});
}

TEST_CASE("tempo changes are followed from the next block on", "[scheduler][tempo]") {
    RollScheduler s(kRate);
    s.setConfig(baseConfig());
    s.process(1000, {noteOn(0)}, stopped(120.0));  // trigger 0 at 0, next at 1500
    // 1000 samples are 2/3 of a unit at 120 BPM. At 240 BPM a unit is 750 samples,
    // so the remaining 1/3 unit takes 250 samples, then one trigger every 750.
    const auto ev = s.process(3000, {}, stopped(240.0));
    CHECK(triggerOffsets(ev) == std::vector<int>{250, 1000, 1750, 2500});
}

TEST_CASE("swing delays off-beat triggers by the planned fraction of a unit", "[scheduler][expression]") {
    auto cfg = baseConfig();
    cfg.expression.swing = 100.0;  // unit steps: every odd reference position is delayed by 1/3 unit
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(4096, {noteOn(0)}, stopped());
    CHECK(triggerOffsets(ev) == std::vector<int>{0, 2000, 3000});
}

TEST_CASE("humanize timing is applied per trigger index and never starts before the note", "[scheduler][expression]") {
    auto cfg = baseConfig();
    cfg.expression.humanize = 100.0;
    cfg.expression.humanizeSeed = 1234;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(60000, {noteOn(0)}, stopped());

    // Expected from the engine pieces: windowed full roll, ms converted at 120 BPM (48 samples per ms).
    const auto full = expressTriggers(generateRoll(cfg.spec), cfg.expression);
    const auto win = applyWindow(full, cfg.spec.type, cfg.playback.windowBars);
    std::vector<int> expected;
    for (const auto& e : win) {
        const double samples = e.trigger.position * kUnit + e.humanizeMs * 48.0;
        expected.push_back(std::max(0, static_cast<int>(std::ceil(samples - 1e-6))));
    }
    std::sort(expected.begin(), expected.end());
    REQUIRE(win.size() == 32);
    CHECK(triggerOffsets(ev) == expected);
}

TEST_CASE("quantize waits for the next grid position and never starts earlier", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Sixteenth;  // 0.25 beat = 6000 samples
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(8192, {noteOn(0)}, playingAt(0.1));
    // next grid at ppq 0.25: (0.25 - 0.1) * 24000 = 3600 samples
    CHECK(triggerOffsets(ev) == std::vector<int>{3600, 5100, 6600, 8100});
}

TEST_CASE("a note exactly on the grid starts at once", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Quarter;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(3000, {noteOn(0)}, playingAt(2.0));
    CHECK(triggerOffsets(ev) == std::vector<int>{0, 1500});
}

TEST_CASE("a pending start survives several blocks and fires on the bar line", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Bar;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    CHECK(s.process(24000, {noteOn(100)}, playingAt(0.0)).empty());
    CHECK(s.isPending());
    CHECK_FALSE(s.isRolling());
    CHECK(s.process(24000, {}, playingAt(1.0)).empty());
    CHECK(s.process(24000, {}, playingAt(2.0)).empty());
    CHECK(s.process(24000, {}, playingAt(3.0)).empty());
    const auto ev = s.process(3000, {}, playingAt(4.0));
    CHECK(triggerOffsets(ev) == std::vector<int>{0, 1500});
    CHECK_FALSE(s.isPending());
    CHECK(s.isRolling());
}

TEST_CASE("quantize is ignored while the host is stopped", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Bar;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(3000, {noteOn(200)}, stopped());
    CHECK(triggerOffsets(ev) == std::vector<int>{200, 1700});
}

TEST_CASE("a newer note replaces the pending start", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Quarter;  // grid at ppq 1.0 = sample 24000
    cfg.expression.curve = {{0.0, 100.0}, {1.0, 100.0}};
    cfg.playback.follow = VelocityFollow::Velocity;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(30000, {noteOn(100, 127), noteOn(500, 64)}, playingAt(0.0));
    REQUIRE_FALSE(ev.empty());
    CHECK(ev[0].sampleOffset == 24000);
    CHECK(ev[0].velocity == 50);  // the second note won
}

TEST_CASE("Hold mode cancels a pending start when the key is released early", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.mode = PlaybackMode::Hold;
    cfg.playback.quantize = Quantize::Quarter;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    CHECK(s.process(10000, {noteOn(0), noteOff(5000)}, playingAt(0.1)).empty());
    CHECK_FALSE(s.isPending());
    CHECK(s.process(30000, {}, playingAt(0.5)).empty());
}

TEST_CASE("Trigger mode starts the quantized roll even after Note-Off", "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Quarter;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    const auto ev = s.process(26000, {noteOn(0), noteOff(5000)}, playingAt(0.1));
    // grid at ppq 1.0: (1.0 - 0.1) * 24000 = 21600
    CHECK(triggerOffsets(ev) == std::vector<int>{21600, 23100, 24600});
}

TEST_CASE("with quantize a running roll continues until the grid position, then it is replaced",
          "[scheduler][quantize]") {
    auto cfg = baseConfig();
    cfg.playback.quantize = Quantize::Quarter;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    // The first note is on the grid (ppq 0.0). The second one at sample 1000 waits for ppq 1.0 = sample 24000.
    const auto ev = s.process(30000, {noteOn(0), noteOn(1000)}, playingAt(0.0));
    std::vector<int> expected = unitMultiples(0, 16);  // old roll: 0 .. 22500
    for (int i = 0; i < 4; ++i) expected.push_back(24000 + i * kUnit);
    CHECK(triggerOffsets(ev) == expected);
}

TEST_CASE("reset silences the scheduler", "[scheduler]") {
    RollScheduler s(kRate);
    s.setConfig(baseConfig());
    s.process(1000, {noteOn(0)}, stopped());
    REQUIRE(s.isRolling());
    s.reset();
    CHECK_FALSE(s.isRolling());
    CHECK_FALSE(s.isPending());
    CHECK(s.process(4000, {}, stopped()).empty());
}

TEST_CASE("a disabled config (no roll type) stays silent", "[scheduler]") {
    auto cfg = baseConfig();
    cfg.enabled = false;
    RollScheduler s(kRate);
    s.setConfig(cfg);
    CHECK(s.process(4096, {noteOn(0)}, stopped()).empty());
    CHECK_FALSE(s.isRolling());
    cfg.playback.quantize = Quantize::Quarter;
    s.setConfig(cfg);
    CHECK(s.process(4096, {noteOn(0)}, playingAt(0.1)).empty());
    CHECK_FALSE(s.isPending());
}
