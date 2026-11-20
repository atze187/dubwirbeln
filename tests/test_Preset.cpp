#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "engine/Preset.h"
#include "engine/Version.h"

using Catch::Approx;
using nlohmann::json;
using namespace dw;

namespace {

std::string readExample(const std::string& relative) {
    std::ifstream in(std::string(DW_EXAMPLES_DIR) + "/" + relative, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

Preset parsed(const std::string& text) {
    const auto r = presetFromJson(text);
    INFO(r.error);
    REQUIRE(r.preset.has_value());
    return *r.preset;
}

json minimal() { return json::parse(readExample("valid/preset-minimal.json")); }

bool rejects(const json& doc) {
    const auto r = presetFromJson(doc.dump());
    return !r.preset.has_value() && !r.error.empty();
}

}  // namespace

TEST_CASE("the default preset has the values of spec Part 11", "[preset]") {
    const Preset p = defaultPreset(4711);
    CHECK(p.schemaVersion == 1);
    CHECK(p.engineVersion == version());
    CHECK_FALSE(p.name.empty());
    CHECK_FALSE(p.rollType.has_value());
    CHECK(p.intensity == Approx(50.0));
    CHECK(p.blend == Approx(50.0));
    CHECK(p.windowBars == Approx(4.0));
    CHECK(p.mode == PlaybackMode::Trigger);
    CHECK(p.quantize == Quantize::Off);
    CHECK(p.curveType == CurveType::Flat);
    CHECK(p.curve == curveTemplate(CurveType::Flat));
    CHECK(p.humanize == Approx(0.0));
    CHECK(p.swing == Approx(0.0));
    CHECK(p.velocityFollow == VelocityFollow::VelocityIntensity);
    CHECK(p.variationSeed == 0u);
    CHECK(p.humanizeSeed == 4711u);
    CHECK_FALSE(p.sampleA.has_value());
    CHECK_FALSE(p.sampleB.has_value());
    CHECK(p.gesture.empty());
}

TEST_CASE("valid example presets read back to the same JSON", "[preset][json]") {
    for (const char* file : {"valid/preset-minimal.json", "valid/preset-full.json"}) {
        INFO(file);
        const auto text = readExample(file);
        const Preset p = parsed(text);
        CHECK(json::parse(presetToJson(p)) == json::parse(text));
    }
}

TEST_CASE("the full example is read field by field", "[preset][json]") {
    const Preset p = parsed(readExample("valid/preset-full.json"));
    REQUIRE(p.rollType.has_value());
    CHECK(*p.rollType == RollType::Build);
    CHECK(p.intensity == Approx(78.0));
    CHECK(p.humanizeSeed == 12345u);
    CHECK(p.curveType == CurveType::Flat);
    REQUIRE(p.curve.size() == 2);
    CHECK(p.curve[1].v == Approx(100.0));
    REQUIRE(p.sampleA.has_value());
    CHECK(p.sampleA->name == "kick.wav");
    CHECK(p.sampleA->file == "samples/kick.wav");
    CHECK_FALSE(p.sampleB.has_value());
    REQUIRE(p.gesture.size() == 2);
    CHECK(p.gesture[1].x == Approx(1.0));
    CHECK(json::parse(p.metadataJson) == json::object());
}

TEST_CASE("a preset without a roll type reads as no roll", "[preset][json]") {
    CHECK_FALSE(parsed(readExample("valid/preset-minimal.json")).rollType.has_value());
}

TEST_CASE("invalid example presets are rejected with a message", "[preset][json]") {
    for (const char* name : {"preset-bad-sha256", "preset-bad-window", "preset-intensity-over-100",
                             "preset-level-over-6db", "preset-mp3-file", "preset-unknown-field",
                             "preset-unknown-roll-type"}) {
        INFO(name);
        const auto r = presetFromJson(readExample(std::string("invalid/") + name + ".json"));
        CHECK_FALSE(r.preset.has_value());
        CHECK_FALSE(r.error.empty());
    }
}

TEST_CASE("structurally broken input is rejected", "[preset][json]") {
    CHECK_FALSE(presetFromJson("not json").preset.has_value());
    CHECK_FALSE(presetFromJson("[]").preset.has_value());
    CHECK_FALSE(presetFromJson("").preset.has_value());
}

TEST_CASE("required fields are required", "[preset][json]") {
    for (const char* key : {"schemaVersion", "engineVersion", "name", "rollType", "intensity", "blend",
                            "velocityCurve", "sampleA", "sampleB", "playback", "variation", "humanize"}) {
        INFO(key);
        auto doc = minimal();
        doc.erase(key);
        CHECK(rejects(doc));
    }
}

TEST_CASE("values outside the schema limits are rejected", "[preset][json]") {
    auto doc = minimal();
    doc["schemaVersion"] = 2;
    CHECK(rejects(doc));

    doc = minimal();
    doc["name"] = "";
    CHECK(rejects(doc));

    doc = minimal();
    doc["blend"] = -1;
    CHECK(rejects(doc));

    doc = minimal();
    doc["engineVersion"] = "one";
    CHECK(rejects(doc));

    doc = minimal();
    doc["playback"]["humanize"] = 101;
    CHECK(rejects(doc));

    doc = minimal();
    doc["playback"]["quantize"] = "1/2";
    CHECK(rejects(doc));

    doc = minimal();
    doc["playback"]["mode"] = "loop";
    CHECK(rejects(doc));

    doc = minimal();
    doc["playback"]["velocityFollow"] = "intensity";
    CHECK(rejects(doc));

    doc = minimal();
    doc["variation"]["seed"] = 4294967296.0;
    CHECK(rejects(doc));

    doc = minimal();
    doc["humanize"]["seed"] = -1;
    CHECK(rejects(doc));
}

TEST_CASE("unknown keys are rejected on every level", "[preset][json]") {
    auto doc = minimal();
    doc["playback"]["extra"] = 1;
    CHECK(rejects(doc));
    doc = minimal();
    doc["variation"]["extra"] = 1;
    CHECK(rejects(doc));
    doc = minimal();
    doc["velocityCurve"]["extra"] = 1;
    CHECK(rejects(doc));
    doc = minimal();
    doc["velocityCurve"]["points"][0]["extra"] = 1;
    CHECK(rejects(doc));
}

TEST_CASE("velocity curve limits are enforced", "[preset][json]") {
    auto doc = minimal();
    doc["velocityCurve"]["points"] = json::array({{{"t", 0}, {"v", 100}}});
    CHECK(rejects(doc));

    doc = minimal();
    doc["velocityCurve"]["points"][0]["v"] = 0;
    CHECK(rejects(doc));

    doc = minimal();
    doc["velocityCurve"]["points"][0]["v"] = 100.5;
    CHECK(rejects(doc));

    doc = minimal();
    doc["velocityCurve"]["points"][1]["t"] = 1.5;
    CHECK(rejects(doc));

    doc = minimal();
    doc["velocityCurve"]["type"] = "triangle";
    CHECK(rejects(doc));

    doc = minimal();
    json many = json::array();
    for (int i = 0; i < 17; ++i) many.push_back({{"t", i / 16.0}, {"v", 100}});
    doc["velocityCurve"]["points"] = many;
    CHECK(rejects(doc));
}

TEST_CASE("curve points are sorted by position on load", "[preset][json]") {
    auto doc = minimal();
    doc["velocityCurve"]["points"] = json::array({{{"t", 1}, {"v", 90}}, {{"t", 0.5}, {"v", 60}}, {{"t", 0}, {"v", 30}}});
    const Preset p = parsed(doc.dump());
    REQUIRE(p.curve.size() == 3);
    CHECK(p.curve[0].t == Approx(0.0));
    CHECK(p.curve[0].v == Approx(30.0));
    CHECK(p.curve[2].t == Approx(1.0));
}

TEST_CASE("sample slot limits are enforced, including end > start", "[preset][json]") {
    const auto text = readExample("valid/preset-full.json");
    auto doc = json::parse(text);
    doc["sampleA"]["start"] = 0.6;
    doc["sampleA"]["end"] = 0.6;
    CHECK(rejects(doc));

    doc = json::parse(text);
    doc["sampleA"]["start"] = 0.8;
    doc["sampleA"]["end"] = 0.2;
    CHECK(rejects(doc));

    doc = json::parse(text);
    doc["sampleA"]["tuneSemitones"] = 25;
    CHECK(rejects(doc));

    doc = json::parse(text);
    doc["sampleA"]["levelDb"] = -61;
    CHECK(rejects(doc));

    doc = json::parse(text);
    doc["sampleA"]["file"] = "samples/a/b.wav";
    CHECK(rejects(doc));

    doc = json::parse(text);
    doc["sampleA"]["start"] = 0.25;
    doc["sampleA"]["end"] = 0.75;
    CHECK(parsed(doc.dump()).sampleA->start == Approx(0.25));
}

TEST_CASE("gesture limits are enforced", "[preset][json]") {
    auto doc = minimal();
    doc["gesture"] = json::array({{{"x", 0}, {"y", 0}}});
    CHECK(rejects(doc));

    doc = minimal();
    doc["gesture"] = json::array({{{"x", 0}, {"y", 0}}, {{"x", 1.2}, {"y", 0}}});
    CHECK(rejects(doc));

    doc = minimal();
    json many = json::array();
    for (int i = 0; i < 513; ++i) many.push_back({{"x", 0.5}, {"y", 0.5}});
    doc["gesture"] = many;
    CHECK(rejects(doc));
}

TEST_CASE("every enum value survives a round trip", "[preset][json]") {
    for (auto type : {RollType::Build, RollType::Drop, RollType::Rush, RollType::Burst, RollType::Wave}) {
        Preset p = defaultPreset(1);
        p.rollType = type;
        CHECK(*parsed(presetToJson(p)).rollType == type);
    }
    for (auto type : {CurveType::Flat, CurveType::Crescendo, CurveType::Decrescendo, CurveType::Spike,
                      CurveType::InvertedSpike, CurveType::Wave, CurveType::Ghost}) {
        Preset p = defaultPreset(1);
        p.curveType = type;
        p.curve = curveTemplate(type);
        const Preset back = parsed(presetToJson(p));
        CHECK(back.curveType == type);
        CHECK(back.curve == p.curve);
    }
    for (auto q : {Quantize::Off, Quantize::Sixteenth, Quantize::Eighth, Quantize::Quarter, Quantize::Bar}) {
        Preset p = defaultPreset(1);
        p.quantize = q;
        CHECK(parsed(presetToJson(p)).quantize == q);
    }
    for (auto f : {VelocityFollow::Off, VelocityFollow::Velocity, VelocityFollow::VelocityIntensity}) {
        Preset p = defaultPreset(1);
        p.velocityFollow = f;
        CHECK(parsed(presetToJson(p)).velocityFollow == f);
    }
    for (double w : {4.0, 2.0, 1.0, 0.5}) {
        Preset p = defaultPreset(1);
        p.windowBars = w;
        p.mode = PlaybackMode::Hold;
        const Preset back = parsed(presetToJson(p));
        CHECK(back.windowBars == Approx(w));
        CHECK(back.mode == PlaybackMode::Hold);
    }
}

TEST_CASE("the seeds keep their full 32 bit range", "[preset][json]") {
    Preset p = defaultPreset(4294967295u);
    p.variationSeed = 4000000000u;
    const Preset back = parsed(presetToJson(p));
    CHECK(back.humanizeSeed == 4294967295u);
    CHECK(back.variationSeed == 4000000000u);
}

TEST_CASE("the default preset is valid and stable", "[preset][json]") {
    const auto text = presetToJson(defaultPreset(99));
    CHECK(presetToJson(parsed(text)) == text);
}

TEST_CASE("toRollConfig carries every playback value", "[preset][config]") {
    Preset p = defaultPreset(99);
    p.rollType = RollType::Wave;
    p.intensity = 30.0;
    p.blend = 70.0;
    p.humanize = 40.0;
    p.swing = 20.0;
    p.mode = PlaybackMode::Hold;
    p.windowBars = 0.5;
    p.quantize = Quantize::Eighth;
    p.velocityFollow = VelocityFollow::Velocity;
    p.curve = curveTemplate(CurveType::Spike);
    const RollConfig c = toRollConfig(p);
    CHECK(c.enabled);
    CHECK(c.spec.type == RollType::Wave);
    CHECK(c.spec.intensity == Approx(30.0));
    CHECK(c.spec.blend == Approx(70.0));
    CHECK(c.expression.humanize == Approx(40.0));
    CHECK(c.expression.swing == Approx(20.0));
    CHECK(c.expression.humanizeSeed == 99u);
    CHECK(c.expression.curve == curveTemplate(CurveType::Spike));
    CHECK(c.playback.mode == PlaybackMode::Hold);
    CHECK(c.playback.windowBars == Approx(0.5));
    CHECK(c.playback.quantize == Quantize::Eighth);
    CHECK(c.playback.follow == VelocityFollow::Velocity);
}

TEST_CASE("a preset without a roll type gives a disabled config", "[preset][config]") {
    CHECK_FALSE(toRollConfig(defaultPreset(1)).enabled);
}

TEST_CASE("parameter values reflect the preset", "[preset][parameters]") {
    Preset p = defaultPreset(1);
    p.rollType = RollType::Rush;
    p.intensity = 61.0;
    p.blend = 12.0;
    p.windowBars = 1.0;
    p.mode = PlaybackMode::Hold;
    p.quantize = Quantize::Quarter;
    p.humanize = 33.0;
    p.swing = 44.0;
    p.velocityFollow = VelocityFollow::Off;
    const ParameterValues v = toParameterValues(p);
    CHECK(v.rollType == 3);
    CHECK(v.intensity == Approx(61.0));
    CHECK(v.blend == Approx(12.0));
    CHECK(v.window == 2);
    CHECK(v.mode == 1);
    CHECK(v.quantize == 3);
    CHECK(v.humanize == Approx(33.0));
    CHECK(v.swing == Approx(44.0));
    CHECK(v.velocityFollow == 0);
}

TEST_CASE("the default preset maps to the default parameter values", "[preset][parameters]") {
    CHECK(toParameterValues(defaultPreset(1)) == ParameterValues{});
}

TEST_CASE("applying parameter values changes only the scalar fields", "[preset][parameters]") {
    Preset p = defaultPreset(55);
    p.name = "Keep me";
    p.variationSeed = 8;
    p.curve = curveTemplate(CurveType::Ghost);
    p.curveType = CurveType::Ghost;
    ParameterValues v;
    v.rollType = 5;
    v.intensity = 80.0;
    v.window = 3;
    v.quantize = 4;
    v.mode = 1;
    v.velocityFollow = 1;
    applyParameterValues(p, v);
    CHECK(*p.rollType == RollType::Wave);
    CHECK(p.intensity == Approx(80.0));
    CHECK(p.windowBars == Approx(0.5));
    CHECK(p.quantize == Quantize::Bar);
    CHECK(p.mode == PlaybackMode::Hold);
    CHECK(p.velocityFollow == VelocityFollow::Velocity);
    CHECK(p.name == "Keep me");
    CHECK(p.variationSeed == 8u);
    CHECK(p.humanizeSeed == 55u);
    CHECK(p.curveType == CurveType::Ghost);
    v.rollType = 0;
    applyParameterValues(p, v);
    CHECK_FALSE(p.rollType.has_value());
}

TEST_CASE("parameter values round-trip through a preset", "[preset][parameters]") {
    ParameterValues v;
    v.rollType = 2;
    v.intensity = 17.5;
    v.blend = 99.0;
    v.window = 1;
    v.mode = 1;
    v.quantize = 2;
    v.humanize = 5.0;
    v.swing = 100.0;
    v.velocityFollow = 0;
    Preset p = defaultPreset(1);
    applyParameterValues(p, v);
    CHECK(toParameterValues(p) == v);
}

TEST_CASE("out-of-range parameter indices are clamped", "[preset][parameters]") {
    Preset p = defaultPreset(1);
    ParameterValues v;
    v.rollType = 99;
    v.window = -4;
    v.mode = 7;
    v.quantize = 12;
    v.velocityFollow = -1;
    v.intensity = 140.0;
    v.blend = -5.0;
    applyParameterValues(p, v);
    CHECK(*p.rollType == RollType::Wave);
    CHECK(p.windowBars == Approx(4.0));
    CHECK(p.mode == PlaybackMode::Hold);
    CHECK(p.quantize == Quantize::Bar);
    CHECK(p.velocityFollow == VelocityFollow::Off);
    CHECK(p.intensity == Approx(100.0));
    CHECK(p.blend == Approx(0.0));
}

// ---- presetsEquivalent (dirty tracking) ----

namespace {

Preset richPreset() {
    Preset p = defaultPreset(4711);
    p.name = "Rich";
    p.rollType = RollType::Rush;
    p.intensity = 78.3;
    p.blend = 41.7;
    p.gesture = {{0.0, 0.0}, {0.5, 0.25}, {1.0, 1.0}};
    p.curveType = CurveType::Ghost;
    p.curve = curveTemplate(CurveType::Ghost);
    p.sampleA = SampleSlot{"kick.wav", std::string(64, 'a'), "samples/kick.wav", 2.0, 0.1, 0.9, -3.0};
    p.sampleB = SampleSlot{"snare.wav", std::string(64, 'b'), "samples/snare.wav", 0.0, 0.0, 1.0, 0.0};
    p.mode = PlaybackMode::Hold;
    p.windowBars = 2.0;
    p.quantize = Quantize::Eighth;
    p.humanize = 33.3;
    p.swing = 12.5;
    p.velocityFollow = VelocityFollow::Velocity;
    p.variationSeed = 99;
    p.metadataJson = R"({"tag":"x"})";
    return p;
}

}  // namespace

TEST_CASE("a preset is equivalent to itself and to a copy", "[preset][dirty]") {
    const Preset a = richPreset();
    CHECK(presetsEquivalent(a, a));
    Preset b = a;
    CHECK(presetsEquivalent(a, b));
}

TEST_CASE("float noise from host parameters does not make a preset different", "[preset][dirty]") {
    const Preset a = richPreset();
    Preset b = a;
    b.intensity = static_cast<double>(static_cast<float>(a.intensity));  // 78.30000305
    b.blend = static_cast<double>(static_cast<float>(a.blend));
    b.humanize = static_cast<double>(static_cast<float>(a.humanize));
    b.swing = static_cast<double>(static_cast<float>(a.swing));
    CHECK(b.intensity != a.intensity);
    CHECK(presetsEquivalent(a, b));
    b.curve[3].t += 1e-7;
    b.sampleA->levelDb += 1e-7;
    CHECK(presetsEquivalent(a, b));
}

TEST_CASE("the schema and engine versions are not compared", "[preset][dirty]") {
    const Preset a = richPreset();
    Preset b = a;
    b.engineVersion = "9.9.9";
    b.schemaVersion = 2;
    CHECK(presetsEquivalent(a, b));
}

TEST_CASE("every real change makes a preset different", "[preset][dirty]") {
    const Preset a = richPreset();
    const std::pair<const char*, void (*)(Preset&)> changes[] = {
        {"name", [](Preset& p) { p.name = "Other"; }},
        {"roll type", [](Preset& p) { p.rollType = RollType::Wave; }},
        {"no roll", [](Preset& p) { p.rollType.reset(); }},
        {"intensity", [](Preset& p) { p.intensity += 0.1; }},
        {"blend", [](Preset& p) { p.blend -= 0.1; }},
        {"gesture point", [](Preset& p) { p.gesture[1].x = 0.6; }},
        {"gesture removed", [](Preset& p) { p.gesture.clear(); }},
        {"curve type", [](Preset& p) { p.curveType = CurveType::Spike; }},
        {"curve value", [](Preset& p) { p.curve[2].v += 1.0; }},
        {"curve length", [](Preset& p) { p.curve.pop_back(); }},
        {"sample A tune", [](Preset& p) { p.sampleA->tuneSemitones = 3.0; }},
        {"sample A hash", [](Preset& p) { p.sampleA->sha256 = std::string(64, 'c'); }},
        {"sample A name", [](Preset& p) { p.sampleA->name = "other.wav"; }},
        {"sample A start", [](Preset& p) { p.sampleA->start = 0.2; }},
        {"sample B removed", [](Preset& p) { p.sampleB.reset(); }},
        {"sample B added", [](Preset& p) { p.sampleA.reset(); }},
        {"mode", [](Preset& p) { p.mode = PlaybackMode::Trigger; }},
        {"window", [](Preset& p) { p.windowBars = 1.0; }},
        {"quantize", [](Preset& p) { p.quantize = Quantize::Bar; }},
        {"humanize", [](Preset& p) { p.humanize = 40.0; }},
        {"swing", [](Preset& p) { p.swing = 0.0; }},
        {"velocity follow", [](Preset& p) { p.velocityFollow = VelocityFollow::Off; }},
        {"variation seed", [](Preset& p) { p.variationSeed = 100; }},
        {"humanize seed", [](Preset& p) { p.humanizeSeed = 4712; }},
        {"metadata", [](Preset& p) { p.metadataJson = R"({"tag":"y"})"; }},
    };
    for (const auto& [what, change] : changes) {
        INFO(what);
        Preset b = a;
        change(b);
        CHECK_FALSE(presetsEquivalent(a, b));
        CHECK_FALSE(presetsEquivalent(b, a));
    }
}
