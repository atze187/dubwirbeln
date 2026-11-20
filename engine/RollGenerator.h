#pragma once

#include <cstdint>
#include <vector>

namespace dw {

enum class RollType { Build, Drop, Rush, Burst, Wave };
enum class Sample { A, B };

// Percent values are 0..100 as in the spec.
struct RollSpec {
    RollType type = RollType::Build;
    double intensity = 50.0;
    double blend = 50.0;
    std::uint32_t variationSeed = 0;  // 0 = no variation (spec Part 10)
};

// One generated trigger. `position` is in 1/64-note units from the start of the reference,
// `grid` is the note denominator (8, 16, 32, 64) of the phase it belongs to.
struct Trigger {
    int position = 0;
    int grid = 0;
    Sample sample = Sample::A;
    bool operator==(const Trigger&) const = default;
};

struct Phase {
    int grid = 0;       // denominator
    double share = 0.0; // fraction of the reference
    bool hold = false;  // the phase that holds the highest grid level
};

constexpr int kUnitsPerBar = 64;  // 4/4 in 1/64 units
constexpr int kReferenceBars = 4;

// Stubs: the tests are written first and must fail against these.
double holdShare(double intensity);
double burstShare(double intensity);
// With a variation seed the shares of the phases that are not held are shifted by up to 15 %
// (relative) and renormalized: grid levels and hold time stay the same (spec Part 10).
std::vector<Phase> planPhases(RollType type, double intensity, std::uint32_t variationSeed = 0);
// Sample DNA expression (spec Part 10): in every run of triggers of the same grid level, up to 20 %
// (depending on the seed) of the triggers swap A and B. Seed 0 changes nothing.
void varySamples(std::vector<Trigger>& triggers, std::uint32_t variationSeed);

std::vector<int> phaseBoundaries(const std::vector<Phase>& phases, int referenceUnits);
std::vector<Sample> blendRow(const std::vector<Sample>& row, double blend);
std::vector<Trigger> generateRoll(const RollSpec& spec, int barUnits = kUnitsPerBar);
// The part of the 4-bar reference a window keeps: positions in [begin, end), moved by -shift.
struct WindowRange {
    int begin = 0;
    int end = 0;
    int shift = 0;
};
WindowRange windowRange(RollType type, double windowBars, int barUnits = kUnitsPerBar);

std::vector<Trigger> applyWindow(const std::vector<Trigger>& triggers, RollType type,
                                 double windowBars, int barUnits = kUnitsPerBar);

}  // namespace dw
