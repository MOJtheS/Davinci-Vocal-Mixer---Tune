cmake_minimum_required(VERSION 3.24)

# must be set BEFORE project() to take effect: universal binary, macOS 11+
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(HumRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG 0.6.6
    GIT_SHALLOW ON)
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE signalsmith-linear signalsmith-stretch)

# ---------------------------------------------------------------- plugin
juce_add_plugin(HumRig
    COMPANY_NAME "HumRig"
    PLUGIN_MANUFACTURER_CODE HuRg
    PLUGIN_CODE HuR1
    FORMATS VST3 AU
    PRODUCT_NAME "HumRig"
    BUNDLE_ID "com.humrig.plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Pitch\ Shift
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Linux dev builds only produce VST3; AU is Apple-only (juce_add_plugin ignores it elsewhere).

target_sources(HumRig PRIVATE
    Source/PluginEntry.cpp
    Source/PluginProcessor.cpp
    Source/Parameters.cpp
    Source/DSP/PitchCorrector.cpp
    Source/Editor/PluginEditor.cpp)

target_include_directories(HumRig PRIVATE Source)
target_compile_definitions(HumRig PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_link_libraries(HumRig
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests (headless, run on Linux/macOS)
option(HUMRIG_BUILD_TESTS "Build the headless DSP/processor test harness" ON)
if(HUMRIG_BUILD_TESTS)
    juce_add_console_app(HumRigTests PRODUCT_NAME "HumRigTests")
    target_sources(HumRigTests PRIVATE
        tests/Harness.cpp
        Source/PluginProcessor.cpp
        Source/Parameters.cpp
        Source/DSP/PitchCorrector.cpp
        Source/Editor/PluginEditor.cpp)
    target_include_directories(HumRigTests PRIVATE Source)
    target_compile_definitions(HumRigTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(HumRigTests PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics
        signalsmith-stretch
        juce::juce_recommended_config_flags)
endif()
