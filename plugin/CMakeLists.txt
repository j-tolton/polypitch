cmake_minimum_required(VERSION 3.22)

project(PolyPitchPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A host process can already hold an older C++ runtime, loaded by something else entirely (a shell
# extension in a file dialog, say), and a plugin would attach to that copy and crash in it. Building
# the runtime in means it's never at the mercy of what else is loaded.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The system VST3 folder needs an elevated build, so copying there is opt-in.
option(POLYPITCH_COPY_PLUGIN "Copy the VST3 to the system folder after building" OFF)
option(POLYPITCH_BUILD_TESTS "Build the render and host tests" ON)

add_subdirectory("C:/Development/JUCE" JUCE)

set(POLYPITCH_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/TraceView.cpp
    Source/TraceView.h
    Source/Scales.h
    Source/Style.h
    Source/TrackHost.h
    ../dsp/polypitch.cpp
    ../dsp/polypitch.h
    ../dsp/dual_engine.h
    ../dsp/tracker.cpp
    ../dsp/tracker.h
    ../dsp/refine.cpp
    ../dsp/refine.h)

juce_add_plugin(PolyPitch
    COMPANY_NAME "Jon Tolton"
    BUNDLE_ID "uk.co.jontolton.polypitch"
    PLUGIN_MANUFACTURER_CODE Jtln
    PLUGIN_CODE Ppch
    FORMATS VST3
    PRODUCT_NAME "PolyPitch"
    VST3_CATEGORIES Fx Analyzer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD ${POLYPITCH_COPY_PLUGIN})

juce_generate_juce_header(PolyPitch)

target_sources(PolyPitch PRIVATE ${POLYPITCH_SOURCES} Source/ReaperBridge.cpp Source/ReaperBridge.h)
target_include_directories(PolyPitch PRIVATE ../dsp)

target_compile_definitions(PolyPitch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PolyPitch PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(POLYPITCH_BUILD_TESTS)
    # Runs a WAV through the plugin's own classes and saves what the editor draws.
    juce_add_console_app(PolyPitchRenderTest PRODUCT_NAME "PolyPitchRenderTest")
    juce_generate_juce_header(PolyPitchRenderTest)
    target_sources(PolyPitchRenderTest PRIVATE tests/render_test.cpp ${POLYPITCH_SOURCES})
    target_include_directories(PolyPitchRenderTest PRIVATE ../dsp Source)
    target_compile_definitions(PolyPitchRenderTest PRIVATE
        POLYPITCH_NO_PLUGIN_ENTRY=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(PolyPitchRenderTest PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Loads the built VST3 the way a host does.
    juce_add_console_app(PolyPitchHostTest PRODUCT_NAME "PolyPitchHostTest")
    juce_generate_juce_header(PolyPitchHostTest)
    target_sources(PolyPitchHostTest PRIVATE tests/host_test.cpp)
    target_compile_definitions(PolyPitchHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(PolyPitchHostTest PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
