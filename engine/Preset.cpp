#include "engine/Preset.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <regex>

#include <nlohmann/json.hpp>

#include "engine/Version.h"

namespace dw {

namespace {

using json = nlohmann::json;

struct ParseError {
    std::string message;
};

[[noreturn]] void fail(const std::string& path, const std::string& what) { throw ParseError{path + ": " + what}; }

std::string child(const std::string& path, const char* key) { return path.empty() ? key : path + "." + key; }

const json& requireKey(const json& obj, const char* key, const std::string& path) {
    const auto it = obj.find(key);
    if (it == obj.end()) fail(child(path, key), "required");
    return *it;
}

void requireObject(const json& v, const std::string& path) {
    if (!v.is_object()) fail(path.empty() ? "preset" : path, "must be an object");
}

void onlyKeys(const json& obj, std::initializer_list<const char*> allowed, const std::string& path) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&](const char* k) { return it.key() == k; });
        if (!known) fail(child(path, it.key().c_str()), "unknown field");
    }
}

double numberIn(const json& v, const std::string& path, double lo, double hi) {
    if (!v.is_number()) fail(path, "must be a number");
    const double d = v.get<double>();
    if (!(d >= lo && d <= hi)) fail(path, "out of range");
    return d;
}

std::int64_t integerIn(const json& v, const std::string& path, std::int64_t lo, std::int64_t hi) {
    if (!v.is_number()) fail(path, "must be an integer");
    const double d = v.get<double>();
    if (d != std::floor(d)) fail(path, "must be an integer");
    if (!(d >= static_cast<double>(lo) && d <= static_cast<double>(hi))) fail(path, "out of range");
    return v.is_number_float() ? static_cast<std::int64_t>(d) : v.get<std::int64_t>();
}

std::string stringValue(const json& v, const std::string& path) {
    if (!v.is_string()) fail(path, "must be a string");
    return v.get<std::string>();
}

template <typename T, size_t N>
T oneOf(const json& v, const std::string& path, const std::pair<const char*, T> (&table)[N]) {
    if (!v.is_string()) fail(path, "must be a string");
    const auto s = v.get<std::string>();
    for (const auto& entry : table)
        if (s == entry.first) return entry.second;
    fail(path, "unknown value '" + s + "'");
}

const std::pair<const char*, RollType> kRollTypes[] = {{"build", RollType::Build},
                                                        {"drop", RollType::Drop},
                                                        {"rush", RollType::Rush},
                                                        {"burst", RollType::Burst},
                                                        {"wave", RollType::Wave}};
const std::pair<const char*, CurveType> kCurveTypes[] = {{"flat", CurveType::Flat},
                                                          {"crescendo", CurveType::Crescendo},
                                                          {"decrescendo", CurveType::Decrescendo},
                                                          {"spike", CurveType::Spike},
                                                          {"inverted_spike", CurveType::InvertedSpike},
                                                          {"wave", CurveType::Wave},
                                                          {"ghost", CurveType::Ghost}};
const std::pair<const char*, PlaybackMode> kModes[] = {{"trigger", PlaybackMode::Trigger},
                                                         {"hold", PlaybackMode::Hold}};
const std::pair<const char*, Quantize> kQuantizes[] = {{"off", Quantize::Off},
                                                        {"1/16", Quantize::Sixteenth},
                                                        {"1/8", Quantize::Eighth},
                                                        {"1/4", Quantize::Quarter},
                                                        {"1bar", Quantize::Bar}};
const std::pair<const char*, VelocityFollow> kFollows[] = {{"off", VelocityFollow::Off},
                                                            {"velocity", VelocityFollow::Velocity},
                                                            {"velocity_intensity", VelocityFollow::VelocityIntensity}};
constexpr double kWindows[] = {4.0, 2.0, 1.0, 0.5};

template <typename T, size_t N>
const char* nameOf(T value, const std::pair<const char*, T> (&table)[N]) {
    for (const auto& entry : table)
        if (entry.second == value) return entry.first;
    return table[0].first;
}

// Integral values are written as integers (4, not 4.0), as in the schema's examples.
json numberJson(double x) {
    if (x == std::floor(x) && std::fabs(x) < 1e15) return json(static_cast<std::int64_t>(x));
    return json(x);
}

