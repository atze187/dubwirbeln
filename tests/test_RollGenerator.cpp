#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

#include "engine/RollGenerator.h"

using namespace dw;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kT = kUnitsPerBar * kReferenceBars;  // 256 units for 4/4

std::vector<Sample> row(const std::string& s) {
    std::vector<Sample> r;
    for (char c : s)
        if (c == 'A') r.push_back(Sample::A);
        else if (c == 'B') r.push_back(Sample::B);
    return r;
}

std::string str(const std::vector<Sample>& r) {
    std::string s;
    for (auto v : r) s += (v == Sample::A ? 'A' : 'B');
    return s;
}

std::vector<int> grids(const std::vector<Phase>& p) {
    std::vector<int> g;
    for (const auto& ph : p) g.push_back(ph.grid);
    return g;
}

// Triggers whose position lies in [from, to).
std::vector<Trigger> between(const std::vector<Trigger>& t, int from, int to) {
    std::vector<Trigger> out;
    for (const auto& x : t)
        if (x.position >= from && x.position < to) out.push_back(x);
    return out;
}

std::string samples(const std::vector<Trigger>& t) {
    std::string s;
    for (const auto& x : t) s += (x.sample == Sample::A ? 'A' : 'B');
    return s;
}

}  // namespace

// ---- Hold time (Part 6, Intensity) -----------------------------------------------------------

TEST_CASE("holdShare is 10/25/40 percent at intensity 50/75/100") {
    REQUIRE_THAT(holdShare(50), WithinAbs(0.10, 1e-9));
    REQUIRE_THAT(holdShare(75), WithinAbs(0.25, 1e-9));
    REQUIRE_THAT(holdShare(100), WithinAbs(0.40, 1e-9));
}

TEST_CASE("holdShare below 50 is 2 % + 0.16 * I and continuous at 50") {
    REQUIRE_THAT(holdShare(0), WithinAbs(0.02, 1e-9));
    REQUIRE_THAT(holdShare(25), WithinAbs(0.06, 1e-9));
    REQUIRE_THAT(holdShare(49.999), WithinAbs(0.10, 1e-4));
}

TEST_CASE("burstShare runs linearly from 25 % to 100 %") {
    REQUIRE_THAT(burstShare(0), WithinAbs(0.25, 1e-9));
    REQUIRE_THAT(burstShare(50), WithinAbs(0.625, 1e-9));
    REQUIRE_THAT(burstShare(100), WithinAbs(1.0, 1e-9));
}

// ---- Phase plan -------------------------------------------------------------------------------

TEST_CASE("BUILD reaches 1/16, 1/32 or 1/64 depending on the intensity zone") {
    REQUIRE(grids(planPhases(RollType::Build, 10)) == std::vector<int>{8, 16});
    REQUIRE(grids(planPhases(RollType::Build, 24.9)) == std::vector<int>{8, 16});
    REQUIRE(grids(planPhases(RollType::Build, 30)) == std::vector<int>{8, 16, 32});
    REQUIRE(grids(planPhases(RollType::Build, 50)) == std::vector<int>{8, 16, 32, 64});
}

TEST_CASE("DROP mirrors BUILD") {
    REQUIRE(grids(planPhases(RollType::Drop, 10)) == std::vector<int>{16, 8});
    REQUIRE(grids(planPhases(RollType::Drop, 30)) == std::vector<int>{32, 16, 8});
    REQUIRE(grids(planPhases(RollType::Drop, 50)) == std::vector<int>{64, 32, 16, 8});
}

TEST_CASE("RUSH escalates to 1/32 below 50 and to 1/64 from 50") {
    REQUIRE(grids(planPhases(RollType::Rush, 49)) == std::vector<int>{8, 8, 8, 32});
    REQUIRE(grids(planPhases(RollType::Rush, 50)) == std::vector<int>{8, 8, 8, 32, 64});
}

