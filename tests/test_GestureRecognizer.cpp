#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/GestureRecognizer.h"

using Catch::Approx;
using namespace dw;

namespace {

constexpr double kPi = 3.14159265358979323846;

Stroke line(double x0, double y0, double x1, double y1, int n = 40) {
    Stroke s;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        s.push_back({x0 + (x1 - x0) * t, y0 + (y1 - y0) * t});
    }
    return s;
}

// Straight pieces through the given corner points, `n` points per piece.
Stroke polyline(const std::vector<GesturePoint>& corners, int n = 30) {
    Stroke s;
    for (size_t i = 0; i + 1 < corners.size(); ++i) {
        const auto piece = line(corners[i].x, corners[i].y, corners[i + 1].x, corners[i + 1].y, n);
        s.insert(s.end(), piece.begin() + (i == 0 ? 0 : 1), piece.end());
    }
    return s;
}

Stroke sine(double periods, double amplitude, int n = 200) {
    Stroke s;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        s.push_back({t, 0.5 + amplitude * std::sin(2.0 * kPi * periods * t)});
    }
    return s;
}

Stroke circle(double cx, double cy, double radius, int n = 16) {
    Stroke s;
    for (int i = 0; i <= n; ++i) {
        const double a = 2.0 * kPi * i / n;
        s.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a)});
    }
    return s;
}

Stroke dot(double x, double y) { return {{x, y}, {x + 0.004, y + 0.002}}; }

// Deterministic hand jitter.
Stroke jittered(Stroke s, double amount) {
    for (size_t i = 0; i < s.size(); ++i) {
        s[i].x += amount * std::sin(1.7 * static_cast<double>(i) + 0.3);
        s[i].y += amount * std::cos(2.3 * static_cast<double>(i) + 1.1);
    }
    return s;
}

Gesture one(Stroke s) { return {std::move(s)}; }

}  // namespace

TEST_CASE("the decision follows the thresholds of the spec", "[gesture][decision]") {
    CHECK(decideGesture(0.95) == GestureDecision::Apply);
    CHECK(decideGesture(0.81) == GestureDecision::Apply);
    CHECK(decideGesture(0.80) == GestureDecision::ApplyWithAlternative);  // 60 to 80 %
    CHECK(decideGesture(0.70) == GestureDecision::ApplyWithAlternative);
    CHECK(decideGesture(0.60) == GestureDecision::ApplyWithAlternative);
    CHECK(decideGesture(0.5999) == GestureDecision::Reject);
    CHECK(decideGesture(0.0) == GestureDecision::Reject);
}

TEST_CASE("a rising line is BUILD", "[gesture][build]") {
    const auto r = recognizeGesture(one(line(0.1, 0.1, 0.9, 0.9)));
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Build);
    CHECK(r.best->confidence > 0.95);
    CHECK(r.decision == GestureDecision::Apply);
}

TEST_CASE("a falling line is DROP", "[gesture][drop]") {
    const auto r = recognizeGesture(one(line(0.1, 0.9, 0.9, 0.1)));
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Drop);
    CHECK(r.best->confidence > 0.95);
    CHECK(r.decision == GestureDecision::Apply);
}

TEST_CASE("the direction of drawing does not matter, the shape does", "[gesture][build][drop]") {
    const auto buildBackwards = recognizeGesture(one(line(0.9, 0.9, 0.1, 0.1)));  // `/` drawn from the top
    REQUIRE(buildBackwards.best.has_value());
    CHECK(buildBackwards.best->type == RollType::Build);
    const auto dropBackwards = recognizeGesture(one(line(0.9, 0.1, 0.1, 0.9)));  // `\` drawn from the bottom
    REQUIRE(dropBackwards.best.has_value());
    CHECK(dropBackwards.best->type == RollType::Drop);
}