SampleSlot parseSlot(const json& v, const std::string& path) {
    requireObject(v, path);
    onlyKeys(v, {"name", "sha256", "file", "tuneSemitones", "start", "end", "levelDb"}, path);
    SampleSlot s;
    s.name = stringValue(requireKey(v, "name", path), child(path, "name"));
    if (s.name.empty()) fail(child(path, "name"), "must not be empty");
    s.sha256 = stringValue(requireKey(v, "sha256", path), child(path, "sha256"));
    if (!std::regex_match(s.sha256, std::regex("^[0-9a-f]{64}$"))) fail(child(path, "sha256"), "must be 64 lowercase hex digits");
    s.file = stringValue(requireKey(v, "file", path), child(path, "file"));
    if (!std::regex_match(s.file, std::regex(R"(^samples/[^/\\]+\.(wav|flac)$)")))
        fail(child(path, "file"), "must be samples/<name>.wav or .flac");
    s.tuneSemitones = numberIn(requireKey(v, "tuneSemitones", path), child(path, "tuneSemitones"), -24.0, 24.0);
    s.start = numberIn(requireKey(v, "start", path), child(path, "start"), 0.0, 1.0);
    s.end = numberIn(requireKey(v, "end", path), child(path, "end"), 0.0, 1.0);
    if (!(s.end > s.start)) fail(child(path, "end"), "must be greater than start");
    s.levelDb = numberIn(requireKey(v, "levelDb", path), child(path, "levelDb"), -60.0, 6.0);
    return s;
}

std::optional<SampleSlot> parseOptionalSlot(const json& v, const std::string& path) {
    if (v.is_null()) return std::nullopt;
    return parseSlot(v, path);
}

json slotJson(const std::optional<SampleSlot>& slot) {
    if (!slot) return nullptr;
    return {{"name", slot->name},
            {"sha256", slot->sha256},
            {"file", slot->file},
            {"tuneSemitones", numberJson(slot->tuneSemitones)},
            {"start", numberJson(slot->start)},
            {"end", numberJson(slot->end)},
            {"levelDb", numberJson(slot->levelDb)}};
}

