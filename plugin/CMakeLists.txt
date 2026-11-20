juce_add_plugin(Dubwirbeln
    COMPANY_NAME "atze187"
    BUNDLE_ID "de.atze187.dubwirbeln"
    PLUGIN_MANUFACTURER_CODE Atze
    PLUGIN_CODE Dwir
    FORMATS VST3
    PRODUCT_NAME "Dubwirbeln"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # No audio input: the plugin generates its own audio, so it is an instrument.
    VST3_CATEGORIES Instrument)

target_sources(Dubwirbeln PRIVATE
    DubwirbelnProcessor.h
    DubwirbelnProcessor.cpp)
target_include_directories(Dubwirbeln PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(Dubwirbeln PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Dubwirbeln PRIVATE
    dw_engine
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_cryptography
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