TEST_CASE("a hand-drawn line with a small wobble is still BUILD or DROP", "[gesture][build][drop]") {
    const auto up = recognizeGesture(one(jittered(line(0.05, 0.1, 0.95, 0.9, 60), 0.015)));
    REQUIRE(up.best.has_value());
    CHECK(up.best->type == RollType::Build);
    CHECK(up.best->confidence > 0.8);
    const auto down = recognizeGesture(one(jittered(line(0.05, 0.9, 0.95, 0.1, 60), 0.015)));
    REQUIRE(down.best.has_value());
    CHECK(down.best->type == RollType::Drop);
    CHECK(down.best->confidence > 0.8);
}

TEST_CASE("size and position of the gesture do not matter", "[gesture]") {
    const auto big = recognizeGesture(one(line(0.0, 0.0, 1.0, 1.0)));
    const auto small = recognizeGesture(one(line(0.40, 0.40, 0.44, 0.44)));
    const auto moved = recognizeGesture(one(line(5.0, 7.0, 105.0, 107.0)));
    REQUIRE(big.best.has_value());
    REQUIRE(small.best.has_value());
    REQUIRE(moved.best.has_value());
    CHECK(small.best->type == RollType::Build);
    CHECK(moved.best->type == RollType::Build);
    CHECK(small.best->confidence == Approx(big.best->confidence).margin(1e-6));
    CHECK(moved.best->confidence == Approx(big.best->confidence).margin(1e-6));
}

TEST_CASE("flat then rising is RUSH", "[gesture][rush]") {
    const auto r = recognizeGesture(one(polyline({{0.0, 0.1}, {0.7, 0.1}, {1.0, 0.5}})));  // `_____/`
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Rush);
    CHECK(r.best->confidence > 0.9);
    CHECK(r.decision == GestureDecision::Apply);
}

TEST_CASE("a hand-drawn RUSH still reads as RUSH", "[gesture][rush]") {
    const auto r = recognizeGesture(one(jittered(polyline({{0.0, 0.1}, {0.7, 0.1}, {1.0, 0.5}}, 40), 0.01)));
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Rush);
    CHECK(r.best->confidence > 0.8);
}

TEST_CASE("a wave with several periods is WAVE", "[gesture][wave]") {
    const auto r = recognizeGesture(one(sine(3.0, 0.25)));  // `~~~~`
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Wave);
    CHECK(r.best->confidence > 0.95);
    CHECK(r.decision == GestureDecision::Apply);
}

TEST_CASE("up, down, up is WAVE", "[gesture][wave]") {
    const auto r = recognizeGesture(one(polyline({{0.0, 0.1}, {0.33, 0.9}, {0.66, 0.1}, {1.0, 0.9}})));  // `/\/`
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Wave);
    CHECK(r.best->confidence > 0.8);
    CHECK(r.decision == GestureDecision::Apply);
}

TEST_CASE("a wiggle of at most 10 percent does not make a wave", "[gesture][wave][build]") {
    // A rising line with a dip of 8 % of the gesture size: still BUILD.
    const auto r = recognizeGesture(one(polyline({{0.0, 0.0}, {0.5, 0.5}, {0.6, 0.42}, {1.0, 1.0}})));
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Build);
}

TEST_CASE("a clear reversal turns BUILD into something uncertain", "[gesture][wave][build]") {
    // `/\`: one big direction change. Neither BUILD/DROP (0 changes) nor WAVE (2 or more).
    const auto r = recognizeGesture(one(polyline({{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}})));
    CHECK(r.decision == GestureDecision::Reject);
}

TEST_CASE("several tiny strokes are BURST", "[gesture][burst]") {
    const Gesture dots = {dot(0.1, 0.5), dot(0.4, 0.5), dot(0.7, 0.5), dot(0.95, 0.5)};  // `. . . .`
    const auto r = recognizeGesture(dots);
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Burst);
    CHECK(r.best->confidence > 0.95);
    CHECK(r.decision == GestureDecision::Apply);
}

