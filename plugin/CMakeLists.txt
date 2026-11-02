# Plugin-Code ohne Processor/Editor, den auch die Tests linken koennen.
add_library(dc_plugin_shared STATIC
    LoopLoader.h LoopLoader.cpp
    HostTransport.h HostTransport.cpp
    PluginState.h PluginState.cpp
    FileRelink.h FileRelink.cpp)
target_include_directories(dc_plugin_shared PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(dc_plugin_shared PUBLIC
    dc_engine
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_data_structures
    juce::juce_cryptography
    juce::juce_events
    juce::juce_gui_basics
    PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Processor und Editor als Quellen, die jedes Ziel mit seinen eigenen Flags uebersetzt
# (Plugin und dc_plugin_tests), damit die Tests den Editor instanziieren koennen.
add_library(dc_plugin_ui INTERFACE)
target_sources(dc_plugin_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/DubcyclingProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DubcyclingProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LoopLane.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LoopLane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Editor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Editor.cpp)
target_include_directories(dc_plugin_ui INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(dc_plugin_ui INTERFACE
    dc_engine
    dc_plugin_shared
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics)

juce_add_plugin(Dubcycling
    COMPANY_NAME "atze187"
    BUNDLE_ID "de.atze187.dubcycling"
    PLUGIN_MANUFACTURER_CODE Atze
    PLUGIN_CODE Dcyc
    FORMATS VST3
    PRODUCT_NAME "Dubcycling"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)

target_include_directories(Dubcycling PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(Dubcycling PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Dubcycling PRIVATE
    dc_engine
    dc_plugin_shared
    dc_plugin_ui
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
