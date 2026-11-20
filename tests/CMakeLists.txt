add_executable(dw_tests test_Smoke.cpp test_Rng.cpp test_RollGenerator.cpp test_VelocityCurve.cpp
    test_Expression.cpp test_VelocityFollow.cpp test_RollScheduler.cpp
    test_Preset.cpp test_PluginState.cpp test_SampleEngine.cpp
    test_Sha256.cpp test_PresetArchive.cpp)
target_link_libraries(dw_tests PRIVATE dw_engine nlohmann_json::nlohmann_json miniz Catch2::Catch2WithMain)
target_compile_definitions(dw_tests PRIVATE DW_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/schemas/examples")
include(Catch)
catch_discover_tests(dw_tests)

# The real processor (no host, no VST3 wrapper): samples, state, preset files and the audio path.
add_executable(dw_plugin_tests plugin/test_Processor.cpp ${PROJECT_SOURCE_DIR}/plugin/DubwirbelnProcessor.cpp)
target_include_directories(dw_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(dw_plugin_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(dw_plugin_tests PRIVATE
    dw_engine
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    Catch2::Catch2WithMain)
catch_discover_tests(dw_plugin_tests TEST_PREFIX "plugin: ")
