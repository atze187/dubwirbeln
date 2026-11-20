#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/RollScheduler.h"
#include "engine/VelocityCurve.h"

namespace dw {

constexpr int kPresetSchemaVersion = 1;

// A sample slot as the preset stores it (spec Part 11). The sample data itself lives in the container.
struct SampleSlot {
    std::string name;
    std::string sha256;
    std::string file;
    double tuneSemitones = 0.0;
    double start = 0.0;
    double end = 1.0;
    double levelDb = 0.0;
    bool operator==(const SampleSlot&) const = default;
};

struct GesturePoint {
    double x = 0.0;
    double y = 0.0;
    bool operator==(const GesturePoint&) const = default;
};

// One preset = one roll (spec Part 11, schemas/preset.schema.json). The trigger list is never stored.
struct Preset {
    int schemaVersion = kPresetSchemaVersion;
    std::string engineVersion;
    std::string name = "Init";

    std::optional<RollType> rollType;  // none = silent until a gesture is drawn
    double intensity = 50.0;
    double blend = 50.0;
    std::vector<GesturePoint> gesture;  // empty = no original gesture

    CurveType curveType = CurveType::Flat;
    std::vector<CurvePoint> curve;

    std::optional<SampleSlot> sampleA;
    std::optional<SampleSlot> sampleB;

    PlaybackMode mode = PlaybackMode::Trigger;
    double windowBars = 4.0;
    Quantize quantize = Quantize::Off;
    double humanize = 0.0;
    double swing = 0.0;
    VelocityFollow velocityFollow = VelocityFollow::VelocityIntensity;

    std::uint32_t variationSeed = 0;
    std::uint32_t humanizeSeed = 0;

    std::string metadataJson;  // reserved (preview, NKS): kept as it was read, never interpreted
};

// A new preset with the defaults of spec Part 11. The humanize seed is assigned once here.
Preset defaultPreset(std::uint32_t humanizeSeed);

struct PresetParseResult {
    std::optional<Preset> preset;
    std::string error;  // set when `preset` is empty
};

std::string presetToJson(const Preset& preset);
PresetParseResult presetFromJson(const std::string& json);

// True when two presets sound and save the same, up to float noise: host parameters make doubles
// like 78.3 come back as 78.30000305. The schema and engine versions are not compared. This is
// what "dirty" means: the current preset is not equivalent to the one that was saved or loaded.
bool presetsEquivalent(const Preset& a, const Preset& b);

// What the roll scheduler needs. A preset without a roll type gives a disabled (silent) config.
RollConfig toRollConfig(const Preset& preset);

// The scalar preset values as host parameters. Choice values are indices:
//   rollType: 0 None, 1 Build, 2 Drop, 3 Rush, 4 Burst, 5 Wave
//   window: 0 = 4 bars, 1 = 2, 2 = 1, 3 = 1/2      mode: 0 Trigger, 1 Hold
//   quantize: 0 Off, 1 1/16, 2 1/8, 3 1/4, 4 1 Bar  velocityFollow: 0 Off, 1 Velocity, 2 Velocity + Intensity
struct ParameterValues {
    int rollType = 0;
    double intensity = 50.0;
    double blend = 50.0;
    int window = 0;
    int mode = 0;
    int quantize = 0;
    double humanize = 0.0;
    double swing = 0.0;
    int velocityFollow = 2;
    bool operator==(const ParameterValues&) const = default;
};

ParameterValues toParameterValues(const Preset& preset);
void applyParameterValues(Preset& preset, const ParameterValues& values);

// The same on a roll config (no allocation): toRollConfig(applyParameterValues(p, v)) equals
// applyParameterValues(toRollConfig(p), v). The audio thread uses this to follow host automation.
void applyParameterValues(RollConfig& config, const ParameterValues& values);

}  // namespace dw
