#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AllocGuard.h"
#include "engine/Preset.h"
#include "engine/SampleEngine.h"

using Catch::Approx;
using dw::test::AllocGuard;
using namespace dw;

// The audio thread must not allocate. The guard counts heap allocations of this thread; the
// checks run after the guard is gone.
namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

const RollType kTypes[] = {RollType::Build, RollType::Drop, RollType::Rush, RollType::Burst, RollType::Wave};

RollConfig config(RollType type, double intensity, PlaybackMode mode, Quantize quantize, VelocityFollow follow) {
    RollConfig c;
    c.spec = {type, intensity, 60.0, 77};
    c.expression.curve = curveTemplate(CurveType::Ghost);  // 12 points
    c.expression.humanize = 40.0;
    c.expression.swing = 30.0;
    c.expression.humanizeSeed = 9;
    c.playback.mode = mode;
    c.playback.windowBars = 2.0;
    c.playback.quantize = quantize;
    c.playback.follow = follow;
    return c;
}

SamplePtr dc(int frames) {
    auto d = std::make_shared<SampleData>();
    d->left.assign(static_cast<size_t>(frames), 0.5f);
    d->sampleRate = kRate;
    return d;
}

}  // namespace

TEST_CASE("the scheduler does not allocate while it rolls", "[realtime][scheduler]") {
    for (auto type : kTypes)
        for (auto mode : {PlaybackMode::Trigger, PlaybackMode::Hold})
            for (auto quantize : {Quantize::Off, Quantize::Quarter})
                for (auto follow : {VelocityFollow::Off, VelocityFollow::VelocityIntensity}) {
                    INFO("type " << static_cast<int>(type) << " mode " << static_cast<int>(mode) << " quantize "
                                 << static_cast<int>(quantize) << " follow " << static_cast<int>(follow));
                    RollScheduler scheduler(kRate);
                    scheduler.setConfig(config(type, 85.0, mode, quantize, follow));
                    EventList out;
                    Transport transport;
                    transport.playing = quantize != Quantize::Off;
                    NoteEvent none[1];

                    long allocations = 0;
                    std::size_t events = 0;
                    {
                        AllocGuard guard;
                        for (int block = 0; block < 80; ++block) {
                            NoteEvent notes[2];
                            int count = 0;
                            if (block == 0) notes[count++] = {10, true, 80};
                            if (block == 25) notes[count++] = {100, true, 30};  // replaces the running roll
                            if (block == 50) notes[count++] = {200, false, 0};
                            if (block == 60) notes[count++] = {5, true, 127};
                            scheduler.process(kBlock, count > 0 ? notes : none, count, transport, out);
                            events += out.size();
                            transport.ppqPosition += kBlock * transport.bpm / (60.0 * kRate);
                        }
                        allocations = guard.allocations();
                    }
                    CHECK(allocations == 0);
                    CHECK(events > 0);
                }
}

TEST_CASE("setting the config does not allocate", "[realtime][scheduler]") {
    RollScheduler scheduler(kRate);
    auto full = config(RollType::Build, 50.0, PlaybackMode::Trigger, Quantize::Off, VelocityFollow::Off);
    full.expression.curve.clear();
    for (int i = 0; i < 16; ++i) full.expression.curve.push_back({i / 15.0, 100.0});  // the most the schema allows
    auto small = full;
    small.expression.curve.resize(2);

    long allocations = 0;
    {
        AllocGuard guard;
        scheduler.setConfig(full);
        scheduler.setConfig(small);
        scheduler.setConfig(full);
        allocations = guard.allocations();
    }
    CHECK(allocations == 0);
}

TEST_CASE("resetting and setting the sample rate of the scheduler do not allocate", "[realtime][scheduler]") {
    RollScheduler scheduler(kRate);
    scheduler.setConfig(config(RollType::Build, 50.0, PlaybackMode::Trigger, Quantize::Off, VelocityFollow::Off));
    EventList out;
    NoteEvent on{0, true, 100};
    scheduler.process(kBlock, &on, 1, Transport{}, out);
    long allocations = 0;
    {
        AllocGuard guard;
        scheduler.reset();
        scheduler.setSampleRate(44100.0);
        allocations = guard.allocations();
    }
    CHECK(allocations == 0);
}