TEST_CASE("WAVE alternates 1/16 with 1/32 (below 50) or 1/64 (from 50)") {
    REQUIRE(grids(planPhases(RollType::Wave, 10)) == std::vector<int>{16, 32, 16, 32});
    REQUIRE(grids(planPhases(RollType::Wave, 60)) == std::vector<int>{16, 64, 16, 64});
}

TEST_CASE("BURST is a single 1/64 phase of burstShare") {
    const auto p = planPhases(RollType::Burst, 0);
    REQUIRE(p.size() == 1u);
    REQUIRE(p[0].grid == 64);
    REQUIRE_THAT(p[0].share, WithinAbs(0.25, 1e-9));
}

TEST_CASE("The highest level gets the hold share, the rest is split equally") {
    const auto b100 = planPhases(RollType::Build, 100);
    REQUIRE(b100.size() == 4u);
    REQUIRE_THAT(b100[3].share, WithinAbs(0.40, 1e-9));
    for (int i = 0; i < 3; ++i) REQUIRE_THAT(b100[i].share, WithinAbs(0.20, 1e-9));

    const auto d50 = planPhases(RollType::Drop, 50);  // peak comes first
    REQUIRE(d50.size() == 4u);
    REQUIRE_THAT(d50[0].share, WithinAbs(0.10, 1e-9));
    for (int i = 1; i < 4; ++i) REQUIRE_THAT(d50[i].share, WithinAbs(0.30, 1e-9));

    const auto r100 = planPhases(RollType::Rush, 100);  // 5 phases, three 1/8 count separately
    REQUIRE(r100.size() == 5u);
    REQUIRE_THAT(r100[4].share, WithinAbs(0.40, 1e-9));
    for (int i = 0; i < 4; ++i) REQUIRE_THAT(r100[i].share, WithinAbs(0.15, 1e-9));

    const auto b0 = planPhases(RollType::Build, 0);
    REQUIRE(b0.size() == 2u);
    REQUIRE_THAT(b0[0].share, WithinAbs(0.98, 1e-9));
    REQUIRE_THAT(b0[1].share, WithinAbs(0.02, 1e-9));
}

TEST_CASE("WAVE has no hold time: four equal quarters") {
    const auto phases = planPhases(RollType::Wave, 100);
    REQUIRE(phases.size() == 4u);
    for (const auto& ph : phases) REQUIRE_THAT(ph.share, WithinAbs(0.25, 1e-9));
}

TEST_CASE("Phase boundaries snap to a multiple of the coarser adjacent grid step") {
    // BUILD 100 %: raw boundaries 51.2, 102.4, 153.6 -> 48 (x8), 104 (x4), 154 (x2)
    const auto b = phaseBoundaries(planPhases(RollType::Build, 100), kT);
    REQUIRE(b == std::vector<int>{0, 48, 104, 154, kT});
}

// ---- Blend (Part 6) ---------------------------------------------------------------------------

TEST_CASE("Blend 0 gives all A, 50 the original row, 100 all B") {
    REQUIRE(str(blendRow(row("ABAB"), 0)) == "AAAA");
    REQUIRE(str(blendRow(row("ABAB"), 50)) == "ABAB");
    REQUIRE(str(blendRow(row("ABAB"), 100)) == "BBBB");
}

TEST_CASE("Blend 25 and 75 reproduce the BUILD example rows") {
    // rows: 1/8 AAAA, 1/16 AABA, 1/32 ABAB, 1/64 BABA
    REQUIRE(str(blendRow(row("AAAA"), 25)) == "AAAA");
    REQUIRE(str(blendRow(row("AABA"), 25)) == "AABA");
    REQUIRE(str(blendRow(row("ABAB"), 25)) == "AAAB");
    REQUIRE(str(blendRow(row("BABA"), 25)) == "AABA");

    REQUIRE(str(blendRow(row("AAAA"), 75)) == "ABAB");
    REQUIRE(str(blendRow(row("AABA"), 75)) == "ABBB");
    REQUIRE(str(blendRow(row("ABAB"), 75)) == "ABBB");
    REQUIRE(str(blendRow(row("BABA"), 75)) == "BABB");
}

