#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Expression.h"

using Catch::Approx;
using namespace dw;

namespace {

std::vector<Trigger> buildRoll() { return generateRoll({RollType::Build, 100.0, 50.0}); }

ExpressionSpec flat(double velocity) {
    ExpressionSpec spec;
    spec.curve = {{0.0, velocity}, {1.0, velocity}};
    return spec;
}

std::vector<Trigger> stepsOf(int grid, int count) {
    std::vector<Trigger> out;
    for (int i = 0; i < count; ++i) out.push_back({i * (64 / grid), grid, Sample::A});
    return out;
}

}  // namespace

TEST_CASE("expressTriggers keeps the roll and numbers the triggers", "[expression]") {
    const auto roll = buildRoll();
    const auto out = expressTriggers(roll, flat(100.0));
    REQUIRE(out.size() == roll.size());
    for (size_t i = 0; i < roll.size(); ++i) {
        CHECK(out[i].trigger == roll[i]);
        CHECK(out[i].index == static_cast<int>(i));
    }
}

TEST_CASE("without humanize and swing the velocity follows the curve exactly", "[expression]") {
    const auto roll = buildRoll();
    const auto out = expressTriggers(roll, flat(100.0));
    REQUIRE(out.size() == roll.size());
    for (const auto& e : out) {
        CHECK(e.velocity == 100);
        CHECK(e.swingUnits == 0.0);
        CHECK(e.humanizeMs == 0.0);
    }
}

TEST_CASE("velocity is the curve at position over the reference, rounded half up", "[expression]") {
    ExpressionSpec spec;
    spec.curve = curveTemplate(CurveType::Crescendo);
    const auto roll = buildRoll();
    const auto out = expressTriggers(roll, spec);
    REQUIRE(out.size() == roll.size());
    for (const auto& e : out) {
        const double v = 40.0 + 87.0 * e.trigger.position / 256.0;
        CHECK(e.velocity == static_cast<int>(std::floor(v + 0.5)));
    }
    CHECK(out.front().velocity == 40);
}

TEST_CASE("swing delays every second step of the grid", "[expression][swing]") {
    auto spec = flat(100.0);
    spec.swing = 100.0;
    const auto out = expressTriggers(stepsOf(16, 4), spec);  // positions 0, 4, 8, 12
    REQUIRE(out.size() == 4);
    CHECK(out[0].swingUnits == Approx(0.0));
    CHECK(out[1].swingUnits == Approx(4.0 / 3.0));
    CHECK(out[2].swingUnits == Approx(0.0));
    CHECK(out[3].swingUnits == Approx(4.0 / 3.0));
}

TEST_CASE("swing scales with the percent value and the grid step", "[expression][swing]") {
    auto spec = flat(100.0);
    spec.swing = 50.0;
    const auto eighths = expressTriggers(stepsOf(8, 4), spec);  // step 8 units
    REQUIRE(eighths.size() == 4);
    CHECK(eighths[1].swingUnits == Approx(4.0 / 3.0));
    const auto thirtySeconds = expressTriggers(stepsOf(32, 4), spec);  // step 2 units
    REQUIRE(thirtySeconds.size() == 4);
    CHECK(thirtySeconds[0].swingUnits == Approx(0.0));
    CHECK(thirtySeconds[1].swingUnits == Approx(1.0 / 3.0));
    CHECK(thirtySeconds[3].swingUnits == Approx(1.0 / 3.0));
}

TEST_CASE("swing 0 shifts nothing", "[expression][swing]") {
    const auto out = expressTriggers(stepsOf(16, 4), flat(100.0));
    REQUIRE(out.size() == 4);
    for (const auto& e : out) CHECK(e.swingUnits == 0.0);
}

TEST_CASE("humanize matches reference values from an independent PCG32", "[expression][humanize]") {
    // Reference: Rng(seed, stream = trigger index); timing = 10 * (2a - 1) ms, velocity = 12 * (2b - 1)
    // with a, b the first two doubles of that generator, scaled by humanize / 100.
    auto spec = flat(100.0);
    spec.humanize = 100.0;
    spec.humanizeSeed = 1234;
    const auto out = expressTriggers(buildRoll(), spec);
    REQUIRE(out.size() > 8);
    CHECK(out[0].humanizeMs == Approx(-2.8980791848152876));
    CHECK(out[0].velocity == 112);
    CHECK(out[5].humanizeMs == Approx(-5.221307184547186));
    CHECK(out[5].velocity == 102);

    spec.humanizeSeed = 99;
    const auto other = expressTriggers(buildRoll(), spec);
    REQUIRE(other.size() > 8);
    CHECK(other[7].humanizeMs == Approx(2.315661055035889));
    CHECK(other[7].velocity == 109);
}

