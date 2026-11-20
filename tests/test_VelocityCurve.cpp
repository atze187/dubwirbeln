#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/VelocityCurve.h"

using Catch::Approx;
using namespace dw;

namespace {
const CurveType kAllTypes[] = {CurveType::Flat,     CurveType::Crescendo,     CurveType::Decrescendo,
                               CurveType::Spike,    CurveType::InvertedSpike, CurveType::Wave,
                               CurveType::Ghost};

double at(CurveType type, double t) { return evaluateCurve(curveTemplate(type), t); }
}  // namespace

TEST_CASE("every template satisfies the preset schema limits", "[curve]") {
    for (auto type : kAllTypes) {
        const auto pts = curveTemplate(type);
        REQUIRE(pts.size() >= 2);
        REQUIRE(pts.size() <= 16);
        CHECK(pts.front().t == 0.0);
        CHECK(pts.back().t == 1.0);
        for (size_t i = 0; i < pts.size(); ++i) {
            CHECK(pts[i].v >= 1.0);
            CHECK(pts[i].v <= 127.0);
            if (i > 0) CHECK(pts[i].t > pts[i - 1].t);
        }
    }
}

TEST_CASE("flat is 100 everywhere", "[curve]") {
    CHECK(at(CurveType::Flat, 0.0) == Approx(100.0));
    CHECK(at(CurveType::Flat, 0.37) == Approx(100.0));
    CHECK(at(CurveType::Flat, 1.0) == Approx(100.0));
}

TEST_CASE("crescendo rises from 40 to 127", "[curve]") {
    CHECK(at(CurveType::Crescendo, 0.0) == Approx(40.0));
    CHECK(at(CurveType::Crescendo, 0.5) == Approx(83.5));
    CHECK(at(CurveType::Crescendo, 1.0) == Approx(127.0));
}

TEST_CASE("decrescendo falls from 127 to 40", "[curve]") {
    CHECK(at(CurveType::Decrescendo, 0.0) == Approx(127.0));
    CHECK(at(CurveType::Decrescendo, 0.5) == Approx(83.5));
    CHECK(at(CurveType::Decrescendo, 1.0) == Approx(40.0));
}

TEST_CASE("spike peaks at 127 in the middle", "[curve]") {
    CHECK(at(CurveType::Spike, 0.0) == Approx(70.0));
    CHECK(at(CurveType::Spike, 0.25) == Approx(98.5));
    CHECK(at(CurveType::Spike, 0.5) == Approx(127.0));
    CHECK(at(CurveType::Spike, 1.0) == Approx(70.0));
}

TEST_CASE("inverted spike dips to 40 in the middle", "[curve]") {
    CHECK(at(CurveType::InvertedSpike, 0.0) == Approx(110.0));
    CHECK(at(CurveType::InvertedSpike, 0.25) == Approx(75.0));
    CHECK(at(CurveType::InvertedSpike, 0.5) == Approx(40.0));
    CHECK(at(CurveType::InvertedSpike, 1.0) == Approx(110.0));
}

TEST_CASE("wave has two periods between 60 and 120", "[curve]") {
    CHECK(at(CurveType::Wave, 0.0) == Approx(60.0));
    CHECK(at(CurveType::Wave, 0.125) == Approx(90.0));
    CHECK(at(CurveType::Wave, 0.25) == Approx(120.0));
    CHECK(at(CurveType::Wave, 0.5) == Approx(60.0));
    CHECK(at(CurveType::Wave, 0.75) == Approx(120.0));
    CHECK(at(CurveType::Wave, 1.0) == Approx(60.0));
}

TEST_CASE("ghost is quiet with accents on every bar start", "[curve]") {
    for (double t : {0.0, 0.25, 0.5, 0.75}) CHECK(at(CurveType::Ghost, t) == Approx(100.0));
    for (double t : {0.1, 0.4, 0.6, 0.9, 1.0}) CHECK(at(CurveType::Ghost, t) == Approx(35.0));
}

TEST_CASE("evaluateCurve interpolates linearly and clamps outside the points", "[curve]") {
    const std::vector<CurvePoint> pts{{0.2, 50.0}, {0.6, 90.0}};
    CHECK(evaluateCurve(pts, 0.0) == Approx(50.0));
    CHECK(evaluateCurve(pts, 0.4) == Approx(70.0));
    CHECK(evaluateCurve(pts, 1.0) == Approx(90.0));
}

TEST_CASE("evaluateCurve handles degenerate curves", "[curve]") {
    CHECK(evaluateCurve({}, 0.5) == Approx(0.0));
    CHECK(evaluateCurve({{0.5, 77.0}}, 0.1) == Approx(77.0));
    CHECK(evaluateCurve({{0.5, 77.0}}, 0.9) == Approx(77.0));
}
