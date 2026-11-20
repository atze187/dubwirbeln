#include "engine/PluginState.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace dw {

using json = nlohmann::json;

std::string pluginStateToJson(const PluginState& state) {
    json doc = {{"stateVersion", kPluginStateVersion},
                {"triggerNote", state.triggerNote},
                {"preset", json::parse(presetToJson(state.preset))}};
    return doc.dump(2);
}

PluginStateParseResult pluginStateFromJson(const std::string& text) {
    const json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) return {std::nullopt, "state is not a JSON object"};

    const auto version = doc.find("stateVersion");
    if (version == doc.end() || !version->is_number_integer()) return {std::nullopt, "stateVersion: required"};
    if (version->get<int>() < 1 || version->get<int>() > kPluginStateVersion)
        return {std::nullopt, "stateVersion: not supported"};

    const auto note = doc.find("triggerNote");
    if (note == doc.end() || !note->is_number_integer()) return {std::nullopt, "triggerNote: required"};
    const auto noteValue = note->get<long long>();
    if (noteValue < 0 || noteValue > 127) return {std::nullopt, "triggerNote: out of range"};

    const auto preset = doc.find("preset");
    if (preset == doc.end()) return {std::nullopt, "preset: required"};
    auto parsed = presetFromJson(preset->dump());
    if (!parsed.preset) return {std::nullopt, "preset." + parsed.error};

    PluginState state;
    state.triggerNote = static_cast<int>(noteValue);
    state.preset = std::move(*parsed.preset);
    return {std::move(state), {}};
}

}  // namespace dw
