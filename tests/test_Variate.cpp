#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Variate.h"

using Catch::Approx;
using namespace dw;

namespace {

Preset basePreset() {
    Preset p = defaultPreset(4711);
    p.name = "Variate test";
    p.rollType = RollType::Build;
    p.intensity = 70.0;
    p.blend = 50.0;
    p.curve = {{0.0, 100.0}, {1.0, 100.0}};
    return p;
}

bool isTenth(double x) { return std::fabs(x * 10.0 - std::round(x * 10.0)) < 1e-6; }

}  // namespace

TEST_CASE("a pass matches the independent reference", "[variate]") {
    // Python PCG32, stream 7, pass seed 77: intensity +14.505, blend -19.506, curve points -2 and +6
    Preset p = basePreset();
    REQUIRE(applyVariate(p, 77));
    CHECK(p.intensity == Approx(84.5));
    CHECK(p.blend == Approx(30.5));
    REQUIRE(p.curve.size() == 2);
    CHECK(p.curve[0].v == Approx(98.0));
    CHECK(p.curve[1].v == Approx(106.0));
    CHECK(p.variationSeed == 77u);
}

TEST_CASE("a pass is deterministic", "[variate]") {
    Preset a = basePreset();
    Preset b = basePreset();
    REQUIRE(applyVariate(a, 4242));
    REQUIRE(applyVariate(b, 4242));
    CHECK(presetToJson(a) == presetToJson(b));
    Preset c = basePreset();
    REQUIRE(applyVariate(c, 4243));
    CHECK(presetToJson(a) != presetToJson(c));
}

TEST_CASE("pass seed 0 is replaced, because 0 means no variation", "[variate]") {
    Preset p = basePreset();
    REQUIRE(applyVariate(p, 0));
    CHECK(p.variationSeed == 1u);
}

TEST_CASE("a preset without a roll is not varied", "[variate]") {
    Preset p = basePreset();
    p.rollType.reset();
    const auto before = presetToJson(p);
    CHECK_FALSE(applyVariate(p, 77));
    CHECK(presetToJson(p) == before);
}

TEST_CASE("Intensity moves by at most 15 points and stays in its zone", "[variate][zone]") {
    struct Zone {
        double lo, hi;
    };
    const std::pair<double, Zone> starts[] = {{0.0, {0.0, 24.9}},   {10.0, {0.0, 24.9}},  {24.9, {0.0, 24.9}},
                                              {25.0, {25.0, 49.9}}, {40.0, {25.0, 49.9}}, {49.9, {25.0, 49.9}},
                                              {50.0, {50.0, 100.0}}, {75.0, {50.0, 100.0}}, {100.0, {50.0, 100.0}}};
    for (const auto& [start, zone] : starts) {
        INFO("start " << start);
        for (std::uint32_t seed = 1; seed <= 400; ++seed) {
            Preset p = basePreset();
            p.intensity = start;
            REQUIRE(applyVariate(p, seed));
            CHECK(p.intensity >= zone.lo - 1e-9);
            CHECK(p.intensity <= zone.hi + 1e-9);
            CHECK(std::fabs(p.intensity - start) <= 15.0 + 0.05);
            CHECK(isTenth(p.intensity));
        }
    }
}

TEST_CASE("Blend moves by at most 20 points and stays in 0 to 100", "[variate]") {
    for (double start : {0.0, 3.0, 50.0, 97.0, 100.0}) {
        INFO("start " << start);
        for (std::uint32_t seed = 1; seed <= 400; ++seed) {
            Preset p = basePreset();
            p.blend = start;
            REQUIRE(applyVariate(p, seed));
            CHECK(p.blend >= 0.0);
            CHECK(p.blend <= 100.0);
            CHECK(std::fabs(p.blend - start) <= 20.0 + 0.05);
            CHECK(isTenth(p.blend));
        }
    }
}

