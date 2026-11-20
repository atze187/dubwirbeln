#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/SampleEngine.h"

using Catch::Approx;
using namespace dw;

// Setup: 48 kHz output -> attack 240 samples (5 ms), release 480 samples (10 ms).
namespace {

constexpr double kRate = 48000.0;

SamplePtr dc(int frames, float value, double rate = kRate) {
    auto d = std::make_shared<SampleData>();
    d->left.assign(static_cast<size_t>(frames), value);
    d->sampleRate = rate;
    return d;
}

// data[i] = i / frames: linear, so linear interpolation is exact.
SamplePtr ramp(int frames, double rate = kRate) {
    auto d = std::make_shared<SampleData>();
    for (int i = 0; i < frames; ++i) d->left.push_back(static_cast<float>(i) / static_cast<float>(frames));
    d->sampleRate = rate;
    return d;
}

SchedulerEvent trig(int offset, Sample slot = Sample::A, int velocity = 127) {
    return {SchedulerEvent::Kind::Trigger, offset, slot, velocity};
}
SchedulerEvent stop(int offset) { return {SchedulerEvent::Kind::Stop, offset, Sample::A, 0}; }

struct Out {
    std::vector<float> l, r;
};

Out render(SampleEngine& e, int n, const std::vector<SchedulerEvent>& events) {
    Out o{std::vector<float>(static_cast<size_t>(n), 9.0f), std::vector<float>(static_cast<size_t>(n), 9.0f)};
    e.process(o.l.data(), o.r.data(), n, events);
    return o;
}

double atk(int i) { return std::pow(10.0, -3.0 * (1.0 - i / 240.0)); }
double rel(int j) { return std::pow(10.0, -3.0 * j / 480.0); }

SampleEngine withA(SamplePtr data, SampleParams p = {}) {
    SampleEngine e(kRate);
    e.setSample(Sample::A, std::move(data), p);
    return e;
}

}  // namespace

TEST_CASE("without a sample the engine is silent", "[sampler]") {
    SampleEngine e(kRate);
    const auto o = render(e, 256, {trig(0)});
    for (size_t i = 0; i < o.l.size(); ++i) {
        CHECK(o.l[i] == 0.0f);
        CHECK(o.r[i] == 0.0f);
    }
    CHECK(e.activeVoices() == 0);
}

TEST_CASE("the attack is a 5 ms ramp, linear in dB from -60 dB to 0 dB", "[sampler][envelope]") {
    auto e = withA(dc(1000, 1.0f));
    const auto o = render(e, 1200, {trig(0)});
    CHECK(o.l[0] == Approx(atk(0)).margin(1e-6));
    CHECK(o.l[100] == Approx(atk(100)).margin(1e-6));
    CHECK(o.l[239] == Approx(atk(239)).margin(1e-6));
    CHECK(o.l[240] == Approx(1.0).margin(1e-6));
    CHECK(o.l[999] == Approx(1.0).margin(1e-6));
    CHECK(o.l[1000] == 0.0f);  // sample end
    CHECK(o.r[500] == Approx(1.0).margin(1e-6));
    CHECK(e.activeVoices() == 0);
}

TEST_CASE("a trigger starts at its sample offset", "[sampler]") {
    auto e = withA(dc(1000, 1.0f));
    const auto o = render(e, 600, {trig(100)});
    CHECK(o.l[99] == 0.0f);
    CHECK(o.l[100] == Approx(atk(0)).margin(1e-6));
    CHECK(o.l[100 + 240] == Approx(1.0).margin(1e-6));
}

TEST_CASE("velocity scales linearly with velocity / 127", "[sampler][gain]") {
    auto e = withA(dc(2000, 1.0f));
    const auto o = render(e, 600, {trig(0, Sample::A, 64)});
    CHECK(o.l[300] == Approx(64.0 / 127.0).margin(1e-6));
    auto loud = withA(dc(2000, 1.0f));
    CHECK(render(loud, 600, {trig(0, Sample::A, 127)}).l[300] == Approx(1.0).margin(1e-6));
}

TEST_CASE("velocity outside 1..127 is clamped", "[sampler][gain]") {
    auto e = withA(dc(2000, 1.0f));
    CHECK(render(e, 600, {trig(0, Sample::A, 500)}).l[300] == Approx(1.0).margin(1e-6));
    auto quiet = withA(dc(2000, 1.0f));
    CHECK(render(quiet, 600, {trig(0, Sample::A, 0)}).l[300] == Approx(1.0 / 127.0).margin(1e-6));
}