TEST_CASE("the dots of the spec, dot, star and small circle, are BURST", "[gesture][burst]") {
    const Gesture dots = {dot(0.1, 0.5),
                          {{0.45, 0.52}, {0.55, 0.48}, {0.5, 0.5}, {0.5, 0.54}, {0.5, 0.46}},  // `*`
                          circle(0.9, 0.5, 0.03)};                                         // `o`
    const auto r = recognizeGesture(dots);
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Burst);
    CHECK(r.best->confidence > 0.8);
}

TEST_CASE("two dots are not enough for BURST", "[gesture][burst]") {
    const auto r = recognizeGesture({dot(0.1, 0.5), dot(0.9, 0.5)});
    CHECK(r.decision == GestureDecision::Reject);
}

TEST_CASE("a click, a gesture smaller than 2 percent of the drawing area, is nothing", "[gesture][reject]") {
    CHECK(recognizeGesture(one(line(0.50, 0.50, 0.51, 0.51))).decision == GestureDecision::Reject);
    CHECK_FALSE(recognizeGesture(one(line(0.50, 0.50, 0.51, 0.51))).best.has_value());
    CHECK(recognizeGesture(one(line(0.50, 0.50, 0.53, 0.53))).decision == GestureDecision::Apply);  // 3 % is a gesture
}

TEST_CASE("a single dot is nothing", "[gesture][burst]") {
    const auto r = recognizeGesture({dot(0.5, 0.5)});
    CHECK(r.decision == GestureDecision::Reject);
    CHECK_FALSE(r.best.has_value());
}

TEST_CASE("a big stroke among dots is not BURST", "[gesture][burst]") {
    const Gesture mixed = {dot(0.1, 0.1), line(0.2, 0.2, 0.9, 0.9), dot(0.5, 0.9)};
    const auto r = recognizeGesture(mixed);
    // never applied as BURST
    CHECK_FALSE((r.best && r.best->type == RollType::Burst && r.decision != GestureDecision::Reject));
}

TEST_CASE("a flat line, a big circle and nothing at all are rejected", "[gesture][reject]") {
    for (const Gesture& g : {one(line(0.0, 0.5, 1.0, 0.5)), one(line(0.0, 0.5, 1.0, 0.52)), one(circle(0.5, 0.5, 0.4, 40)),
                             Gesture{}, Gesture{Stroke{}}}) {
        const auto r = recognizeGesture(g);
        CHECK(r.decision == GestureDecision::Reject);
        if (r.best) CHECK(r.best->confidence < 0.6);
    }
}

TEST_CASE("a bent ramp is applied with an alternative", "[gesture][alternative]") {
    // Rises, but bends: BUILD with about two thirds confidence, RUSH as the second guess.
    const auto r = recognizeGesture(one(polyline({{0.0, 0.0}, {0.5, 0.3}, {1.0, 1.0}})));
    REQUIRE(r.best.has_value());
    CHECK(r.best->type == RollType::Build);
    CHECK(r.best->confidence >= 0.6);
    CHECK(r.best->confidence <= 0.8);
    CHECK(r.decision == GestureDecision::ApplyWithAlternative);
    REQUIRE(r.alternative.has_value());
    CHECK(r.alternative->type == RollType::Rush);
    CHECK(r.alternative->confidence >= kMinAlternative);
    CHECK(r.alternative->confidence < r.best->confidence);
}

TEST_CASE("a clear gesture has no alternative", "[gesture][alternative]") {
    const auto r = recognizeGesture(one(line(0.0, 0.0, 1.0, 1.0)));
    CHECK_FALSE(r.alternative.has_value());
}

TEST_CASE("recognition is deterministic", "[gesture]") {
    const Gesture g = one(jittered(sine(2.0, 0.2), 0.01));
    const auto a = recognizeGesture(g);
    const auto b = recognizeGesture(g);
    REQUIRE(a.best.has_value());
    REQUIRE(b.best.has_value());
    CHECK(a.best->type == b.best->type);
    CHECK(a.best->confidence == b.best->confidence);
    CHECK(a.decision == b.decision);
}