Preset parsePreset(const json& doc) {
    requireObject(doc, "");
    onlyKeys(doc,
             {"schemaVersion", "engineVersion", "name", "rollType", "intensity", "blend", "gesture", "velocityCurve",
              "sampleA", "sampleB", "playback", "variation", "humanize", "metadata"},
             "");
    Preset p;

    p.schemaVersion = static_cast<int>(integerIn(requireKey(doc, "schemaVersion", ""), "schemaVersion", 1, 1000000));
    if (p.schemaVersion > kPresetSchemaVersion) fail("schemaVersion", "newer than this version supports");

    p.engineVersion = stringValue(requireKey(doc, "engineVersion", ""), "engineVersion");
    if (!std::regex_match(p.engineVersion, std::regex(R"(^[0-9]+\.[0-9]+\.[0-9]+$)")))
        fail("engineVersion", "must be a semantic version");

    p.name = stringValue(requireKey(doc, "name", ""), "name");
    if (p.name.empty() || p.name.size() > 128) fail("name", "must be 1 to 128 characters");

    const json& roll = requireKey(doc, "rollType", "");
    if (roll.is_null()) p.rollType.reset();
    else p.rollType = oneOf(roll, "rollType", kRollTypes);

    p.intensity = numberIn(requireKey(doc, "intensity", ""), "intensity", 0.0, 100.0);
    p.blend = numberIn(requireKey(doc, "blend", ""), "blend", 0.0, 100.0);

    if (const auto it = doc.find("gesture"); it != doc.end()) {
        if (!it->is_array() || it->size() < 2 || it->size() > 512) fail("gesture", "must have 2 to 512 points");
        for (const auto& pt : *it) {
            requireObject(pt, "gesture[]");
            onlyKeys(pt, {"x", "y"}, "gesture[]");
            p.gesture.push_back({numberIn(requireKey(pt, "x", "gesture[]"), "gesture[].x", 0.0, 1.0),
                                 numberIn(requireKey(pt, "y", "gesture[]"), "gesture[].y", 0.0, 1.0)});
        }
    }

    const json& curve = requireKey(doc, "velocityCurve", "");
    requireObject(curve, "velocityCurve");
    onlyKeys(curve, {"type", "points"}, "velocityCurve");
    p.curveType = oneOf(requireKey(curve, "type", "velocityCurve"), "velocityCurve.type", kCurveTypes);
    const json& points = requireKey(curve, "points", "velocityCurve");
    if (!points.is_array() || points.size() < 2 || points.size() > 16) fail("velocityCurve.points", "must have 2 to 16 points");
    for (const auto& pt : points) {
        requireObject(pt, "velocityCurve.points[]");
        onlyKeys(pt, {"t", "v"}, "velocityCurve.points[]");
        p.curve.push_back({numberIn(requireKey(pt, "t", "velocityCurve.points[]"), "velocityCurve.points[].t", 0.0, 1.0),
                           static_cast<double>(integerIn(requireKey(pt, "v", "velocityCurve.points[]"),
                                                         "velocityCurve.points[].v", 1, 127))});
    }
    std::stable_sort(p.curve.begin(), p.curve.end(), [](const CurvePoint& a, const CurvePoint& b) { return a.t < b.t; });

    p.sampleA = parseOptionalSlot(requireKey(doc, "sampleA", ""), "sampleA");
    p.sampleB = parseOptionalSlot(requireKey(doc, "sampleB", ""), "sampleB");

    const json& pb = requireKey(doc, "playback", "");
    requireObject(pb, "playback");
    onlyKeys(pb, {"mode", "windowBars", "quantize", "humanize", "swing", "velocityFollow"}, "playback");
    p.mode = oneOf(requireKey(pb, "mode", "playback"), "playback.mode", kModes);
    p.windowBars = numberIn(requireKey(pb, "windowBars", "playback"), "playback.windowBars", 0.0, 4.0);
    if (std::none_of(std::begin(kWindows), std::end(kWindows), [&](double w) { return w == p.windowBars; }))
        fail("playback.windowBars", "must be 4, 2, 1 or 0.5");
    p.quantize = oneOf(requireKey(pb, "quantize", "playback"), "playback.quantize", kQuantizes);
    p.humanize = numberIn(requireKey(pb, "humanize", "playback"), "playback.humanize", 0.0, 100.0);
    p.swing = numberIn(requireKey(pb, "swing", "playback"), "playback.swing", 0.0, 100.0);
    p.velocityFollow = oneOf(requireKey(pb, "velocityFollow", "playback"), "playback.velocityFollow", kFollows);

    for (const char* key : {"variation", "humanize"}) {
        const json& o = requireKey(doc, key, "");
        requireObject(o, key);
        onlyKeys(o, {"seed"}, key);
        const auto seed = static_cast<std::uint32_t>(
            integerIn(requireKey(o, "seed", key), child(key, "seed"), 0, 4294967295ll));
        (std::string(key) == "variation" ? p.variationSeed : p.humanizeSeed) = seed;
    }

    if (const auto it = doc.find("metadata"); it != doc.end()) {
        requireObject(*it, "metadata");
        p.metadataJson = it->dump();
    }
    return p;
}

const char* const kRollTypeChoices[] = {nullptr, "build", "drop", "rush", "burst", "wave"};

}  // namespace

Preset defaultPreset(std::uint32_t humanizeSeed) {
    Preset p;
    p.engineVersion = version();
    p.curve = curveTemplate(CurveType::Flat);
    p.humanizeSeed = humanizeSeed;
    return p;
}

std::string presetToJson(const Preset& p) {
    json points = json::array();
    for (const auto& pt : p.curve)
        points.push_back({{"t", numberJson(pt.t)}, {"v", static_cast<std::int64_t>(std::lround(pt.v))}});

    json doc = {
        {"schemaVersion", p.schemaVersion},
        {"engineVersion", p.engineVersion},
        {"name", p.name},
        {"rollType", p.rollType ? json(kRollTypeChoices[static_cast<int>(*p.rollType) + 1]) : json(nullptr)},
        {"intensity", numberJson(p.intensity)},
        {"blend", numberJson(p.blend)},
        {"velocityCurve", {{"type", nameOf(p.curveType, kCurveTypes)}, {"points", points}}},
        {"sampleA", slotJson(p.sampleA)},
        {"sampleB", slotJson(p.sampleB)},
        {"playback",
         {{"mode", nameOf(p.mode, kModes)},
          {"windowBars", numberJson(p.windowBars)},
          {"quantize", nameOf(p.quantize, kQuantizes)},
          {"humanize", numberJson(p.humanize)},
          {"swing", numberJson(p.swing)},
          {"velocityFollow", nameOf(p.velocityFollow, kFollows)}}},
        {"variation", {{"seed", p.variationSeed}}},
        {"humanize", {{"seed", p.humanizeSeed}}},
    };
    if (!p.gesture.empty()) {
        json g = json::array();
        for (const auto& pt : p.gesture) g.push_back({{"x", numberJson(pt.x)}, {"y", numberJson(pt.y)}});
        doc["gesture"] = g;
    }
    if (!p.metadataJson.empty()) {
        const json meta = json::parse(p.metadataJson, nullptr, false);
        if (meta.is_object()) doc["metadata"] = meta;
    }
    return doc.dump(2);
}

