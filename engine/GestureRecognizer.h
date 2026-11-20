#pragma once

#include <optional>
#include <vector>

#include "engine/Preset.h"

namespace dw {

// A drawn gesture: strokes (pen down to pen up) of points. The y axis points up, so `/` rises from
// the bottom left to the top right. Dots (BURST) are several very small strokes.
using Stroke = std::vector<GesturePoint>;
using Gesture = std::vector<Stroke>;

struct GestureCandidate {
    RollType type = RollType::Build;
    double confidence = 0.0;  // 0..1
};

enum class GestureDecision {
    Apply,                // above 80 %: apply silently
    ApplyWithAlternative, // 60 to 80 %: apply the best guess, the status bar offers the alternative
    Reject                // below 60 %: ask the user to redraw, the roll stays as it is
};

struct Recognition {
    std::optional<GestureCandidate> best;         // the most likely roll type, if any fits at all
    std::optional<GestureCandidate> alternative;  // the second guess, only if it is plausible
    GestureDecision decision = GestureDecision::Reject;
};

// Thresholds (spec Part 5).
constexpr double kApplySilently = 0.80;   // above this: silent
constexpr double kSuggestFrom = 0.60;     // from this: apply and show the alternative
constexpr double kSwingThreshold = 0.10;  // a direction change counts above 10 % of the gesture size
constexpr double kMinAlternative = 0.35;  // weaker second guesses are not shown
// Coordinates are relative to the drawing area (0..1). A gesture smaller than this is a click.
constexpr double kMinGestureSize = 0.02;

GestureDecision decideGesture(double confidence);

// Recognizes the roll type of a gesture (spec Part 5). It only decides the roll type; length,
// steepness and speed of the gesture play no role. Deterministic.
Recognition recognizeGesture(const Gesture& gesture);

// The swings of a zigzag through `values`: the rises and falls that are larger than `threshold`
// (strictly: a swing of exactly the threshold is noise). Smaller wiggles are ignored.
std::vector<double> zigzagSwings(const std::vector<double>& values, double threshold);

// Moves the gesture to the origin and scales both axes by the same factor so that the longer side
// is 1. Proportions are kept, so a flat line stays flat.
Gesture normalizeGesture(const Gesture& gesture);

// The gesture as the preset stores it (schema: 2 to 512 points in 0..1): normalized, strokes
// joined in drawing order, thinned out evenly if there are more than 512 points. Empty if the
// gesture has fewer than 2 points.
std::vector<GesturePoint> flattenForPreset(const Gesture& gesture);

}  // namespace dw
