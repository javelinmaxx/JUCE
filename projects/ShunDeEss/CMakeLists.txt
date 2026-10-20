cmake_minimum_required(VERSION 3.22)
project(ShunDeEss VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(ShunDeEss
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.jsonic.shundeess"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE ShDe
    FORMATS ${plugin_formats}
    PRODUCT_NAME "SHUN DE-ESS"
    DESCRIPTION "Wideband and split-band de-esser"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Dynamics)
target_sources(ShunDeEss PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(ShunDeEss PUBLIC cxx_std_17)
target_compile_definitions(ShunDeEss PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ShunDeEss PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(SHUN_BUILD_TESTS "Build SHUN DE-ESS processor tests" ON)
if(SHUN_BUILD_TESTS)
    enable_testing()
    add_executable(ShunDeEssTests Tests/ProcessorTests.cpp)
    target_include_directories(ShunDeEssTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(ShunDeEssTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(ShunDeEssTests PRIVATE ShunDeEss)
    add_test(NAME ShunDeEssProcessor COMMAND ShunDeEssTests)
endif()
