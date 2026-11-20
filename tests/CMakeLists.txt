add_executable(dw_tests test_Smoke.cpp test_Rng.cpp test_RollGenerator.cpp test_VelocityCurve.cpp
    test_Expression.cpp test_VelocityFollow.cpp test_RollScheduler.cpp
    test_Preset.cpp test_PluginState.cpp)
target_link_libraries(dw_tests PRIVATE dw_engine nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
target_compile_definitions(dw_tests PRIVATE DW_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/schemas/examples")
include(Catch)
catch_discover_tests(dw_tests)
