cmake_minimum_required(VERSION 3.22)
project(ShunDeEsser VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(ShunDeEsser
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.jsonic.shundeesser"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE ShDe
    FORMATS ${plugin_formats}
    PRODUCT_NAME "Shun De-Esser"
    DESCRIPTION "Wideband, Split-band, Brilliant, and Smooth spectral de-esser"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Dynamics)
target_sources(ShunDeEsser PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(ShunDeEsser PUBLIC cxx_std_17)
target_compile_definitions(ShunDeEsser PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ShunDeEsser PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(SHUN_DE_ESSER_BUILD_TESTS "Build Shun De-Esser processor tests" ON)
if(SHUN_DE_ESSER_BUILD_TESTS)
    enable_testing()
    add_executable(ShunDeEsserTests Tests/ProcessorTests.cpp)
    target_include_directories(ShunDeEsserTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(ShunDeEsserTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(ShunDeEsserTests PRIVATE ShunDeEsser)
    add_test(NAME ShunDeEsserProcessor COMMAND ShunDeEsserTests)
endif()
