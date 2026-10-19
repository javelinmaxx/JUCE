cmake_minimum_required(VERSION 3.22)

# Set before project() so an Intel AU build can run on Catalina.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Target architectures")
endif()
project(CharacterCompressor VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "JUCE checkout")
add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(CharacterCompressor
    COMPANY_NAME "J-SONIC"
    BUNDLE_ID "audio.character.compressor"
    PLUGIN_MANUFACTURER_CODE Chra
    PLUGIN_CODE Chc1
    FORMATS ${plugin_formats}
    PRODUCT_NAME "YinYang Compressor"
    DESCRIPTION "Four-model dynamics processor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(CharacterCompressor PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(CharacterCompressor PUBLIC cxx_std_17)
target_compile_definitions(CharacterCompressor PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(CharacterCompressor PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(CHARACTER_BUILD_TESTS "Build DSP and plug-in integration tests" ON)
if(CHARACTER_BUILD_TESTS)
    enable_testing()
    add_executable(CharacterDSPTests Tests/DSPTests.cpp)
    target_compile_features(CharacterDSPTests PRIVATE cxx_std_17)
    add_test(NAME CharacterDSP COMMAND CharacterDSPTests)
    add_executable(CharacterProcessorTests Tests/ProcessorTests.cpp)
    target_include_directories(CharacterProcessorTests PRIVATE "${JUCE_SOURCE_DIR}/modules")
    target_compile_definitions(CharacterProcessorTests PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(CharacterProcessorTests PRIVATE CharacterCompressor)
    add_test(NAME CharacterProcessor COMMAND CharacterProcessorTests)
    add_executable(CharacterUpwardCompressionTests Tests/UpwardCompressionTests.cpp)
    target_compile_features(CharacterUpwardCompressionTests PRIVATE cxx_std_17)
    add_test(NAME CharacterUpwardCompression COMMAND CharacterUpwardCompressionTests)
endif()
