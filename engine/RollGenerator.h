#pragma once

#include <vector>

namespace dw {

enum class RollType { Build, Drop, Rush, Burst, Wave };
enum class Sample { A, B };

// Percent values are 0..100 as in the spec.
struct RollSpec {
    RollType type = RollType::Build;
    double intensity = 50.0;
    double blend = 50.0;
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
};

constexpr int kUnitsPerBar = 64;  // 4/4 in 1/64 units
constexpr int kReferenceBars = 4;

// Stubs: the tests are written first and must fail against these.
double holdShare(double intensity);
double burstShare(double intensity);
std::vector<Phase> planPhases(RollType type, double intensity);
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