// ---- the zigzag that counts direction changes ----

TEST_CASE("a monotone series has one swing", "[gesture][zigzag]") {
    const auto swings = zigzagSwings({0.0, 0.2, 0.5, 0.9, 1.0}, 0.1);
    REQUIRE(swings.size() == 1);
    CHECK(swings[0] == Approx(1.0));
}

TEST_CASE("up, down, up has three swings", "[gesture][zigzag]") {
    const auto swings = zigzagSwings({0.0, 1.0, 0.0, 1.0}, 0.1);
    REQUIRE(swings.size() == 3);
    for (double s : swings) CHECK(s == Approx(1.0));
}

TEST_CASE("a swing of exactly the threshold does not count, a larger one does", "[gesture][zigzag]") {
    // up 0.5, down 0.1 (exactly the threshold), up to 1.0: the dip is noise
    CHECK(zigzagSwings({0.0, 0.5, 0.4, 1.0}, 0.1).size() == 1);
    // the dip is 0.11: it counts
    CHECK(zigzagSwings({0.0, 0.5, 0.39, 1.0}, 0.1).size() == 3);
}

TEST_CASE("the zigzag handles degenerate input", "[gesture][zigzag]") {
    CHECK(zigzagSwings({}, 0.1).empty());
    CHECK(zigzagSwings({0.5}, 0.1).empty());
    CHECK(zigzagSwings({0.5, 0.5, 0.5}, 0.1).empty());
    CHECK(zigzagSwings({0.0, 0.05}, 0.1).empty());
}

// ---- normalization and the stored form ----

TEST_CASE("normalizing keeps the proportions and puts the longer side to 1", "[gesture][normalize]") {
    const auto n = normalizeGesture(one(line(10.0, 20.0, 14.0, 22.0, 5)));  // 4 wide, 2 high
    REQUIRE(n.size() == 1);
    REQUIRE(n[0].size() == 5);
    CHECK(n[0].front().x == Approx(0.0));
    CHECK(n[0].front().y == Approx(0.0));
    CHECK(n[0].back().x == Approx(1.0));
    CHECK(n[0].back().y == Approx(0.5));
}

TEST_CASE("normalizing a single point or nothing does not break", "[gesture][normalize]") {
    CHECK(normalizeGesture({}).empty());
    const auto n = normalizeGesture(one({{3.0, 4.0}}));
    REQUIRE(n.size() == 1);
    CHECK(n[0][0].x == Approx(0.0));
    CHECK(n[0][0].y == Approx(0.0));
}

TEST_CASE("the stored form joins the strokes and stays in the schema limits", "[gesture][flatten]") {
    const auto flat = flattenForPreset({line(0.0, 0.0, 0.5, 0.25, 10), line(0.6, 0.0, 1.0, 0.5, 10)});
    CHECK(flat.size() == 20);
    for (const auto& p : flat) {
        CHECK(p.x >= 0.0);
        CHECK(p.x <= 1.0);
        CHECK(p.y >= 0.0);
        CHECK(p.y <= 1.0);
    }
}

TEST_CASE("the stored form is thinned out above 512 points and keeps both ends", "[gesture][flatten]") {
    const auto flat = flattenForPreset(one(line(0.0, 0.0, 1.0, 1.0, 2000)));
    REQUIRE(flat.size() == 512);
    CHECK(flat.front().x == Approx(0.0));
    CHECK(flat.back().x == Approx(1.0));
    CHECK(flat.back().y == Approx(1.0));
}

TEST_CASE("fewer than two points are not stored", "[gesture][flatten]") {
    CHECK(flattenForPreset({}).empty());
    CHECK(flattenForPreset(one({{0.5, 0.5}})).empty());
    CHECK(flattenForPreset({Stroke{}, Stroke{}}).empty());
}
