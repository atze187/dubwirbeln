#include "engine/Variate.h"

#include <algorithm>
#include <cmath>

#include "engine/Rng.h"

namespace dw {

namespace {

constexpr std::uint64_t kVariateStream = 7;
constexpr double kIntensityRange = 15.0;  // spec Part 10, percentage points
constexpr double kBlendRange = 20.0;
constexpr int kCurveRange = 10;           // velocity units per curve point

double roundToTenth(double x) { return std::floor(x * 10.0 + 0.5 + 1e-9) / 10.0; }

// The Intensity zones decide which grid levels a roll reaches, so Variate never leaves them
// (spec Part 10). Steps are 0.1, so the upper ends of the first two zones are 24.9 and 49.9.
void intensityZone(double intensity, double& lo, double& hi) {
    if (intensity < 25.0) {
        lo = 0.0;
        hi = 24.9;
    } else if (intensity < 50.0) {
        lo = 25.0;
        hi = 49.9;
    } else {
        lo = 50.0;
        hi = 100.0;
    }
}

}  // namespace

VariateSnapshot variateSnapshot(const Preset& preset) {
    return {preset.intensity, preset.blend, preset.curve, preset.variationSeed};
}

void restoreSnapshot(Preset& preset, const VariateSnapshot& snapshot) {
    preset.intensity = snapshot.intensity;
    preset.blend = snapshot.blend;
    preset.curve = snapshot.curve;
    preset.variationSeed = snapshot.variationSeed;
}

bool applyVariate(Preset& preset, std::uint32_t passSeed) {
    if (!preset.rollType) return false;
    if (passSeed == 0) passSeed = 1;

    Rng rng(static_cast<std::uint64_t>(passSeed), kVariateStream);
    const double intensityDelta = (2.0 * rng.nextDouble() - 1.0) * kIntensityRange;
    const double blendDelta = (2.0 * rng.nextDouble() - 1.0) * kBlendRange;

    double lo = 0.0, hi = 100.0;
    intensityZone(preset.intensity, lo, hi);
    preset.intensity = roundToTenth(std::clamp(preset.intensity + intensityDelta, lo, hi));
    preset.blend = roundToTenth(std::clamp(preset.blend + blendDelta, 0.0, 100.0));

    for (auto& point : preset.curve) {
        const int delta = static_cast<int>(rng.nextBelow(2u * kCurveRange + 1u)) - kCurveRange;
        point.v = std::clamp(std::round(point.v) + delta, 1.0, 127.0);
    }

    preset.variationSeed = passSeed;
    return true;
}

void VariateHistory::record(VariateSnapshot before, VariateSnapshot after) {
    pass_ = Pass{std::move(before), std::move(after), false};
}

bool VariateHistory::canUndo() const { return pass_ && !pass_->undone; }

bool VariateHistory::canRedo() const { return pass_ && pass_->undone; }

bool VariateHistory::undo(Preset& preset) {
    if (!canUndo()) return false;
    restoreSnapshot(preset, pass_->before);
    pass_->undone = true;
    return true;
}

bool VariateHistory::redo(Preset& preset) {
    if (!canRedo()) return false;
    restoreSnapshot(preset, pass_->after);
    pass_->undone = false;
    return true;
}

void VariateHistory::clear() { pass_.reset(); }

void clearRoll(Preset& preset) {
    const Preset defaults = defaultPreset(0);
    preset.rollType.reset();
    preset.gesture.clear();
    preset.intensity = defaults.intensity;
    preset.blend = defaults.blend;
    preset.variationSeed = 0;
}

}  // namespace dw
