cmake_minimum_required(VERSION 3.22)
project(Entropy VERSION 1.0.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()

option(ENTROPY_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(ENTROPY_BUILD_TESTS "Build offline regression tests" ON)
option(ENTROPY_COPY_PLUGIN_AFTER_BUILD "Install plugins after building" OFF)

add_library(EntropyDSP STATIC Source/Dsp/EntropyEngine.cpp Source/Dsp/Recipes.cpp)
set_target_properties(EntropyDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(EntropyDSP PUBLIC Source)

if(ENTROPY_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
    set(entropy_formats VST3 Standalone)
    if(APPLE)
        list(APPEND entropy_formats AU)
    endif()
    juce_add_plugin(Entropy
        PRODUCT_NAME "ChemE-Entropy"
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.ChemEEntropy"
        PLUGIN_MANUFACTURER_CODE Isac
        PLUGIN_CODE CE02
        FORMATS ${entropy_formats}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
        VST3_CATEGORIES Fx Distortion
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${ENTROPY_COPY_PLUGIN_AFTER_BUILD})
    juce_generate_juce_header(Entropy)
    target_sources(Entropy PRIVATE
        Source/Parameters.cpp Source/Parameters.h
        Source/PluginProcessor.cpp Source/PluginProcessor.h
        Source/PluginEditor.cpp Source/PluginEditor.h
        Source/UI/ScientificLookAndFeel.h Source/UI/TimelineControl.h Source/UI/PresetPanel.h
        Source/Timeline.h Source/Timeline.cpp Source/Thermodynamics.h)
    target_compile_definitions(Entropy PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Entropy PRIVATE EntropyDSP juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(Entropy PRIVATE /utf-8)
    endif()
endif()

if(MSVC)
    target_compile_options(EntropyDSP PRIVATE /W4 /utf-8)
else()
    target_compile_options(EntropyDSP PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENTROPY_BUILD_TESTS)
    enable_testing()
    add_executable(EntropyDSPTests Tests/DspTests.cpp)
    target_link_libraries(EntropyDSPTests PRIVATE EntropyDSP)
    add_test(NAME EntropyDSPTests COMMAND EntropyDSPTests)
    if(ENTROPY_BUILD_PLUGIN)
        juce_add_console_app(EntropyStateTests PRODUCT_NAME "Entropy State Tests")
        target_sources(EntropyStateTests PRIVATE Tests/StateTests.cpp)
        target_include_directories(EntropyStateTests PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/Entropy_artefacts/JuceLibraryCode")
        target_link_libraries(EntropyStateTests PRIVATE Entropy juce::juce_audio_utils juce::juce_dsp)
        if(MSVC)
            target_compile_options(EntropyStateTests PRIVATE /utf-8)
        endif()
        add_test(NAME EntropyStateTests COMMAND EntropyStateTests)
    endif()
endif()
