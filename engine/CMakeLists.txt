add_library(dw_engine STATIC Version.h Version.cpp Rng.h RollGenerator.h RollGenerator.cpp
    VelocityCurve.h VelocityCurve.cpp Expression.h Expression.cpp
    VelocityFollow.h VelocityFollow.cpp RollScheduler.h RollScheduler.cpp
    Preset.h Preset.cpp PluginState.h PluginState.cpp SampleEngine.h SampleEngine.cpp Variate.h Variate.cpp TriggerInput.h TriggerInput.cpp GestureRecognizer.h GestureRecognizer.cpp NoteNames.h NoteNames.cpp
    Sha256.h Sha256.cpp PresetArchive.h PresetArchive.cpp)
target_include_directories(dw_engine PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(dw_engine PUBLIC cxx_std_20)
target_link_libraries(dw_engine PRIVATE nlohmann_json::nlohmann_json miniz)
if(MSVC)
    target_compile_options(dw_engine PRIVATE /W4 /permissive-)
endif()
