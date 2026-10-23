cmake_minimum_required(VERSION 3.22)
project(KuoStereo VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(KuoStereo
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.jsonic.kuostereo"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE KSt1
    FORMATS ${plugin_formats}
    PRODUCT_NAME "Kuo Stereo"
    DESCRIPTION "Adaptive Haas and transient-aware velvet stereo widener"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Spatial)
target_sources(KuoStereo PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(KuoStereo PUBLIC cxx_std_17)
target_compile_definitions(KuoStereo PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(KuoStereo PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(KUO_STEREO_BUILD_TESTS "Build Kuo Stereo tests" ON)
if(KUO_STEREO_BUILD_TESTS)
    enable_testing()
    add_executable(KuoStereoTests Tests/ProcessorTests.cpp)
    target_include_directories(KuoStereoTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(KuoStereoTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(KuoStereoTests PRIVATE KuoStereo)
    add_test(NAME KuoStereoProcessor COMMAND KuoStereoTests)

    add_executable(KuoStereoFeatureMeasurements Measurements/FeatureMeasurements.cpp)
    target_include_directories(KuoStereoFeatureMeasurements PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_link_libraries(KuoStereoFeatureMeasurements PRIVATE KuoStereo)
endif()
