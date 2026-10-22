cmake_minimum_required(VERSION 3.22)
project(ZaoDrive VERSION 0.2.0 LANGUAGES C CXX)
set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(ZaoDrive
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.jsonic.zaodrive"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE ZaD1
    FORMATS ${plugin_formats}
    PRODUCT_NAME "Zao Drive"
    DESCRIPTION "Common-emitter and op-amp feedback circuit overdrive"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Distortion)
target_sources(ZaoDrive PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SmartHarmonics.cpp)
target_compile_features(ZaoDrive PUBLIC cxx_std_17)
target_compile_definitions(ZaoDrive PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ZaoDrive PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
option(ZAO_BUILD_TESTS "Build Zao Drive tests" ON)
if(ZAO_BUILD_TESTS)
    enable_testing()
    add_executable(ZaoDriveTests Tests/ProcessorTests.cpp)
    target_include_directories(ZaoDriveTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(ZaoDriveTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(ZaoDriveTests PRIVATE ZaoDrive)
    add_test(NAME ZaoDriveProcessor COMMAND ZaoDriveTests)
endif()
