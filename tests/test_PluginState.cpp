#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "engine/PluginState.h"

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
