cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(DrumTrigger VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DRUMTRIGGER_BUILD_TESTS "Build the headless DSP/instrument test suite" ON)
set(JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout (otherwise JUCE is downloaded)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(DT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DT_FORMATS AU)
endif()

juce_add_plugin(DrumTrigger
    COMPANY_NAME "DrumTrigger"
    BUNDLE_ID com.drumtrigger.plugin
    PLUGIN_MANUFACTURER_CODE Dtrg
    PLUGIN_CODE Dtr1
    FORMATS ${DT_FORMATS}
    PRODUCT_NAME "DrumTrigger"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Drum
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(DT_CORE_SOURCES
    Source/engine/Instrument.cpp
    Source/builder/InstrumentBuilder.cpp)

target_sources(DrumTrigger PRIVATE
    ${DT_CORE_SOURCES}
    Source/Params.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/BuilderPanel.cpp)

target_include_directories(DrumTrigger PRIVATE Source)

target_compile_definitions(DrumTrigger PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DrumTrigger
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(DRUMTRIGGER_BUILD_TESTS)
    juce_add_console_app(DrumTriggerTests PRODUCT_NAME "DrumTriggerTests")
    # The tests also compile the processor/editor so the full signal path can be exercised headless.
    target_sources(DrumTriggerTests PRIVATE tests/Tests.cpp ${DT_CORE_SOURCES}
        Source/Params.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ui/BuilderPanel.cpp)
    target_include_directories(DrumTriggerTests PRIVATE Source)
    target_compile_definitions(DrumTriggerTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 DT_TEST_PROCESSOR=1)
    target_link_libraries(DrumTriggerTests
        PRIVATE juce::juce_audio_utils juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