PresetParseResult presetFromJson(const std::string& text) {
    try {
        const json doc = json::parse(text);
        return {parsePreset(doc), {}};
    } catch (const ParseError& e) {
        return {std::nullopt, e.message};
    } catch (const json::exception& e) {
        return {std::nullopt, std::string("invalid JSON: ") + e.what()};
    }
}

RollConfig toRollConfig(const Preset& p) {
    RollConfig c;
    c.enabled = p.rollType.has_value();
    c.spec = {p.rollType.value_or(RollType::Build), p.intensity, p.blend, p.variationSeed};
    c.expression.curve = p.curve;
    c.expression.humanize = p.humanize;
    c.expression.swing = p.swing;
    c.expression.humanizeSeed = p.humanizeSeed;
    c.playback.mode = p.mode;
    c.playback.windowBars = p.windowBars;
    c.playback.quantize = p.quantize;
    c.playback.follow = p.velocityFollow;
    return c;
}

ParameterValues toParameterValues(const Preset& p) {
    ParameterValues v;
    v.rollType = p.rollType ? static_cast<int>(*p.rollType) + 1 : 0;
    v.intensity = p.intensity;
    v.blend = p.blend;
    const auto w = std::find(std::begin(kWindows), std::end(kWindows), p.windowBars);
    v.window = w == std::end(kWindows) ? 0 : static_cast<int>(w - std::begin(kWindows));
    v.mode = static_cast<int>(p.mode);
    v.quantize = static_cast<int>(p.quantize);
    v.humanize = p.humanize;
    v.swing = p.swing;
    v.velocityFollow = static_cast<int>(p.velocityFollow);
    return v;
}

void applyParameterValues(Preset& p, const ParameterValues& v) {
    const int roll = std::clamp(v.rollType, 0, 5);
    if (roll == 0) p.rollType.reset();
    else p.rollType = static_cast<RollType>(roll - 1);
    p.intensity = std::clamp(v.intensity, 0.0, 100.0);
    p.blend = std::clamp(v.blend, 0.0, 100.0);
    p.windowBars = kWindows[std::clamp(v.window, 0, 3)];
    p.mode = static_cast<PlaybackMode>(std::clamp(v.mode, 0, 1));
    p.quantize = static_cast<Quantize>(std::clamp(v.quantize, 0, 4));
    p.humanize = std::clamp(v.humanize, 0.0, 100.0);
    p.swing = std::clamp(v.swing, 0.0, 100.0);
    p.velocityFollow = static_cast<VelocityFollow>(std::clamp(v.velocityFollow, 0, 2));
}

void applyParameterValues(RollConfig& c, const ParameterValues& v) {
    const int roll = std::clamp(v.rollType, 0, 5);
    c.enabled = roll != 0;
    c.spec.type = roll == 0 ? RollType::Build : static_cast<RollType>(roll - 1);
    c.spec.intensity = std::clamp(v.intensity, 0.0, 100.0);
    c.spec.blend = std::clamp(v.blend, 0.0, 100.0);
    c.expression.humanize = std::clamp(v.humanize, 0.0, 100.0);
    c.expression.swing = std::clamp(v.swing, 0.0, 100.0);
    c.playback.windowBars = kWindows[std::clamp(v.window, 0, 3)];
    c.playback.mode = static_cast<PlaybackMode>(std::clamp(v.mode, 0, 1));
    c.playback.quantize = static_cast<Quantize>(std::clamp(v.quantize, 0, 4));
    c.playback.follow = static_cast<VelocityFollow>(std::clamp(v.velocityFollow, 0, 2));
}

}  // namespace dw
