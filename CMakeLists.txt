cmake_minimum_required(VERSION 3.22)
# Windows: the C++ runtime is built into the plugin, so it runs without installing Visual C++ redistributables
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(HalfLife VERSION 0.4.0)
set(CMAKE_CXX_STANDARD 17)
# JUCE 8 by default (no splash screen, sellable under the free Starter licence); JUCE7 kept as a fallback: -DHL_JUCE=JUCE7
set(HL_JUCE "JUCE8" CACHE STRING "JUCE folder to build with")
add_subdirectory(${HL_JUCE})

juce_add_plugin(HalfLife
    COMPANY_NAME "Half Life"
    PLUGIN_MANUFACTURER_CODE Hlfl
    PLUGIN_CODE Hlf1
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Sampler
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Half Life")

juce_generate_juce_header(HalfLife)
file(GLOB HL_FACTORY_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/*.hlpreset)   # factory presets, built into the plugin
juce_add_binary_data(HalfLifeData SOURCES
    Resources/Archivo-Regular.ttf Resources/Archivo-Bold.ttf Resources/Archivo-Black.ttf ${HL_FACTORY_PRESETS})

target_sources(HalfLife PRIVATE Source/Dsp.cpp Source/Engine.cpp Source/Params.cpp Source/Ui.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(HalfLife PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(HalfLife PRIVATE HalfLifeData
    juce::juce_audio_utils juce::juce_opengl juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(MINGW)
    target_link_options(HalfLife_VST3 PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(HalfLife PRIVATE opengl32)
endif()

if(HL_TESTS)
    juce_add_gui_app(HalfLifeTest PRODUCT_NAME "HalfLifeTest")
    juce_generate_juce_header(HalfLifeTest)
    target_sources(HalfLifeTest PRIVATE tests/AppTest.cpp Source/Dsp.cpp Source/Engine.cpp Source/Params.cpp Source/Ui.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(HalfLifeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 HL_NO_PLUGIN_ENTRY=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(HalfLifeTest PRIVATE HalfLifeData juce::juce_audio_utils juce::juce_opengl juce::juce_dsp juce::juce_recommended_config_flags)
endif()
