cmake_minimum_required(VERSION 3.22)
project(XEX03 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

option(COPY_AFTER_BUILD "Copiar el VST3 a la carpeta del sistema al compilar" ON)
option(XEX_BUILD_TESTS  "Compilar tambien las pruebas del motor"            OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(XEX03
    COMPANY_NAME "MiEstudio"
    PLUGIN_MANUFACTURER_CODE Mies
    PLUGIN_CODE Xex3
    FORMATS VST3 Standalone
    PRODUCT_NAME "XEX03"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

target_sources(XEX03 PRIVATE
    Source/dsp/XexEngine.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

target_include_directories(XEX03 PRIVATE Source)

target_compile_definitions(XEX03 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(XEX03
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(XEX_BUILD_TESTS)
    add_executable(xex_tests tests/test_engine.cpp)
endif()
