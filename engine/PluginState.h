#pragma once

#include <optional>
#include <string>

#include "engine/Preset.h"

namespace dw {

constexpr int kPluginStateVersion = 1;
constexpr int kDefaultTriggerNote = 36;  // C1 (spec Part 9)

// What the plugin instance saves with the DAW project: the preset (schema format) plus the trigger
// note, which belongs to the instance and is never part of a preset (spec Part 9).
struct PluginState {
    int triggerNote = kDefaultTriggerNote;
    Preset preset;
};

std::string pluginStateToJson(const PluginState& state);

struct PluginStateParseResult {
    std::optional<PluginState> state;
    std::string error;
};
PluginStateParseResult pluginStateFromJson(const std::string& json);

}  // namespace dw
