#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "engine/PluginState.h"
#include "engine/Sha256.h"

using Catch::Approx;
using nlohmann::json;
using namespace dw;

namespace {

PluginState sampleState() {
    PluginState s;
    s.triggerNote = 60;
    s.preset = defaultPreset(777);
    s.preset.name = "State test";
    s.preset.rollType = RollType::Drop;
    s.preset.intensity = 64.0;
    s.preset.blend = 20.0;
    s.preset.mode = PlaybackMode::Hold;
    s.preset.curveType = CurveType::Spike;
    s.preset.curve = curveTemplate(CurveType::Spike);
    s.preset.variationSeed = 31337;
    return s;
}

}  // namespace

TEST_CASE("a default state uses C1 as trigger note", "[state]") {
    CHECK(PluginState{}.triggerNote == 36);
}

TEST_CASE("the plugin state round-trips", "[state]") {
    const PluginState s = sampleState();
    const auto r = pluginStateFromJson(pluginStateToJson(s));
    INFO(r.error);
    REQUIRE(r.state.has_value());
    CHECK(r.state->triggerNote == 60);
    CHECK(r.state->preset.name == "State test");
    REQUIRE(r.state->preset.rollType.has_value());
    CHECK(*r.state->preset.rollType == RollType::Drop);
    CHECK(r.state->preset.intensity == Approx(64.0));
    CHECK(r.state->preset.mode == PlaybackMode::Hold);
    CHECK(r.state->preset.curve == curveTemplate(CurveType::Spike));
    CHECK(r.state->preset.variationSeed == 31337u);
    CHECK(r.state->preset.humanizeSeed == 777u);
}

TEST_CASE("the trigger note is not part of the preset", "[state]") {
    const auto doc = json::parse(pluginStateToJson(sampleState()));
    REQUIRE(doc.contains("preset"));
    CHECK_FALSE(doc["preset"].contains("triggerNote"));
    CHECK(doc["triggerNote"] == 60);
    CHECK(doc["stateVersion"] == 1);
    // the stored preset is a valid preset document on its own
    CHECK(presetFromJson(doc["preset"].dump()).preset.has_value());
}

TEST_CASE("broken or unsupported states are rejected", "[state]") {
    CHECK_FALSE(pluginStateFromJson("").state.has_value());
    CHECK_FALSE(pluginStateFromJson("garbage").state.has_value());
    CHECK_FALSE(pluginStateFromJson("{}").state.has_value());

    auto doc = json::parse(pluginStateToJson(sampleState()));
    auto bad = doc;
    bad["stateVersion"] = 2;
    CHECK_FALSE(pluginStateFromJson(bad.dump()).state.has_value());

    bad = doc;
    bad["triggerNote"] = 128;
    CHECK_FALSE(pluginStateFromJson(bad.dump()).state.has_value());

    bad = doc;
    bad["triggerNote"] = -1;
    CHECK_FALSE(pluginStateFromJson(bad.dump()).state.has_value());

    bad = doc;
    bad["preset"]["intensity"] = 500;
    const auto r = pluginStateFromJson(bad.dump());
    CHECK_FALSE(r.state.has_value());
    CHECK_FALSE(r.error.empty());

    bad = doc;
    bad.erase("preset");
    CHECK_FALSE(pluginStateFromJson(bad.dump()).state.has_value());
}

// ---- binary state (archive) ----

namespace {

Bytes pseudoBytes(size_t n, std::uint32_t seed) {
    Bytes b(n);
    std::uint32_t x = seed;
    for (auto& v : b) {
        x = x * 1664525u + 1013904223u;
        v = static_cast<std::uint8_t>(x >> 24);
    }
    return b;
}

}  // namespace

TEST_CASE("the binary state keeps the preset, the trigger note and the samples", "[state][bytes]") {
    PluginState s = sampleState();
    const Bytes kick = pseudoBytes(3000, 1);
    s.preset.sampleA = SampleSlot{"kick.wav", sha256Hex(kick), "samples/kick.wav", 3.0, 0.1, 0.9, -6.0};
    FileMap files{{"samples/kick.wav", kick}};

    const auto written = pluginStateToBytes(s, files);
    INFO(written.error);
    REQUIRE(written.ok());

    const auto r = pluginStateFromBytes(written.bytes);
    INFO(r.error);
    REQUIRE(r.state.has_value());
    CHECK(r.state->triggerNote == 60);
    CHECK(r.state->preset.name == "State test");
    REQUIRE(r.state->preset.sampleA.has_value());
    CHECK(r.state->preset.sampleA->tuneSemitones == Approx(3.0));
    CHECK(r.samples.size() == 1);
    CHECK(r.samples.at("samples/kick.wav") == kick);
}

