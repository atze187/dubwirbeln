#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "engine/Preset.h"

namespace dw {

// The values a Variate pass changes (spec Part 10).
struct VariateSnapshot {
    double intensity = 50.0;
    double blend = 50.0;
    std::vector<CurvePoint> curve;
    std::uint32_t variationSeed = 0;
    bool operator==(const VariateSnapshot&) const = default;
};

VariateSnapshot variateSnapshot(const Preset& preset);
void restoreSnapshot(Preset& preset, const VariateSnapshot& snapshot);

// One Variate pass. All randomness comes from `passSeed` (the caller picks it at random; 0 is
// replaced by 1), so the same seed on the same preset gives the same result. Returns false and
// changes nothing when the preset has no roll.
//   Intensity +/- 15 points, kept inside its zone (0-24.9, 25-49.9, 50-100), in steps of 0.1
//   Blend +/- 20 points, clamped to 0-100, in steps of 0.1
//   every velocity curve point +/- 10 (whole numbers), clamped to 1-127
//   variation.seed = the pass seed: it derives the density and sample DNA changes
// The random draws are taken in this order: intensity, blend, then the curve points.
bool applyVariate(Preset& preset, std::uint32_t passSeed);

// Undo/Redo of the last Variate pass only: a single level, no chain (spec Part 10).
class VariateHistory {
public:
    // Remembers a pass, replacing the previous one.
    void record(VariateSnapshot before, VariateSnapshot after);
    bool canUndo() const;
    bool canRedo() const;
    bool undo(Preset& preset);  // back to the state before the pass
    bool redo(Preset& preset);  // forward again, after an undo
    void clear();

private:
    struct Pass {
        VariateSnapshot before;
        VariateSnapshot after;
        bool undone = false;
    };
    std::optional<Pass> pass_;
};

// Empties the roll (spec Part 10): no roll type, no gesture, Intensity and Blend back to their
// defaults, variation.seed 0. Samples, velocity curve, playback settings and humanize.seed stay.
void clearRoll(Preset& preset);

}  // namespace dw
