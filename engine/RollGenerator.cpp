#include "engine/RollGenerator.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

namespace dw {

namespace {

constexpr double kEps = 1e-9;

// Round half up with a tiny tolerance so exact .5 cases do not depend on float noise.
int roundHalfUp(double x) { return static_cast<int>(std::floor(x + 0.5 + kEps)); }

// Units (1/64 notes) between two triggers of a grid level: 1/8 -> 8, 1/16 -> 4, 1/32 -> 2, 1/64 -> 1.
int stepUnits(int grid) { return 64 / grid; }

std::vector<Sample> parseRow(const char* s) {
    std::vector<Sample> row;
    for (; *s; ++s) row.push_back(*s == 'B' ? Sample::B : Sample::A);
    return row;
}

bool usesSingleRow(RollType type) { return type == RollType::Burst || type == RollType::Wave; }

// Sample DNA rows by grid level (spec Part 6). DROP uses the same rows as BUILD, because its
// phases run from dense to sparse and each row belongs to a grid level, not to a phase index.
std::vector<Sample> dnaRow(RollType type, int grid) {
    switch (type) {
        case RollType::Build:
        case RollType::Drop:
            switch (grid) {
                case 8: return parseRow("AAAA");
                case 16: return parseRow("AABA");
                case 32: return parseRow("ABAB");
                default: return parseRow("BABA");  // 64
            }
        case RollType::Rush:
            switch (grid) {
                case 8: return parseRow("AAAA");
                case 32: return parseRow("AABA");
                default: return parseRow("ABBAB");  // 64
            }
        case RollType::Burst: return parseRow("ABAB");
        case RollType::Wave: return parseRow("AABB");
    }
    return {};
}

// Priority order P for blending: weak positions first (odd indices, then 2, 4, ...; index 0 last),
// ties to the higher index. For n = 4 this is 3, 1, 2, 0.
std::vector<int> priorityOrder(int n) {
    auto weakness = [](int i) { return i == 0 ? 1000 : std::countr_zero(static_cast<unsigned>(i)); };
    std::vector<int> order(static_cast<size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int wa = weakness(a), wb = weakness(b);
        return wa != wb ? wa < wb : a > b;
    });
    return order;
}

}  // namespace

double holdShare(double intensity) {
    return intensity >= 50.0 ? 0.10 + 0.006 * (intensity - 50.0) : 0.02 + 0.0016 * intensity;
}

double burstShare(double intensity) { return 0.25 + 0.0075 * intensity; }

std::vector<Phase> planPhases(RollType type, double intensity) {
    if (type == RollType::Burst) return {{64, burstShare(intensity)}};

    if (type == RollType::Wave) {
        const int fast = intensity >= 50.0 ? 64 : 32;
        return {{16, 0.25}, {fast, 0.25}, {16, 0.25}, {fast, 0.25}};
    }

    std::vector<int> grids;
    switch (type) {
        case RollType::Build:
        case RollType::Drop:
            if (intensity < 25.0) grids = {8, 16};
            else if (intensity < 50.0) grids = {8, 16, 32};
            else grids = {8, 16, 32, 64};
            break;
        default:  // Rush
            grids = intensity < 50.0 ? std::vector<int>{8, 8, 8, 32} : std::vector<int>{8, 8, 8, 32, 64};
            break;
    }
    if (type == RollType::Drop) std::reverse(grids.begin(), grids.end());

    const size_t peak = type == RollType::Drop ? 0 : grids.size() - 1;
    const double hold = holdShare(intensity);
    const double rest = (1.0 - hold) / static_cast<double>(grids.size() - 1);

    std::vector<Phase> phases;
    for (size_t i = 0; i < grids.size(); ++i) phases.push_back({grids[i], i == peak ? hold : rest});
    return phases;
}

std::vector<int> phaseBoundaries(const std::vector<Phase>& phases, int referenceUnits) {
    std::vector<int> bounds{0};
    double cumulative = 0.0;
    for (size_t i = 0; i < phases.size(); ++i) {
        cumulative += phases[i].share;
        const double raw = cumulative * referenceUnits;
        if (i + 1 == phases.size()) {
            bounds.push_back(roundHalfUp(raw));
        } else {
            // Snap to a multiple of the coarser neighbouring step so both phases stay on the grid.
            const int m = std::max(stepUnits(phases[i].grid), stepUnits(phases[i + 1].grid));
            bounds.push_back(std::max(bounds.back(), roundHalfUp(raw / m) * m));
        }
    }
    return bounds;
}

std::vector<Sample> blendRow(const std::vector<Sample>& row, double blend) {
    const int n = static_cast<int>(row.size());
    const int k = static_cast<int>(std::count(row.begin(), row.end(), Sample::B));
    const double b = std::clamp(blend, 0.0, 100.0);
    const auto order = priorityOrder(n);

    std::vector<Sample> out(row.size(), Sample::A);
    if (b <= 50.0) {
        // Keep the first `keep` original B positions in priority order.
        int keep = roundHalfUp(k * b / 50.0);
        for (int idx : order) {
            if (keep == 0) break;
            if (row[static_cast<size_t>(idx)] == Sample::B) {
                out[static_cast<size_t>(idx)] = Sample::B;
                --keep;
            }
        }
    } else {
        out = row;
        // Add B on the first `add` A positions in priority order.
        int add = roundHalfUp((n - k) * (b - 50.0) / 50.0);
        for (int idx : order) {
            if (add == 0) break;
            if (out[static_cast<size_t>(idx)] == Sample::A) {
                out[static_cast<size_t>(idx)] = Sample::B;
                --add;
            }
        }
    }
    return out;
}

std::vector<Trigger> generateRoll(const RollSpec& spec, int barUnits) {
    const int total = barUnits * kReferenceBars;
    const auto phases = planPhases(spec.type, spec.intensity);
    const auto bounds = phaseBoundaries(phases, total);

    std::vector<Trigger> triggers;
    size_t globalIndex = 0;  // used by single-row types (BURST, WAVE): the row runs through all phases
    for (size_t p = 0; p < phases.size(); ++p) {
        const int step = stepUnits(phases[p].grid);
        const auto row = blendRow(dnaRow(spec.type, phases[p].grid), spec.blend);
        size_t phaseIndex = 0;  // other types restart their row at every phase
        for (int pos = bounds[p]; pos < bounds[p + 1]; pos += step) {
            const size_t i = usesSingleRow(spec.type) ? globalIndex : phaseIndex;
            triggers.push_back({pos, phases[p].grid, row[i % row.size()]});
            ++globalIndex;
            ++phaseIndex;
        }
    }
    return triggers;
}

std::vector<Trigger> applyWindow(const std::vector<Trigger>& triggers, RollType type, double windowBars,
                                 int barUnits) {
    const int total = barUnits * kReferenceBars;
    const int window = roundHalfUp(windowBars * barUnits);
    if (window >= total) return triggers;

    std::vector<Trigger> out;
    const bool keepStart = type == RollType::Drop || type == RollType::Burst;
    const int offset = total - window;
    for (auto t : triggers) {
        if (keepStart) {
            if (t.position < window) out.push_back(t);
        } else if (t.position >= offset) {
            t.position -= offset;
            out.push_back(t);
        }
    }
    return out;
}

}  // namespace dw
