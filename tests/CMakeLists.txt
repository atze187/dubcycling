add_executable(dc_tests
    test_Smoke.cpp)
target_link_libraries(dc_tests PRIVATE dc_engine Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(dc_tests)