TEST_CASE("curve points move by at most 10 and stay between 1 and 127", "[variate]") {
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        Preset p = basePreset();
        p.curveType = CurveType::Ghost;
        p.curve = {{0.0, 1.0}, {0.25, 5.0}, {0.5, 64.0}, {0.75, 123.0}, {1.0, 127.0}};
        const auto before = p.curve;
        REQUIRE(applyVariate(p, seed));
        REQUIRE(p.curve.size() == before.size());
        CHECK(p.curveType == CurveType::Ghost);
        for (size_t i = 0; i < before.size(); ++i) {
            CHECK(p.curve[i].t == before[i].t);
            CHECK(p.curve[i].v >= 1.0);
            CHECK(p.curve[i].v <= 127.0);
            CHECK(std::fabs(p.curve[i].v - before[i].v) <= 10.0);
            CHECK(p.curve[i].v == std::round(p.curve[i].v));
        }
    }
}

TEST_CASE("the changes spread in both directions", "[variate]") {
    double minDelta = 0.0, maxDelta = 0.0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        Preset p = basePreset();
        p.intensity = 75.0;
        REQUIRE(applyVariate(p, seed));
        minDelta = std::min(minDelta, p.intensity - 75.0);
        maxDelta = std::max(maxDelta, p.intensity - 75.0);
    }
    CHECK(minDelta < -10.0);
    CHECK(maxDelta > 10.0);
}

TEST_CASE("everything else stays as it was", "[variate]") {
    Preset p = basePreset();
    p.gesture = {{0.0, 0.0}, {1.0, 1.0}};
    p.mode = PlaybackMode::Hold;
    p.windowBars = 2.0;
    p.quantize = Quantize::Eighth;
    p.humanize = 30.0;
    p.swing = 20.0;
    p.velocityFollow = VelocityFollow::Velocity;
    p.sampleA = SampleSlot{"kick.wav", std::string(64, 'a'), "samples/kick.wav", 1.0, 0.1, 0.9, -3.0};
    const Preset original = p;
    REQUIRE(applyVariate(p, 99));
    restoreSnapshot(p, variateSnapshot(original));  // put the four changed values back
    CHECK(presetToJson(p) == presetToJson(original));
}

TEST_CASE("passes are relative to the current state and drift", "[variate]") {
    Preset once = basePreset();
    REQUIRE(applyVariate(once, 77));
    Preset twice = once;
    REQUIRE(applyVariate(twice, 78));
    // the second pass starts from the first result (it is not applied to the original again)
    Preset fresh = basePreset();
    REQUIRE(applyVariate(fresh, 78));
    CHECK(twice.intensity != Approx(fresh.intensity));
    CHECK(std::fabs(twice.intensity - once.intensity) <= 15.05);
    CHECK(twice.intensity >= 50.0);
    CHECK(twice.variationSeed == 78u);  // each pass replaces the seed
}

TEST_CASE("a snapshot holds the four changed values and can be restored", "[variate]") {
    Preset p = basePreset();
    p.variationSeed = 5;
    const auto snap = variateSnapshot(p);
    CHECK(snap.intensity == Approx(70.0));
    CHECK(snap.blend == Approx(50.0));
    CHECK(snap.curve == p.curve);
    CHECK(snap.variationSeed == 5u);

    REQUIRE(applyVariate(p, 31));
    restoreSnapshot(p, snap);
    CHECK(variateSnapshot(p) == snap);
}

TEST_CASE("undo and redo cover the last pass", "[variate][history]") {
    Preset p = basePreset();
    VariateHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK_FALSE(history.undo(p));
    CHECK_FALSE(history.redo(p));

    const auto before = variateSnapshot(p);
    REQUIRE(applyVariate(p, 21));
    const auto after = variateSnapshot(p);
    history.record(before, after);
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());

    REQUIRE(history.undo(p));
    CHECK(variateSnapshot(p) == before);
    CHECK_FALSE(history.canUndo());
    CHECK(history.canRedo());
    CHECK_FALSE(history.undo(p));  // a single level

    REQUIRE(history.redo(p));
    CHECK(variateSnapshot(p) == after);
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("a new pass replaces the remembered one, there is no chain", "[variate][history]") {
    Preset p = basePreset();
    VariateHistory history;
    const auto start = variateSnapshot(p);

    REQUIRE(applyVariate(p, 1));
    const auto afterFirst = variateSnapshot(p);
    history.record(start, afterFirst);
    REQUIRE(applyVariate(p, 2));
    history.record(afterFirst, variateSnapshot(p));

    REQUIRE(history.undo(p));
    CHECK(variateSnapshot(p) == afterFirst);  // only the last pass is undone
    CHECK_FALSE(history.undo(p));
}