TEST_CASE("Blend works for rows of length 5 (priority order 3,1,2,4,0)") {
    REQUIRE(str(blendRow(row("ABBAB"), 25)) == "ABBAA");
    REQUIRE(str(blendRow(row("ABBAB"), 75)) == "ABBBB");
}

TEST_CASE("Blend results are nested: more blend never removes a B") {
    for (const char* original : {"AAAA", "AABA", "ABAB", "BABA", "ABBAB", "AABB"}) {
        std::string previous;
        for (int b = 0; b <= 100; b += 5) {
            const auto cur = str(blendRow(row(original), b));
            REQUIRE(cur.size() == std::string(original).size());
            if (!previous.empty())
                for (size_t i = 0; i < cur.size(); ++i)
                    if (previous[i] == 'B') REQUIRE(cur[i] == 'B');
            previous = cur;
        }
    }
}

// ---- Roll generation --------------------------------------------------------------------------

TEST_CASE("BUILD 100 %: triggers follow the phases and start on the grid") {
    const auto t = generateRoll({RollType::Build, 100, 50});
    REQUIRE(t.size() == 147u);
    REQUIRE(t.front().position == 0);

    const auto p1 = between(t, 0, 48), p2 = between(t, 48, 104), p3 = between(t, 104, 154),
               p4 = between(t, 154, kT);
    REQUIRE(p1.size() == 6u);   // 1/8 : 0,8,..,40
    REQUIRE(p2.size() == 14u);  // 1/16: 48,52,..,100
    REQUIRE(p3.size() == 25u);  // 1/32: 104,106,..,152
    REQUIRE(p4.size() == 102u); // 1/64: 154..255

    REQUIRE(p1.front().grid == 8);
    REQUIRE(p2.front().position == 48);
    REQUIRE(p2.front().grid == 16);
    REQUIRE(p3.front().grid == 32);
    REQUIRE(p4.front().grid == 64);

    for (size_t i = 1; i < t.size(); ++i) REQUIRE(t[i].position > t[i - 1].position);
    REQUIRE(t.back().position < kT);
}

TEST_CASE("BUILD 100 % at blend 50: each phase tiles its own DNA row, restarted per phase") {
    const auto t = generateRoll({RollType::Build, 100, 50});
    // rows: 1/8 AAAA, 1/16 AABA, 1/32 ABAB, 1/64 BABA
    REQUIRE(samples(between(t, 0, 48)) == "AAAAAA");
    REQUIRE(samples(between(t, 48, 104)) == "AABAAABAAABAAA");
    REQUIRE(samples(between(t, 104, 154)).substr(0, 8) == "ABABABAB");
    REQUIRE(samples(between(t, 154, kT)).substr(0, 8) == "BABABABA");
}

TEST_CASE("Blend 0 and 100 turn the whole roll into A or B") {
    for (auto type : {RollType::Build, RollType::Drop, RollType::Rush, RollType::Burst, RollType::Wave}) {
        const auto a = generateRoll({type, 100, 0});
        const auto b = generateRoll({type, 100, 100});
        REQUIRE(!a.empty());
        REQUIRE(a.size() == b.size());
        for (const auto& x : a) REQUIRE(x.sample == Sample::A);
        for (const auto& x : b) REQUIRE(x.sample == Sample::B);
    }
}

TEST_CASE("DROP uses the mirrored BUILD rows from dense to sparse") {
    const auto t = generateRoll({RollType::Drop, 100, 50});
    REQUIRE(!t.empty());
    REQUIRE(t.front().grid == 64);
    REQUIRE(t.back().grid == 8);
    // rows: 1/64 BABA, 1/32 ABAB, 1/16 AABA, 1/8 AAAA
    REQUIRE(samples(t).substr(0, 8) == "BABABABA");
    REQUIRE(t.back().sample == Sample::A);
}

