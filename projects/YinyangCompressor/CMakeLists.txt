cmake_minimum_required(VERSION 3.22)

# Set before project() so an Intel AU build can run on Catalina.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Target architectures")
endif()
project(YinyangCompressor VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(YinyangCompressor
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.jsonic.yinyangcompressor"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE Chc1
    FORMATS ${plugin_formats}
    PRODUCT_NAME "Yinyang Compressor"
    DESCRIPTION "Four-model dynamics processor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(YinyangCompressor PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(YinyangCompressor PUBLIC cxx_std_17)
target_compile_definitions(YinyangCompressor PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(YinyangCompressor PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(YINYANG_COMPRESSOR_BUILD_TESTS "Build DSP and plug-in integration tests" ON)
if(YINYANG_COMPRESSOR_BUILD_TESTS)
    enable_testing()
    add_executable(YinyangCompressorDSPTests Tests/DSPTests.cpp)
    target_compile_features(YinyangCompressorDSPTests PRIVATE cxx_std_17)
    add_test(NAME YinyangCompressorDSP COMMAND YinyangCompressorDSPTests)
    add_executable(YinyangCompressorTests Tests/ProcessorTests.cpp)
    target_include_directories(YinyangCompressorTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(YinyangCompressorTests PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(YinyangCompressorTests PRIVATE YinyangCompressor)
    add_test(NAME YinyangCompressorProcessor COMMAND YinyangCompressorTests)
    add_executable(YinyangCompressorUpwardCompressionTests Tests/UpwardCompressionTests.cpp)
    target_compile_features(YinyangCompressorUpwardCompressionTests PRIVATE cxx_std_17)
    add_test(NAME YinyangCompressorUpwardCompression COMMAND YinyangCompressorUpwardCompressionTests)
    add_executable(YinyangCompressorOversamplingTests Tests/OversamplingTests.cpp)
    target_compile_features(YinyangCompressorOversamplingTests PRIVATE cxx_std_17)
    add_test(NAME YinyangCompressorOversampling COMMAND YinyangCompressorOversamplingTests)
    add_executable(YinyangCompressorPerformanceBench Tests/PerformanceBench.cpp)
    target_compile_features(YinyangCompressorPerformanceBench PRIVATE cxx_std_17)
endif()
