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

ArchiveWriteResult pluginStateToBytes(const PluginState& state, const FileMap& samples) {
    PresetArchive archive;
    archive.preset = state.preset;
    archive.samples = samples;
    const std::string instance =
        json{{"stateVersion", kPluginStateVersion}, {"triggerNote", state.triggerNote}}.dump();
    archive.extras["instance.json"] = Bytes(instance.begin(), instance.end());
    if (state.baseline) {
        const std::string baseline = presetToJson(*state.baseline);
        archive.extras["baseline.json"] = Bytes(baseline.begin(), baseline.end());
    }
    return writePresetArchive(archive);
}

PluginStateBytesResult pluginStateFromBytes(const Bytes& bytes) {
    const bool isZip = bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == 'K';
    if (!isZip) {  // states saved as plain JSON by older versions
        auto legacy = pluginStateFromJson(std::string(bytes.begin(), bytes.end()));
        return {std::move(legacy.state), {}, legacy.error};
    }

    auto read = readPresetArchive(bytes);
    if (!read.archive) return {std::nullopt, {}, read.error};

    const auto it = read.archive->extras.find("instance.json");
    if (it == read.archive->extras.end()) return {std::nullopt, {}, "instance.json: required"};
    const json instance = json::parse(it->second.begin(), it->second.end(), nullptr, false);
    if (!instance.is_object()) return {std::nullopt, {}, "instance.json: not a JSON object"};

    const auto version = instance.find("stateVersion");
    if (version == instance.end() || !version->is_number_integer() || version->get<int>() < 1 ||
        version->get<int>() > kPluginStateVersion)
        return {std::nullopt, {}, "instance.json: stateVersion not supported"};
    const auto note = instance.find("triggerNote");
    if (note == instance.end() || !note->is_number_integer() || note->get<long long>() < 0 ||
        note->get<long long>() > 127)
        return {std::nullopt, {}, "instance.json: triggerNote out of range"};

    PluginState state;
    state.triggerNote = note->get<int>();
    state.preset = std::move(read.archive->preset);
    // The baseline is optional and only used for dirty tracking: a damaged one is ignored.
    if (const auto b = read.archive->extras.find("baseline.json"); b != read.archive->extras.end()) {
        auto parsed = presetFromJson(std::string(b->second.begin(), b->second.end()));
        if (parsed.preset) state.baseline = std::move(parsed.preset);
    }
    return {std::move(state), std::move(read.archive->samples), {}};
}

}  // namespace dw