TEST_CASE("RUSH: the three 1/8 phases share one row, each restarting at its own start") {
    const auto t = generateRoll({RollType::Rush, 100, 50});
    // rows: 1/8 AAAA, 1/32 AABA, 1/64 ABBAB
    REQUIRE(!t.empty());
    const auto first = between(t, 0, 40);  // plateau of 1/8
    REQUIRE(first.size() == 5u);
    for (const auto& x : first) REQUIRE(x.grid == 8);
    REQUIRE(samples(first).find('B') == std::string::npos);
    REQUIRE(t.back().grid == 64);
}

TEST_CASE("BURST: single row ABAB tiled over the whole burst, length from the burst share") {
    const auto lo = generateRoll({RollType::Burst, 0, 50});
    REQUIRE(lo.size() == 64u);  // 25 % of 256 units at 1/64
    REQUIRE(lo.front().position == 0);  // guarded by the size check above
    REQUIRE(samples(lo).substr(0, 8) == "ABABABAB");

    const auto hi = generateRoll({RollType::Burst, 100, 50});
    REQUIRE(hi.size() == 256u);
}

TEST_CASE("WAVE: single row AABB runs through all four phases without restarting") {
    const auto t = generateRoll({RollType::Wave, 10, 50});
    REQUIRE(t.size() == 96u);  // 16 + 32 + 16 + 32
    REQUIRE(between(t, 0, 64).size() == 16u);
    REQUIRE(between(t, 0, 64).front().grid == 16);
    REQUIRE(between(t, 64, 128).front().grid == 32);
    REQUIRE(samples(t).substr(0, 8) == "AABBAABB");
    REQUIRE(samples(t).substr(16, 4) == "AABB");  // index 16 = start of phase 2, no restart needed
}

TEST_CASE("Generation is deterministic") {
    const RollSpec spec{RollType::Rush, 73, 41};
    const auto first = generateRoll(spec);
    REQUIRE(!first.empty());
    REQUIRE(first == generateRoll(spec));
}

// ---- Windowing (Part 6) -----------------------------------------------------------------------

TEST_CASE("A 4-bar window keeps the whole roll") {
    const auto t = generateRoll({RollType::Build, 100, 50});
    REQUIRE(!t.empty());
    REQUIRE(applyWindow(t, RollType::Build, 4) == t);
}

TEST_CASE("BUILD trims the early portion and rebases the rest to zero") {
    const auto t = generateRoll({RollType::Build, 100, 50});
    const auto w = applyWindow(t, RollType::Build, 1);
    REQUIRE(w.size() == 64u);
    for (const auto& x : w) {
        REQUIRE(x.position >= 0);
        REQUIRE(x.position < kUnitsPerBar);
    }
    // 1 bar = last 64 units = positions 192..255 -> all 1/64
    for (const auto& x : w) REQUIRE(x.grid == 64);
    REQUIRE(w.size() == 64u);
}

TEST_CASE("DROP trims the end and keeps the dense start") {
    const auto t = generateRoll({RollType::Drop, 100, 50});
    const auto w = applyWindow(t, RollType::Drop, 1);
    REQUIRE(w.size() == 64u);
    REQUIRE(w.front().position == 0);
    for (const auto& x : w) {
        REQUIRE(x.position < kUnitsPerBar);
        REQUIRE(x.grid == 64);  // 1/64 fills the first 40 % = 102 units
    }
}

TEST_CASE("BURST starts immediately and is cut to the window") {
    const auto t = generateRoll({RollType::Burst, 100, 50});
    const auto w = applyWindow(t, RollType::Burst, 2);
    REQUIRE(w.size() == 128u);
    REQUIRE(w.front().position == 0);  // guarded by the size check above
}

TEST_CASE("WAVE trims the early portion") {
    const auto t = generateRoll({RollType::Wave, 10, 50});
    const auto w = applyWindow(t, RollType::Wave, 2);
    REQUIRE(w.size() == 48u);  // 16 + 32 from the last two phases
    REQUIRE(w.front().position >= 0);
    for (const auto& x : w) REQUIRE(x.position < 2 * kUnitsPerBar);
}
