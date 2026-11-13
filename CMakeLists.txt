cmake_minimum_required(VERSION 3.25)
project(Dubcycling VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The engine and plugin libraries are linked into the VST3 shared module (needs -fPIC on Linux).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

enable_testing()
add_subdirectory(engine)
add_subdirectory(plugin)
add_subdirectory(tests)