TEST_CASE("level is applied in dB", "[sampler][gain]") {
    SampleParams p;
    p.levelDb = -20.0;
    auto e = withA(dc(2000, 1.0f), p);
    CHECK(render(e, 600, {trig(0)}).l[300] == Approx(0.1).margin(1e-6));
}

TEST_CASE("start and end cut the sample", "[sampler][params]") {
    SampleParams p;
    p.start = 0.5;
    p.end = 0.75;
    auto e = withA(ramp(1000), p);  // plays frames 500..749
    const auto o = render(e, 600, {trig(0)});
    CHECK(o.l[245] == Approx(0.745).margin(1e-6));  // frame 500 + 245, past the attack
    CHECK(o.l[249] == Approx(0.749).margin(1e-6));
    CHECK(o.l[250] == 0.0f);
}

TEST_CASE("an empty range (end <= start) is silent", "[sampler][params]") {
    SampleParams p;
    p.start = 0.6;
    p.end = 0.6;
    auto e = withA(dc(1000, 1.0f), p);
    const auto o = render(e, 400, {trig(0)});
    for (float v : o.l) CHECK(v == 0.0f);
    CHECK(e.activeVoices() == 0);
}

TEST_CASE("tune changes the playback speed", "[sampler][params]") {
    SampleParams up;
    up.tuneSemitones = 12.0;
    auto e = withA(ramp(1000), up);
    const auto o = render(e, 700, {trig(0)});
    CHECK(o.l[300] == Approx(0.6).margin(1e-5));  // twice as fast: frame 600
    CHECK(o.l[499] == Approx(0.998).margin(1e-5));
    CHECK(o.l[500] == 0.0f);

    SampleParams down;
    down.tuneSemitones = -12.0;
    auto slow = withA(ramp(1000), down);
    const auto s = render(slow, 600, {trig(0)});
    CHECK(s.l[300] == Approx(0.15).margin(1e-5));  // half speed: frame 150
}

TEST_CASE("a different sample rate is converted", "[sampler][params]") {
    auto e = withA(ramp(1000, 24000.0));  // half the output rate: plays at half speed
    CHECK(render(e, 600, {trig(0)}).l[300] == Approx(0.15).margin(1e-5));
}

TEST_CASE("stereo samples keep their channels, mono feeds both", "[sampler]") {
    auto stereo = std::make_shared<SampleData>();
    stereo->left.assign(1000, 1.0f);
    stereo->right.assign(1000, 0.5f);
    stereo->sampleRate = kRate;
    SampleEngine e(kRate);
    e.setSample(Sample::A, stereo, {});
    const auto o = render(e, 600, {trig(0)});
    CHECK(o.l[400] == Approx(1.0).margin(1e-6));
    CHECK(o.r[400] == Approx(0.5).margin(1e-6));

    auto mono = withA(dc(1000, 0.7f));
    const auto m = render(mono, 600, {trig(0)});
    CHECK(m.l[400] == Approx(0.7).margin(1e-6));
    CHECK(m.r[400] == Approx(0.7).margin(1e-6));
}

TEST_CASE("sample B plays its own sample", "[sampler]") {
    SampleEngine e(kRate);
    e.setSample(Sample::A, dc(4000, 1.0f), {});
    e.setSample(Sample::B, dc(4000, 0.25f), {});
    CHECK(render(e, 600, {trig(0, Sample::B)}).l[400] == Approx(0.25).margin(1e-6));
}

TEST_CASE("a new trigger releases the running voice over 10 ms and starts a new one", "[sampler][retrigger]") {
    auto e = withA(dc(48000, 1.0f));
    const auto o = render(e, 3000, {trig(0), trig(1000)});
    CHECK(o.l[999] == Approx(1.0).margin(1e-6));
    CHECK(o.l[1000] == Approx(rel(0) + atk(0)).margin(1e-5));
    CHECK(o.l[1000 + 100] == Approx(rel(100) + atk(100)).margin(1e-5));
    CHECK(o.l[1000 + 240] == Approx(rel(240) + 1.0).margin(1e-5));
    CHECK(o.l[1000 + 479] == Approx(rel(479) + 1.0).margin(1e-5));
    CHECK(o.l[1000 + 480] == Approx(1.0).margin(1e-6));  // the old voice is gone
    CHECK(e.activeVoices() == 1);
}