TEST_CASE("the sample engine does not allocate", "[realtime][sampler]") {
    SampleEngine engine(kRate);
    engine.setSample(Sample::A, dc(30000), {});
    engine.setSample(Sample::B, dc(30000), {});
    std::vector<float> left(kBlock), right(kBlock);

    long allocations = 0;
    {
        AllocGuard guard;
        for (int block = 0; block < 60; ++block) {
            SchedulerEvent events[8];
            int count = 0;
            if (block % 3 == 0) {
                // faster than the release: more triggers than the voice pool holds
                for (int i = 0; i < 6; ++i)
                    events[count++] = {SchedulerEvent::Kind::Trigger, i * 20, i % 2 ? Sample::B : Sample::A, 100};
            }
            if (block % 7 == 6) events[count++] = {SchedulerEvent::Kind::Stop, 400, Sample::A, 0};
            engine.process(left.data(), right.data(), kBlock, events, count);
        }
        allocations = guard.allocations();
    }
    CHECK(allocations == 0);
}

TEST_CASE("the whole chain from notes to samples does not allocate", "[realtime]") {
    RollScheduler scheduler(kRate);
    scheduler.setConfig(config(RollType::Rush, 90.0, PlaybackMode::Hold, Quantize::Off, VelocityFollow::VelocityIntensity));
    SampleEngine engine(kRate);
    engine.setSample(Sample::A, dc(3000), {});
    engine.setSample(Sample::B, dc(3000), {});
    std::vector<float> left(kBlock), right(kBlock);
    EventList out;

    long allocations = 0;
    float peak = 0.0f;
    {
        AllocGuard guard;
        for (int block = 0; block < 120; ++block) {
            NoteEvent note{0, true, 100};
            const int count = block == 0 || block == 70 ? 1 : 0;
            scheduler.process(kBlock, &note, count, Transport{}, out);
            engine.process(left.data(), right.data(), kBlock, out.data(), static_cast<int>(out.size()));
            for (float v : left) peak = std::max(peak, std::fabs(v));
        }
        allocations = guard.allocations();
    }
    CHECK(allocations == 0);
    CHECK(peak > 0.05f);
}

// ---- the allocation-free paths give the same results as the vector ones ----

TEST_CASE("planPhasesInto equals planPhases", "[realtime][equivalence]") {
    for (auto type : kTypes)
        for (double intensity : {0.0, 20.0, 40.0, 50.0, 80.0, 100.0})
            for (std::uint32_t seed : {0u, 5u, 77u}) {
                PhaseList into;
                planPhasesInto(type, intensity, seed, into);
                const auto expected = planPhases(type, intensity, seed);
                REQUIRE(into.size() == expected.size());
                for (size_t i = 0; i < expected.size(); ++i) {
                    CHECK(into[i].grid == expected[i].grid);
                    CHECK(into[i].share == expected[i].share);
                    CHECK(into[i].hold == expected[i].hold);
                }
            }
}

TEST_CASE("generateRollInto equals generateRoll", "[realtime][equivalence]") {
    for (auto type : kTypes)
        for (double intensity : {0.0, 30.0, 55.0, 100.0})
            for (double blend : {0.0, 25.0, 50.0, 80.0, 100.0})
                for (std::uint32_t seed : {0u, 9u}) {
                    const RollSpec spec{type, intensity, blend, seed};
                    TriggerList into;
                    generateRollInto(spec, kUnitsPerBar, into);
                    const auto expected = generateRoll(spec);
                    REQUIRE(into.size() == expected.size());
                    for (size_t i = 0; i < expected.size(); ++i) CHECK(into[i] == expected[i]);
                }
}

