add_executable(dw_tests test_Smoke.cpp test_Rng.cpp test_RollGenerator.cpp)
target_link_libraries(dw_tests PRIVATE dw_engine Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(dw_tests)
