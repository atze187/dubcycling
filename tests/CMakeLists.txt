add_executable(dc_tests
    test_Smoke.cpp
    test_LoopTiming.cpp
    test_LoopReader.cpp
    test_RegionMap.cpp
    test_RuleSet.cpp
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
    plugin/test_FileRelink.cpp)
target_link_libraries(dc_plugin_tests PRIVATE dc_plugin_shared Catch2::Catch2WithMain)
catch_discover_tests(dc_plugin_tests)