TEST_CASE("expressTriggersInto and applyWindowInto equal the vector versions", "[realtime][equivalence]") {
    ExpressionSpec spec;
    spec.curve = curveTemplate(CurveType::Wave);
    spec.humanize = 70.0;
    spec.swing = 45.0;
    spec.humanizeSeed = 123;
    for (auto type : kTypes)
        for (double window : {4.0, 2.0, 1.0, 0.5}) {
            const RollSpec rs{type, 75.0, 40.0, 3};
            TriggerList roll;
            generateRollInto(rs, kUnitsPerBar, roll);
            ExpressedList into;
            expressTriggersInto(roll, spec, kUnitsPerBar, into);
            const auto expected = expressTriggers(generateRoll(rs), spec);
            REQUIRE(into.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                CHECK(into[i].trigger == expected[i].trigger);
                CHECK(into[i].velocity == expected[i].velocity);
                CHECK(into[i].index == expected[i].index);
                CHECK(into[i].swingUnits == expected[i].swingUnits);
                CHECK(into[i].humanizeMs == expected[i].humanizeMs);
            }

            applyWindowInto(into, type, window);
            const auto windowed = applyWindow(expected, type, window);
            REQUIRE(into.size() == windowed.size());
            for (size_t i = 0; i < windowed.size(); ++i) {
                CHECK(into[i].trigger == windowed[i].trigger);
                CHECK(into[i].index == windowed[i].index);
            }

            applyWindowInto(roll, type, window);
            const auto plain = applyWindow(generateRoll(rs), type, window);
            REQUIRE(roll.size() == plain.size());
            for (size_t i = 0; i < plain.size(); ++i) CHECK(roll[i] == plain[i]);
        }
}

TEST_CASE("applying parameter values to a roll config equals applying them to the preset", "[realtime][equivalence]") {
    Preset preset = defaultPreset(5);
    preset.curve = curveTemplate(CurveType::Spike);
    preset.variationSeed = 12;
    const std::uint32_t pick[] = {0u, 1u, 2u, 3u, 4u, 5u};
    for (int roll = 0; roll <= 5; ++roll)
        for (int window = 0; window < 4; ++window)
            for (int mode = 0; mode < 2; ++mode)
                for (int quantize = 0; quantize < 5; ++quantize)
                    for (int follow = 0; follow < 3; ++follow) {
                        ParameterValues v;
                        v.rollType = roll;
                        v.window = window;
                        v.mode = mode;
                        v.quantize = quantize;
                        v.velocityFollow = follow;
                        v.intensity = 10.0 + pick[roll] * 17.0;
                        v.blend = 100.0 - pick[roll] * 13.0;
                        v.humanize = 3.0 * quantize;
                        v.swing = 7.0 * follow;

                        Preset edited = preset;
                        applyParameterValues(edited, v);
                        const RollConfig expected = toRollConfig(edited);
                        RollConfig actual = toRollConfig(preset);
                        applyParameterValues(actual, v);

                        CHECK(actual.enabled == expected.enabled);
                        CHECK(actual.spec.type == expected.spec.type);
                        CHECK(actual.spec.intensity == expected.spec.intensity);
                        CHECK(actual.spec.blend == expected.spec.blend);
                        CHECK(actual.spec.variationSeed == expected.spec.variationSeed);
                        CHECK(actual.expression.humanize == expected.expression.humanize);
                        CHECK(actual.expression.swing == expected.expression.swing);
                        CHECK(actual.expression.curve == expected.expression.curve);
                        CHECK(actual.playback.windowBars == expected.playback.windowBars);
                        CHECK(actual.playback.mode == expected.playback.mode);
                        CHECK(actual.playback.quantize == expected.playback.quantize);
                        CHECK(actual.playback.follow == expected.playback.follow);
                    }
}

TEST_CASE("applying parameter values to a roll config does not allocate", "[realtime]") {
    RollConfig c = toRollConfig(defaultPreset(1));
    ParameterValues v;
    v.rollType = 3;
    v.intensity = 66.0;
    long allocations = 0;
    {
        AllocGuard guard;
        for (int i = 0; i < 100; ++i) {
            v.window = i % 4;
            applyParameterValues(c, v);
        }
        allocations = guard.allocations();
    }
    CHECK(allocations == 0);
}
