#include "engine/VelocityCurve.h"

namespace dw {

std::vector<CurvePoint> curveTemplate(CurveType type) {
    switch (type) {
        case CurveType::Flat: return {{0.0, 100.0}, {1.0, 100.0}};
        case CurveType::Crescendo: return {{0.0, 40.0}, {1.0, 127.0}};
        case CurveType::Decrescendo: return {{0.0, 127.0}, {1.0, 40.0}};
        case CurveType::Spike: return {{0.0, 70.0}, {0.5, 127.0}, {1.0, 70.0}};
        case CurveType::InvertedSpike: return {{0.0, 110.0}, {0.5, 40.0}, {1.0, 110.0}};
        case CurveType::Wave: return {{0.0, 60.0}, {0.25, 120.0}, {0.5, 60.0}, {0.75, 120.0}, {1.0, 60.0}};
        case CurveType::Ghost:
            // Quiet at 35, with a narrow accent of 100 at the start of every bar.
            return {{0.0, 100.0},  {0.02, 35.0},  {0.23, 35.0}, {0.25, 100.0}, {0.27, 35.0}, {0.48, 35.0},
                    {0.5, 100.0},  {0.52, 35.0},  {0.73, 35.0}, {0.75, 100.0}, {0.77, 35.0}, {1.0, 35.0}};
    }
    return {};
}

double evaluateCurve(const std::vector<CurvePoint>& points, double t) {
    if (points.empty()) return 0.0;
    if (t <= points.front().t) return points.front().v;
    if (t >= points.back().t) return points.back().v;
    for (size_t i = 1; i < points.size(); ++i) {
        if (t > points[i].t) continue;
        const auto& a = points[i - 1];
        const auto& b = points[i];
        const double span = b.t - a.t;
        return span <= 0.0 ? b.v : a.v + (b.v - a.v) * (t - a.t) / span;
    }
    return points.back().v;
}

}  // namespace dw
