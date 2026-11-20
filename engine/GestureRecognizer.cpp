#include "engine/GestureRecognizer.h"

#include <algorithm>
#include <cmath>

namespace dw {

namespace {

constexpr double kTiny = 1e-9;
constexpr int kResampleCount = 64;
constexpr int kSmoothHalfWindow = 2;     // moving average over 5 points
constexpr double kDotSize = 0.15;        // a stroke is a dot if it is smaller than this part of the gesture
constexpr double kMaxPoints = 512;       // schema limit of the stored gesture

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

struct Bounds {
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
    double size() const { return std::max(width(), height()); }
    void add(const GesturePoint& p) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
        if (!any) {
            minX = maxX = p.x;
            minY = maxY = p.y;
            any = true;
            return;
        }
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
};

Bounds boundsOf(const Stroke& s) {
    Bounds b;
    for (const auto& p : s) b.add(p);
    return b;
}

Bounds boundsOf(const Gesture& g) {
    Bounds b;
    for (const auto& s : g)
        for (const auto& p : s) b.add(p);
    return b;
}

// Resamples a polyline to `n` points that are evenly spaced along its length.
Stroke resample(const Stroke& poly, int n) {
    Stroke out;
    if (poly.empty()) return out;
    std::vector<double> cumulative(poly.size(), 0.0);
    for (size_t i = 1; i < poly.size(); ++i)
        cumulative[i] = cumulative[i - 1] + std::hypot(poly[i].x - poly[i - 1].x, poly[i].y - poly[i - 1].y);
    const double total = cumulative.back();
    if (total < kTiny) {
        out.assign(static_cast<size_t>(n), poly.front());
        return out;
    }
    size_t seg = 1;
    for (int i = 0; i < n; ++i) {
        const double target = total * i / (n - 1);
        while (seg + 1 < poly.size() && cumulative[seg] < target) ++seg;
        const double span = cumulative[seg] - cumulative[seg - 1];
        const double t = span < kTiny ? 0.0 : clamp01((target - cumulative[seg - 1]) / span);
        out.push_back({poly[seg - 1].x + (poly[seg].x - poly[seg - 1].x) * t,
                       poly[seg - 1].y + (poly[seg].y - poly[seg - 1].y) * t});
    }
    return out;
}

// Moving average of x and y; the window shrinks at the ends so they stay where they are.
Stroke smooth(const Stroke& in) {
    Stroke out = in;
    const int n = static_cast<int>(in.size());
    for (int i = 0; i < n; ++i) {
        const int half = std::min({kSmoothHalfWindow, i, n - 1 - i});
        double sx = 0, sy = 0;
        for (int j = i - half; j <= i + half; ++j) {
            sx += in[static_cast<size_t>(j)].x;
            sy += in[static_cast<size_t>(j)].y;
        }
        out[static_cast<size_t>(i)] = {sx / (2 * half + 1), sy / (2 * half + 1)};
    }
    return out;
}

// How far the shape rises or falls over the whole gesture, in parts of the gesture size: below 10 %
// it is flat, from 30 % it counts fully.
double heightFactor(double rise) { return clamp01((std::fabs(rise) - 0.10) / 0.20); }

struct Scores {
    double build = 0, drop = 0, rush = 0, burst = 0, wave = 0;
};

// BUILD, DROP, RUSH and WAVE from the outline of the gesture (all strokes joined in drawing order).
Scores shapeScores(const Gesture& normalized) {
    Scores s;
    Stroke path;
    for (const auto& stroke : normalized)
        for (const auto& p : stroke)
            if (std::isfinite(p.x) && std::isfinite(p.y)) path.push_back(p);
    if (path.size() < 2) return s;

    // Left to right, whichever way it was drawn: `/` is BUILD from the bottom or from the top.
    if (path.back().x < path.front().x) std::reverse(path.begin(), path.end());

    const Stroke pts = smooth(resample(path, kResampleCount));
    std::vector<double> ys;
    double totalX = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        ys.push_back(pts[i].y);
        if (i > 0) totalX += std::fabs(pts[i].x - pts[i - 1].x);
    }
    if (totalX < kTiny) return s;  // straight up or down is not a ramp

    // A gesture must travel from left to right; a circle or a scribble does not.
    const double progress = (pts.back().x - pts.front().x) / totalX;
    const double progressFactor = clamp01((progress - 0.5) / 0.3);

    const auto swings = zigzagSwings(ys, kSwingThreshold);
    if (swings.empty()) return s;  // nothing moves by more than 10 %: a flat line
    const int changes = static_cast<int>(swings.size()) - 1;
    const double rise = pts.back().y - pts.front().y;

    if (changes == 0) {
        // How straight is the line? The largest distance of any point from the chord.
        const double dx = pts.back().x - pts.front().x;
        const double length = std::hypot(dx, rise);
        double deviation = 0.0;
        if (length > kTiny)
            for (const auto& p : pts)
                deviation = std::max(deviation, std::fabs(dx * (p.y - pts.front().y) - rise * (p.x - pts.front().x)) / length);
        const double linearity = 1.0 - clamp01((deviation / std::max(length, kTiny) - 0.05) / 0.15);

        if (rise > 0) s.build = heightFactor(rise) * linearity;
        else s.drop = heightFactor(rise) * linearity;

        // Flat first, the rise comes late: the height at half the width, in parts of the rise.
        if (rise > 0) {
            const double midX = pts.front().x + 0.5 * (pts.back().x - pts.front().x);
            size_t k = 0;
            while (k + 1 < pts.size() && pts[k].x < midX) ++k;
            const double r = (pts[k].y - pts.front().y) / rise;
            s.rush = heightFactor(rise) * clamp01((0.45 - r) / 0.30);
        }
    } else if (changes == 1) {
        s.wave = 0.35;  // `/\`: one clear turn is neither a ramp nor a wave
    } else {
        double smallest = swings[1];
        for (size_t i = 1; i + 1 < swings.size(); ++i) smallest = std::min(smallest, swings[i]);
        const double count = changes == 2 ? 0.85 : 1.0;
        const double amplitude = std::clamp(0.6 + 0.4 * (smallest - kSwingThreshold) / 0.10, 0.6, 1.0);
        s.wave = count * amplitude;
    }

