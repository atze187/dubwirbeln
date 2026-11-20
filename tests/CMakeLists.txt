add_executable(dw_tests test_Smoke.cpp test_Rng.cpp test_RollGenerator.cpp test_VelocityCurve.cpp
    test_Expression.cpp test_VelocityFollow.cpp test_RollScheduler.cpp)
target_link_libraries(dw_tests PRIVATE dw_engine Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(dw_tests)
