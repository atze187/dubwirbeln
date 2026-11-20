#include "engine/Expression.h"

#include <algorithm>
#include <cmath>

#include "engine/Rng.h"

namespace dw {

namespace {

int roundHalfUp(double x) { return static_cast<int>(std::floor(x + 0.5 + 1e-9)); }

// Velocity (curve + humanize), swing and humanize timing of the trigger with the given index.
ExpressedTrigger express(const Trigger& trigger, size_t index, const ExpressionSpec& spec, double total) {
    const double humanize = std::clamp(spec.humanize, 0.0, 100.0) / 100.0;
    const double swing = std::clamp(spec.swing, 0.0, 100.0) / 100.0;

    ExpressedTrigger e;
    e.trigger = trigger;
    e.index = static_cast<int>(index);

    double velocity = evaluateCurve(spec.curve, e.trigger.position / total);

    // Swing: every second step of the trigger's grid is delayed, 100 % gives a triplet feel.
    const int step = e.trigger.grid > 0 ? kUnitsPerBar / e.trigger.grid : 0;
    if (step > 0 && (e.trigger.position / step) % 2 == 1) e.swingUnits = swing * step / 3.0;

    // Humanize: a pure function of seed and trigger index, so trimming does not shift it.
    if (humanize > 0.0) {
        Rng rng(static_cast<std::uint64_t>(spec.humanizeSeed), static_cast<std::uint64_t>(index));
        const double a = rng.nextDouble();
        const double b = rng.nextDouble();
        e.humanizeMs = humanize * kHumanizeMaxMs * (2.0 * a - 1.0);
        velocity += humanize * kHumanizeMaxVelocity * (2.0 * b - 1.0);
    }

    e.velocity = std::clamp(roundHalfUp(velocity), 1, 127);
    return e;
}

}  // namespace

std::vector<ExpressedTrigger> expressTriggers(const std::vector<Trigger>& fullRoll, const ExpressionSpec& spec,
                                              int barUnits) {
    const double total = static_cast<double>(barUnits) * kReferenceBars;
    std::vector<ExpressedTrigger> out;
    out.reserve(fullRoll.size());
    for (size_t i = 0; i < fullRoll.size(); ++i) out.push_back(express(fullRoll[i], i, spec, total));
    return out;
}

void expressTriggersInto(const TriggerList& fullRoll, const ExpressionSpec& spec, int barUnits, ExpressedList& out) {
    const double total = static_cast<double>(barUnits) * kReferenceBars;
    out.clear();
    for (size_t i = 0; i < fullRoll.size(); ++i) out.push_back(express(fullRoll[i], i, spec, total));
}

std::vector<ExpressedTrigger> applyWindow(const std::vector<ExpressedTrigger>& triggers, RollType type,
                                          double windowBars, int barUnits) {
    const auto range = windowRange(type, windowBars, barUnits);
    std::vector<ExpressedTrigger> out;
    for (auto e : triggers) {
        if (e.trigger.position < range.begin || e.trigger.position >= range.end) continue;
        e.trigger.position -= range.shift;
        out.push_back(e);
    }
    return out;
}

void applyWindowInto(ExpressedList& triggers, RollType type, double windowBars, int barUnits) {
    const auto range = windowRange(type, windowBars, barUnits);
    size_t kept = 0;
    for (size_t i = 0; i < triggers.size(); ++i) {
        ExpressedTrigger e = triggers[i];
        if (e.trigger.position < range.begin || e.trigger.position >= range.end) continue;
        e.trigger.position -= range.shift;
        triggers[kept++] = e;
    }
    triggers.truncate(kept);
}

}  // namespace dw
