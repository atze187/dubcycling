add_library(dc_engine STATIC
    Version.h Version.cpp
    LoopTiming.h LoopTiming.cpp
    SampleInterp.h GrainStretch.h GrainStretch.cpp
    LoopReader.h LoopReader.cpp
    RegionMap.h RegionMap.cpp
    RuleSet.h RuleSet.cpp
    Sequencer.h Sequencer.cpp)
target_include_directories(dc_engine PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(dc_engine PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(dc_engine PRIVATE /W4 /permissive-)
endif()