TEST_CASE("recording after an undo drops the redo", "[variate][history]") {
    Preset p = basePreset();
    VariateHistory history;
    const auto start = variateSnapshot(p);
    REQUIRE(applyVariate(p, 1));
    history.record(start, variateSnapshot(p));
    REQUIRE(history.undo(p));
    REQUIRE(history.canRedo());

    const auto again = variateSnapshot(p);
    REQUIRE(applyVariate(p, 2));
    history.record(again, variateSnapshot(p));
    CHECK_FALSE(history.canRedo());
    CHECK(history.canUndo());
}

TEST_CASE("undo only touches the variate values", "[variate][history]") {
    Preset p = basePreset();
    VariateHistory history;
    const auto before = variateSnapshot(p);
    REQUIRE(applyVariate(p, 8));
    history.record(before, variateSnapshot(p));
    p.name = "Renamed meanwhile";
    p.humanize = 55.0;
    REQUIRE(history.undo(p));
    CHECK(p.name == "Renamed meanwhile");
    CHECK(p.humanize == Approx(55.0));
    CHECK(p.intensity == Approx(70.0));
}

TEST_CASE("clearing the history forgets the pass", "[variate][history]") {
    Preset p = basePreset();
    VariateHistory history;
    const auto before = variateSnapshot(p);
    REQUIRE(applyVariate(p, 8));
    history.record(before, variateSnapshot(p));
    history.clear();
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("clear empties only the roll", "[variate][clear]") {
    Preset p = basePreset();
    p.gesture = {{0.0, 0.0}, {1.0, 1.0}};
    p.variationSeed = 99;
    p.intensity = 88.0;
    p.blend = 12.0;
    p.mode = PlaybackMode::Hold;
    p.windowBars = 1.0;
    p.quantize = Quantize::Quarter;
    p.humanize = 30.0;
    p.swing = 20.0;
    p.velocityFollow = VelocityFollow::Off;
    p.curveType = CurveType::Spike;
    p.curve = curveTemplate(CurveType::Spike);
    p.sampleA = SampleSlot{"kick.wav", std::string(64, 'a'), "samples/kick.wav", 2.0, 0.1, 0.9, -3.0};
    p.sampleB = SampleSlot{"snare.wav", std::string(64, 'b'), "samples/snare.wav", 0.0, 0.0, 1.0, 0.0};
    const Preset before = p;

    clearRoll(p);

    CHECK_FALSE(p.rollType.has_value());
    CHECK(p.gesture.empty());
    CHECK(p.intensity == Approx(50.0));
    CHECK(p.blend == Approx(50.0));
    CHECK(p.variationSeed == 0u);

    CHECK(p.humanizeSeed == before.humanizeSeed);
    CHECK(p.name == before.name);
    CHECK(p.mode == PlaybackMode::Hold);
    CHECK(p.windowBars == Approx(1.0));
    CHECK(p.quantize == Quantize::Quarter);
    CHECK(p.humanize == Approx(30.0));
    CHECK(p.swing == Approx(20.0));
    CHECK(p.velocityFollow == VelocityFollow::Off);
    CHECK(p.curveType == CurveType::Spike);
    CHECK(p.curve == before.curve);
    REQUIRE(p.sampleA.has_value());
    CHECK(p.sampleA->tuneSemitones == Approx(2.0));
    CHECK(p.sampleA->start == Approx(0.1));
    CHECK(p.sampleB.has_value());
}
