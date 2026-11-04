cmake_minimum_required(VERSION 3.22)

project(PrismPatch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DPRISM_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(PRISM_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(PRISM_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(PRISM_BUILD_TESTS "Build the offline render test" ON)

if(PRISM_BUILD_PLUGIN)
    if(PRISM_JUCE_PATH)
        add_subdirectory(${PRISM_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(PRISM_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND PRISM_FORMATS AU)
    endif()

    juce_add_plugin(PrismPatch
        COMPANY_NAME "Kevin Kaye"
        COMPANY_WEBSITE ""
        BUNDLE_ID "uk.kevinkaye.prismpatch"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Prsm
        FORMATS ${PRISM_FORMATS}
        PRODUCT_NAME "Prism Patch"
        DESCRIPTION "Four reorderable modules of colour, movement, space and texture"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Distortion Modulation Delay Reverb)

    juce_add_binary_data(PrismData SOURCES Resources/RobotoCondensed-Bold.ttf)

    target_sources(PrismPatch PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PedalControl.cpp)

    target_include_directories(PrismPatch PRIVATE Source Source/engine)

    target_compile_definitions(PrismPatch PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(PrismPatch
        PRIVATE
            PrismData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Offline engine test (no JUCE needed): renders test audio through every effect
# ---------------------------------------------------------------------------
if(PRISM_BUILD_TESTS)
    add_executable(prism_render_test Tests/RenderTest.cpp)
    target_include_directories(prism_render_test PRIVATE Source/engine)
endif()
