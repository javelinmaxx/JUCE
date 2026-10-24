cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../PluginVersion.cmake")
project(RenTransformer VERSION ${J_SONIC_PLUGIN_VERSION} LANGUAGES C CXX)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(RenTransformer
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.jsonic.rentransformer"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE RnT1
    FORMATS ${plugin_formats}
    PRODUCT_NAME "Ren Transformer"
    DESCRIPTION "Standalone Ren Transformer stage"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Distortion)
target_sources(RenTransformer PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp ../StagePluginStyle.cpp)
target_compile_features(RenTransformer PUBLIC cxx_std_17)
target_compile_definitions(RenTransformer PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(RenTransformer PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
option(REN_TRANSFORMER_BUILD_TESTS "Build Ren Transformer tests" ON)
if(REN_TRANSFORMER_BUILD_TESTS)
    enable_testing()
    add_executable(RenTransformerTests Tests/ProcessorTests.cpp)
    target_include_directories(RenTransformerTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(RenTransformerTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(RenTransformerTests PRIVATE RenTransformer)
    target_compile_definitions(RenTransformerTests PRIVATE STAGE_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME RenTransformerProcessor COMMAND RenTransformerTests)
endif()