TEST_CASE("a trigger for a slot without a sample leaves the running voice alone", "[sampler][retrigger]") {
    auto e = withA(dc(48000, 1.0f));
    const auto o = render(e, 3000, {trig(0), trig(1000, Sample::B)});
    CHECK(o.l[1000] == Approx(1.0).margin(1e-6));
    CHECK(o.l[2500] == Approx(1.0).margin(1e-6));
    CHECK(e.activeVoices() == 1);
}

TEST_CASE("stop releases the voice over 10 ms and then it is silent", "[sampler][stop]") {
    auto e = withA(dc(48000, 1.0f));
    const auto o = render(e, 3000, {trig(0), stop(2000)});
    CHECK(o.l[1999] == Approx(1.0).margin(1e-6));
    CHECK(o.l[2000] == Approx(rel(0)).margin(1e-6));
    CHECK(o.l[2000 + 240] == Approx(rel(240)).margin(1e-6));
    CHECK(o.l[2000 + 479] == Approx(rel(479)).margin(1e-6));
    CHECK(o.l[2000 + 480] == 0.0f);
    CHECK(e.activeVoices() == 0);
}

TEST_CASE("stop during the attack releases from the current level", "[sampler][stop]") {
    auto e = withA(dc(48000, 1.0f));
    const auto o = render(e, 1000, {trig(0), stop(120)});
    const double start = atk(120);
    CHECK(o.l[119] == Approx(atk(119)).margin(1e-6));
    CHECK(o.l[120] == Approx(start * rel(0)).margin(1e-6));
    CHECK(o.l[120 + 100] == Approx(start * rel(100)).margin(1e-6));
    CHECK(o.l[120 + 480] == 0.0f);
}

TEST_CASE("the output does not depend on the block size", "[sampler][blocks]") {
    const std::vector<SchedulerEvent> events = {trig(100), trig(900, Sample::B, 80), trig(1500, Sample::A, 40), stop(2500)};
    auto make = [] {
        SampleEngine e(kRate);
        e.setSample(Sample::A, ramp(20000), {});
        e.setSample(Sample::B, dc(20000, 0.5f), {});
        return e;
    };
    auto whole = make();
    const auto expected = render(whole, 4000, events);

    auto chunked = make();
    std::vector<float> l, r;
    for (int from = 0; from < 4000; from += 37) {
        const int n = std::min(37, 4000 - from);
        std::vector<SchedulerEvent> local;
        for (auto ev : events)
            if (ev.sampleOffset >= from && ev.sampleOffset < from + n) {
                ev.sampleOffset -= from;
                local.push_back(ev);
            }
        const auto part = render(chunked, n, local);
        l.insert(l.end(), part.l.begin(), part.l.end());
        r.insert(r.end(), part.r.begin(), part.r.end());
    }
    REQUIRE(l.size() == expected.l.size());
    for (size_t i = 0; i < l.size(); ++i) {
        INFO("sample " << i);
        CHECK(l[i] == expected.l[i]);
        CHECK(r[i] == expected.r[i]);
    }
}

TEST_CASE("very fast triggers never exceed the voice pool", "[sampler][retrigger]") {
    auto e = withA(dc(48000, 1.0f));
    std::vector<SchedulerEvent> events;
    for (int i = 0; i < 20; ++i) events.push_back(trig(i * 50));
    render(e, 2000, events);
    CHECK(e.activeVoices() >= 1);
    CHECK(e.activeVoices() <= 4);
    const auto o = render(e, 4000, {});
    for (float v : o.l) CHECK(std::isfinite(v));
    CHECK(e.activeVoices() == 1);
}

TEST_CASE("reset silences everything", "[sampler]") {
    auto e = withA(dc(48000, 1.0f));
    render(e, 500, {trig(0)});
    REQUIRE(e.activeVoices() == 1);
    e.reset();
    CHECK(e.activeVoices() == 0);
    const auto o = render(e, 500, {});
    for (float v : o.l) CHECK(v == 0.0f);
}

TEST_CASE("changing the parameters affects the next trigger only", "[sampler][params]") {
    auto e = withA(dc(48000, 1.0f));
    render(e, 400, {trig(0)});
    SampleParams quiet;
    quiet.levelDb = -20.0;
    e.setParams(Sample::A, quiet);
    const auto o = render(e, 600, {});
    CHECK(o.l[100] == Approx(1.0).margin(1e-6));  // the running voice keeps its gain
    const auto n = render(e, 600, {trig(0)});
    CHECK(n.l[500] == Approx(0.1).margin(1e-6));  // the old voice has faded out by then
}