TEST_CASE("a state without samples round-trips as bytes", "[state][bytes]") {
    const auto written = pluginStateToBytes(sampleState(), {});
    REQUIRE(written.ok());
    const auto r = pluginStateFromBytes(written.bytes);
    REQUIRE(r.state.has_value());
    CHECK(r.state->triggerNote == 60);
    CHECK(r.samples.empty());
}

TEST_CASE("states saved as plain JSON are still read", "[state][bytes]") {
    const std::string text = pluginStateToJson(sampleState());
    const auto r = pluginStateFromBytes(Bytes(text.begin(), text.end()));
    INFO(r.error);
    REQUIRE(r.state.has_value());
    CHECK(r.state->triggerNote == 60);
    CHECK(r.samples.empty());
}

TEST_CASE("writing a state fails when a sample is missing", "[state][bytes]") {
    PluginState s = sampleState();
    s.preset.sampleA = SampleSlot{"kick.wav", std::string(64, 'a'), "samples/kick.wav", 0, 0, 1, 0};
    CHECK_FALSE(pluginStateToBytes(s, {}).ok());
}

TEST_CASE("broken binary states are rejected", "[state][bytes]") {
    CHECK_FALSE(pluginStateFromBytes({}).state.has_value());
    CHECK_FALSE(pluginStateFromBytes(pseudoBytes(500, 9)).state.has_value());

    // a plain .dwpreset has no instance.json: it is not a plugin state
    PresetArchive plain;
    plain.preset = defaultPreset(1);
    const auto zip = writePresetArchive(plain);
    REQUIRE(zip.ok());
    CHECK_FALSE(pluginStateFromBytes(zip.bytes).state.has_value());

    // instance.json with a bad note
    PresetArchive bad;
    bad.preset = defaultPreset(1);
    const std::string inst = R"({"stateVersion":1,"triggerNote":200})";
    bad.extras["instance.json"] = Bytes(inst.begin(), inst.end());
    const auto badZip = writePresetArchive(bad);
    REQUIRE(badZip.ok());
    CHECK_FALSE(pluginStateFromBytes(badZip.bytes).state.has_value());
}

// ---- the baseline for dirty tracking ----

TEST_CASE("the saved baseline travels with the state", "[state][bytes][dirty]") {
    PluginState s = sampleState();
    Preset baseline = s.preset;
    baseline.intensity = 12.5;
    baseline.name = "As saved";
    s.baseline = baseline;

    const auto written = pluginStateToBytes(s, {});
    REQUIRE(written.ok());
    const auto r = pluginStateFromBytes(written.bytes);
    REQUIRE(r.state.has_value());
    REQUIRE(r.state->baseline.has_value());
    CHECK(r.state->baseline->name == "As saved");
    CHECK(r.state->baseline->intensity == Approx(12.5));
    CHECK(r.state->preset.name == "State test");
}

TEST_CASE("a state without a baseline reads without one", "[state][bytes][dirty]") {
    const auto written = pluginStateToBytes(sampleState(), {});
    REQUIRE(written.ok());
    const auto r = pluginStateFromBytes(written.bytes);
    REQUIRE(r.state.has_value());
    CHECK_FALSE(r.state->baseline.has_value());
}

TEST_CASE("a damaged baseline is ignored, the state still loads", "[state][bytes][dirty]") {
    PresetArchive a;
    a.preset = defaultPreset(1);
    const std::string instance = R"({"stateVersion":1,"triggerNote":50})";
    a.extras["instance.json"] = Bytes(instance.begin(), instance.end());
    const std::string bad = R"({"not":"a preset"})";
    a.extras["baseline.json"] = Bytes(bad.begin(), bad.end());
    const auto zip = writePresetArchive(a);
    REQUIRE(zip.ok());
    const auto r = pluginStateFromBytes(zip.bytes);
    INFO(r.error);
    REQUIRE(r.state.has_value());
    CHECK(r.state->triggerNote == 50);
    CHECK_FALSE(r.state->baseline.has_value());
}
