#include "engine/VelocityFollow.h"

#include <algorithm>
#include <cmath>

namespace dw {

namespace {
int clampVelocity(int v) { return std::clamp(v, 1, 127); }
}  // namespace

double effectiveIntensity(double storedIntensity, VelocityFollow follow, int playedVelocity) {
    if (follow != VelocityFollow::VelocityIntensity) return storedIntensity;
    return (clampVelocity(playedVelocity) - 1) / 126.0 * 100.0;
}

int followVelocity(int triggerVelocity, VelocityFollow follow, int playedVelocity) {
    if (follow == VelocityFollow::Off) return triggerVelocity;
    const double scaled = triggerVelocity * clampVelocity(playedVelocity) / 127.0;
    return clampVelocity(static_cast<int>(std::floor(scaled + 0.5 + 1e-9)));
}

}  // namespace dw