TEST_CASE("humanize scales linearly with the percent value", "[expression][humanize]") {
    auto spec = flat(100.0);
    spec.humanizeSeed = 1234;
    spec.humanize = 50.0;
    const auto out = expressTriggers(buildRoll(), spec);
    REQUIRE(out.size() > 5);
    CHECK(out[5].humanizeMs == Approx(-5.221307184547186 / 2.0));
    CHECK(out[5].velocity == 101);  // 100 + 0.78
}

TEST_CASE("humanize stays inside its range and the velocity inside 1..127", "[expression][humanize]") {
    auto spec = flat(100.0);
    spec.humanize = 100.0;
    spec.humanizeSeed = 4242;
    const auto out = expressTriggers(buildRoll(), spec);
    REQUIRE_FALSE(out.empty());
    for (const auto& e : out) {
        CHECK(std::abs(e.humanizeMs) <= kHumanizeMaxMs);
        CHECK(e.velocity >= 88);
        CHECK(e.velocity <= 112);
    }
    for (double v : {1.0, 127.0}) {
        auto edge = flat(v);
        edge.humanize = 100.0;
        edge.humanizeSeed = 7;
        const auto o = expressTriggers(buildRoll(), edge);
        REQUIRE_FALSE(o.empty());
        for (const auto& e : o) {
            CHECK(e.velocity >= 1);
            CHECK(e.velocity <= 127);
        }
    }
}

TEST_CASE("humanize 0 ignores the seed", "[expression][humanize]") {
    auto a = flat(100.0);
    auto b = flat(100.0);
    a.humanizeSeed = 1;
    b.humanizeSeed = 2;
    CHECK(expressTriggers(buildRoll(), a).size() == buildRoll().size());
    const auto oa = expressTriggers(buildRoll(), a);
    const auto ob = expressTriggers(buildRoll(), b);
    REQUIRE(oa.size() == ob.size());
    for (size_t i = 0; i < oa.size(); ++i) {
        CHECK(oa[i].humanizeMs == 0.0);
        CHECK(oa[i].velocity == ob[i].velocity);
    }
}

TEST_CASE("the same seed renders identically, another seed differs", "[expression][humanize]") {
    auto spec = flat(100.0);
    spec.humanize = 80.0;
    spec.humanizeSeed = 11;
    const auto a = expressTriggers(buildRoll(), spec);
    const auto b = expressTriggers(buildRoll(), spec);
    spec.humanizeSeed = 12;
    const auto c = expressTriggers(buildRoll(), spec);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == c.size());
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].humanizeMs == b[i].humanizeMs);
        CHECK(a[i].velocity == b[i].velocity);
        if (a[i].humanizeMs != c[i].humanizeMs) differs = true;
    }
    CHECK(differs);
}

TEST_CASE("window trimming keeps humanize, velocity and swing of the remaining triggers", "[expression][window]") {
    ExpressionSpec spec;
    spec.curve = curveTemplate(CurveType::Wave);
    spec.humanize = 100.0;
    spec.swing = 60.0;
    spec.humanizeSeed = 31;
    const auto full = expressTriggers(buildRoll(), spec);
    REQUIRE_FALSE(full.empty());

    const auto windowed = applyWindow(full, RollType::Build, 1.0);
    REQUIRE_FALSE(windowed.empty());
    REQUIRE(windowed.size() < full.size());
    for (const auto& w : windowed) {
        REQUIRE(w.index >= 0);
        REQUIRE(w.index < static_cast<int>(full.size()));
        const auto& f = full[static_cast<size_t>(w.index)];
        CHECK(w.velocity == f.velocity);
        CHECK(w.humanizeMs == f.humanizeMs);
        CHECK(w.swingUnits == f.swingUnits);
        CHECK(w.trigger.position == f.trigger.position - 192);
    }
}

TEST_CASE("windowing expressed triggers selects the same triggers as windowing plain ones", "[expression][window]") {
    const auto roll = generateRoll({RollType::Drop, 80.0, 50.0});
    const auto expressed = expressTriggers(roll, flat(100.0));
    const auto plain = applyWindow(roll, RollType::Drop, 2.0);
    const auto windowed = applyWindow(expressed, RollType::Drop, 2.0);
    REQUIRE_FALSE(plain.empty());
    REQUIRE(windowed.size() == plain.size());
    for (size_t i = 0; i < plain.size(); ++i) CHECK(windowed[i].trigger == plain[i]);
}
