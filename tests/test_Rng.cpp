#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdint>
#include <set>

#include "engine/Rng.h"

// Reference values were computed with an independent Python implementation of
// PCG32 (XSH-RR), the first row being the official pcg32-demo output for
// srandom(42, 54).

TEST_CASE("Rng matches the official PCG32 reference vector") {
    dw::Rng rng(42u, 54u);
    const std::array<std::uint32_t, 6> expected{0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                                0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (auto value : expected) REQUIRE(rng.nextU32() == value);
}

TEST_CASE("Rng(seed) uses the fixed engine stream") {
    dw::Rng rng(12345u);
    REQUIRE(rng.nextU32() == 0xf60105d9u);
    REQUIRE(rng.nextU32() == 0xcec02ee0u);
    REQUIRE(rng.nextU32() == 0x8774b855u);
    REQUIRE(rng.nextU32() == 0xf398bc09u);
}

TEST_CASE("Rng accepts seed 0") {
    dw::Rng rng(0u);
    REQUIRE(rng.nextU32() == 0x47c28b93u);
    REQUIRE(rng.nextU32() == 0xb98f6a27u);
}

TEST_CASE("Rng is reproducible and seed dependent") {
    dw::Rng a(99u), b(99u), c(100u);
    bool differs = false;
    for (int i = 0; i < 32; ++i) {
        const auto x = a.nextU32();
        REQUIRE(x == b.nextU32());
        differs = differs || (x != c.nextU32());
    }
    REQUIRE(differs);
}

TEST_CASE("nextDouble stays in [0, 1) with a sane mean") {
    dw::Rng rng(7u);
    double sum = 0.0;
    constexpr int n = 20000;
    for (int i = 0; i < n; ++i) {
        const double v = rng.nextDouble();
        REQUIRE(v >= 0.0);
        REQUIRE(v < 1.0);
        sum += v;
    }
    REQUIRE_THAT(sum / n, Catch::Matchers::WithinAbs(0.5, 0.02));
}

TEST_CASE("nextBelow is bounded, unbiased in range and matches the reference") {
    dw::Rng ref(12345u);
    const std::array<std::uint32_t, 8> expected{1, 2, 1, 1, 3, 0, 1, 3};
    for (auto value : expected) REQUIRE(ref.nextBelow(6) == value);

    dw::Rng rng(5u);
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 2000; ++i) {
        const auto v = rng.nextBelow(6);
        REQUIRE(v < 6u);
        seen.insert(v);
    }
    REQUIRE(seen.size() == 6u);
    REQUIRE(rng.nextBelow(1) == 0u);
}

TEST_CASE("hashIndex is stateless and depends on both arguments") {
    REQUIRE(dw::hashIndex(7u, 3u) == 0xdb7b2723u);
    REQUIRE(dw::hashIndex(3u, 7u) == 0xd6e1932fu);
    REQUIRE(dw::hashIndex(7u, 4u) == 0x7263a3ecu);
    REQUIRE(dw::hashIndex(7u, 3u) == dw::hashIndex(7u, 3u));
}