    s.build *= progressFactor;
    s.drop *= progressFactor;
    s.rush *= progressFactor;
    s.wave *= progressFactor;
    return s;
}

}  // namespace

GestureDecision decideGesture(double confidence) {
    if (confidence > kApplySilently) return GestureDecision::Apply;
    if (confidence >= kSuggestFrom) return GestureDecision::ApplyWithAlternative;
    return GestureDecision::Reject;
}

std::vector<double> zigzagSwings(const std::vector<double>& values, double threshold) {
    std::vector<double> swings;
    if (values.size() < 2) return swings;

    // Until a first swing is clear, remember the lowest and highest value so far.
    double lo = values[0], hi = values[0];
    int direction = 0;  // +1 rising, -1 falling
    double pivot = 0.0, extreme = 0.0;
    for (size_t i = 1; i < values.size(); ++i) {
        const double v = values[i];
        if (direction == 0) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            if (v - lo > threshold) {
                direction = 1;
                pivot = lo;
                extreme = v;
            } else if (hi - v > threshold) {
                direction = -1;
                pivot = hi;
                extreme = v;
            }
        } else if (direction > 0) {
            if (v > extreme) {
                extreme = v;
            } else if (extreme - v > threshold) {
                swings.push_back(extreme - pivot);
                pivot = extreme;
                extreme = v;
                direction = -1;
            }
        } else {
            if (v < extreme) {
                extreme = v;
            } else if (v - extreme > threshold) {
                swings.push_back(pivot - extreme);
                pivot = extreme;
                extreme = v;
                direction = 1;
            }
        }
    }
    if (direction != 0) swings.push_back(std::fabs(extreme - pivot));
    return swings;
}

Gesture normalizeGesture(const Gesture& gesture) {
    const Bounds b = boundsOf(gesture);
    Gesture out = gesture;
    if (!b.any) return out;
    const double size = std::max(b.size(), kTiny);
    for (auto& stroke : out)
        for (auto& p : stroke) {
            p.x = std::isfinite(p.x) ? (p.x - b.minX) / size : 0.0;
            p.y = std::isfinite(p.y) ? (p.y - b.minY) / size : 0.0;
        }
    return out;
}

std::vector<GesturePoint> flattenForPreset(const Gesture& gesture) {
    std::vector<GesturePoint> all;
    for (const auto& stroke : normalizeGesture(gesture))
        for (const auto& p : stroke) all.push_back({clamp01(p.x), clamp01(p.y)});
    if (all.size() < 2) return {};
    if (all.size() <= static_cast<size_t>(kMaxPoints)) return all;

    std::vector<GesturePoint> thinned;
    const auto last = static_cast<double>(all.size() - 1);
    for (int i = 0; i < static_cast<int>(kMaxPoints); ++i)
        thinned.push_back(all[static_cast<size_t>(std::llround(last * i / (kMaxPoints - 1)))]);
    return thinned;
}

Recognition recognizeGesture(const Gesture& gesture) {
    Recognition result;
    const Bounds all = boundsOf(gesture);
    if (!all.any || all.size() < kMinGestureSize) return result;  // nothing drawn, or just a click

    // Dots: several strokes that are all small compared to the whole gesture.
    int strokes = 0, dots = 0;
    for (const auto& stroke : gesture) {
        const Bounds b = boundsOf(stroke);
        if (!b.any) continue;
        ++strokes;
        if (b.size() < kDotSize * all.size()) ++dots;
    }
    Scores scores;
    double dotFraction = 0.0;
    if (strokes >= 2) {
        dotFraction = static_cast<double>(dots) / strokes;
        scores.burst = dotFraction * dotFraction * clamp01((strokes - 1) / 2.0);
    }

    const Scores shape = shapeScores(normalizeGesture(gesture));
    const double shapeWeight = 1.0 - dotFraction;  // dots are dots, even when they lie on a line
    scores.build = shape.build * shapeWeight;
    scores.drop = shape.drop * shapeWeight;
    scores.rush = shape.rush * shapeWeight;
    scores.wave = shape.wave * shapeWeight;

    std::vector<GestureCandidate> ranked = {{RollType::Build, scores.build},
                                            {RollType::Drop, scores.drop},
                                            {RollType::Rush, scores.rush},
                                            {RollType::Burst, scores.burst},
                                            {RollType::Wave, scores.wave}};
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const GestureCandidate& a, const GestureCandidate& b) { return a.confidence > b.confidence; });

    if (ranked[0].confidence <= 0.0) return result;
    result.best = ranked[0];
    if (ranked[1].confidence >= kMinAlternative) result.alternative = ranked[1];
    result.decision = decideGesture(ranked[0].confidence);
    return result;
}

}  // namespace dw
