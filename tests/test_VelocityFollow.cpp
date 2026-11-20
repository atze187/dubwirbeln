#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/VelocityFollow.h"

using Catch::Approx;
using namespace dw;

TEST_CASE("Follow OFF changes neither intensity nor velocity", "[follow]") {
    CHECK(effectiveIntensity(37.0, VelocityFollow::Off, 90) == Approx(37.0));
    CHECK(followVelocity(100, VelocityFollow::Off, 20) == 100);
}

TEST_CASE("Follow VELOCITY keeps the stored intensity", "[follow]") {
    CHECK(effectiveIntensity(37.0, VelocityFollow::Velocity, 90) == Approx(37.0));
}

TEST_CASE("Follow VELOCITY scales trigger velocities by played / 127", "[follow]") {
    CHECK(followVelocity(100, VelocityFollow::Velocity, 127) == 100);
    CHECK(followVelocity(100, VelocityFollow::Velocity, 64) == 50);  // 50.39
    CHECK(followVelocity(127, VelocityFollow::Velocity, 64) == 64);
    CHECK(followVelocity(50, VelocityFollow::Velocity, 127) == 50);
}

TEST_CASE("scaled velocity never leaves 1..127", "[follow]") {
    CHECK(followVelocity(1, VelocityFollow::Velocity, 1) == 1);
    CHECK(followVelocity(100, VelocityFollow::Velocity, 1) == 1);  // 0.79
    CHECK(followVelocity(127, VelocityFollow::Velocity, 127) == 127);
}

TEST_CASE("Follow VELOCITY + INTENSITY maps 1..127 linearly to intensity 0..100", "[follow]") {
    CHECK(effectiveIntensity(37.0, VelocityFollow::VelocityIntensity, 1) == Approx(0.0));
    CHECK(effectiveIntensity(37.0, VelocityFollow::VelocityIntensity, 127) == Approx(100.0));
    CHECK(effectiveIntensity(37.0, VelocityFollow::VelocityIntensity, 64) == Approx(50.0));
    CHECK(effectiveIntensity(37.0, VelocityFollow::VelocityIntensity, 32) == Approx(31.0 / 126.0 * 100.0));
}

TEST_CASE("Follow VELOCITY + INTENSITY also scales trigger velocities", "[follow]") {
    CHECK(followVelocity(100, VelocityFollow::VelocityIntensity, 64) == 50);
    CHECK(followVelocity(100, VelocityFollow::VelocityIntensity, 127) == 100);
}

TEST_CASE("played velocity outside 1..127 is clamped", "[follow]") {
    CHECK(effectiveIntensity(0.0, VelocityFollow::VelocityIntensity, 0) == Approx(0.0));
    CHECK(effectiveIntensity(0.0, VelocityFollow::VelocityIntensity, 200) == Approx(100.0));
}
