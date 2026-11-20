#pragma once

#include <cstdint>
#include <vector>

#include "engine/RollGenerator.h"
#include "engine/VelocityCurve.h"

namespace dw {

// Humanize at 100 % deviates by up to +/- these amounts, linearly scaled by the percent value.
constexpr double kHumanizeMaxMs = 10.0;
constexpr double kHumanizeMaxVelocity = 12.0;

struct ExpressionSpec {
    std::vector<CurvePoint> curve;  // velocity curve over the 4-bar reference
    double humanize = 0.0;          // percent 0..100
    double swing = 0.0;             // percent 0..100, 100 = triplet feel
    std::uint32_t humanizeSeed = 0;
};

// A trigger with its performance data. `index` is the trigger's index in the full 4-bar roll
// and stays the same when the window trims the roll, so humanize does not shift.
struct ExpressedTrigger {
    Trigger trigger;
    int index = 0;
    int velocity = 0;           // 1..127
    double swingUnits = 0.0;    // timing shift in 1/64 units (tempo independent)
    double humanizeMs = 0.0;    // timing shift in milliseconds
};

// Velocity (curve + humanize), swing and humanize timing for every trigger of the full roll.
std::vector<ExpressedTrigger> expressTriggers(const std::vector<Trigger>& fullRoll, const ExpressionSpec& spec,
                                              int barUnits = kUnitsPerBar);

// Same trimming as applyWindow, keeping the performance data.
std::vector<ExpressedTrigger> applyWindow(const std::vector<ExpressedTrigger>& triggers, RollType type,
                                          double windowBars, int barUnits = kUnitsPerBar);

}  // namespace dw
