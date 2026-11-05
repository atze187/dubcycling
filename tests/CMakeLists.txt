add_executable(dc_tests
    test_Smoke.cpp
    test_LoopTiming.cpp
    test_LoopReader.cpp
    test_RegionMap.cpp
    test_RuleSet.cpp
    test_FxSettings.cpp
    test_FxUnits.cpp
    test_TransientDetector.cpp
    test_Sequencer.cpp
    test_Engine.cpp)
target_link_libraries(dc_tests PRIVATE dc_engine Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(dc_tests)

# Plugin-seitige Tests: JUCE-Konsolen-App, die dc_plugin_shared linkt.
juce_add_console_app(dc_plugin_tests PRODUCT_NAME "DubcyclingPluginTests")
target_sources(dc_plugin_tests PRIVATE
    plugin/test_LoopLoader.cpp
    plugin/test_HostTransport.cpp
    plugin/test_PluginState.cpp
    plugin/test_FileRelink.cpp
    plugin/test_LoopLane.cpp
    plugin/test_Editor.cpp
    plugin/test_RulePanel.cpp
    plugin/test_Processor.cpp
    plugin/test_SlotTracker.cpp
    plugin/test_BankFormat.cpp
    plugin/test_BankStore.cpp
    plugin/test_BankArchive.cpp
    plugin/test_PresetBar.cpp)
# dc_plugin_ui bringt Processor und Editor als Quellen mit (die Tests instanziieren den Editor).
target_link_libraries(dc_plugin_tests PRIVATE dc_plugin_shared dc_plugin_ui Catch2::Catch2WithMain)
target_compile_definitions(dc_plugin_tests PRIVATE
    DC_TEST_HOOKS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
catch_discover_tests(dc_plugin_tests)
