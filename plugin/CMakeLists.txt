# ToneForge AI plugin: JUCE VST3 + Standalone wrapper around ToneForgeCore.
#
# NOT YET BUILT OR VERIFIED ANYWHERE - see docs/SETUP_WINDOWS.md. This file
# is only reached when the top-level CMakeLists.txt is configured with
# -DTONEFORGE_BUILD_PLUGIN=ON, which requires a full desktop toolchain
# (MSVC/Xcode/gcc+X11) and pulls JUCE via FetchContent (a large download).

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ToneForgeAI
    COMPANY_NAME "ToneForge"
    PRODUCT_NAME "ToneForge AI"
    PLUGIN_MANUFACTURER_CODE Tfrg
    PLUGIN_CODE Tfa1
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx Distortion
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

target_sources(ToneForgeAI PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AiPanel.cpp
    Source/ai/AiClient.cpp
    Source/state/ParameterBridge.cpp
    Source/state/ModuleRegistry.cpp
)

target_include_directories(ToneForgeAI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(ToneForgeAI PRIVATE
    tfcore
    juce::juce_audio_utils
    juce::juce_dsp
)

target_compile_definitions(ToneForgeAI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
