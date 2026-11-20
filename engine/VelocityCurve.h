#pragma once

#include <vector>

namespace dw {

enum class CurveType { Flat, Crescendo, Decrescendo, Spike, InvertedSpike, Wave, Ghost };

// `t` is 0..1 over the 4-bar reference, `v` is a MIDI velocity 1..127 (spec Part 6).
struct CurvePoint {
    double t = 0.0;
    double v = 0.0;
    bool operator==(const CurvePoint&) const = default;
};

// Control points of a curve type's template (spec Part 6: a type is a template, the points are
// freely editable afterwards).
std::vector<CurvePoint> curveTemplate(CurveType type);

// Linear interpolation between the points; constant before the first and after the last point.
// Returns 0 for an empty curve.
double evaluateCurve(const std::vector<CurvePoint>& points, double t);

}  // namespace dw
