add_library(mixology_dsp STATIC
    src/AssistEq.cpp
    src/AssistRestore.cpp
    src/ProcChain.cpp
    src/ProcDsp.cpp
    src/ProcParams.cpp
    src/SpaceFx.cpp
    src/BridgeProtocol.cpp
    src/BridgeSession.cpp
    src/FaderLaw.cpp
    src/Fft.cpp
    src/LinkRegistry.cpp
    src/MixAnalysis.cpp
    src/MixPlanner.cpp
    src/SharedBlock.cpp
    src/TrackMeter.cpp
    src/TrackAnalyzer.cpp
    src/KWeighting.cpp
    src/TruePeakDetector.cpp
    src/LoudnessMeter.cpp)

target_include_directories(mixology_dsp PUBLIC include)
target_compile_features(mixology_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(mixology_dsp PRIVATE /W4 /fp:precise)
else()
    target_compile_options(mixology_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# POSIX shared memory (shm_open) lives in librt on older glibc.
if(UNIX AND NOT APPLE)
    target_link_libraries(mixology_dsp PRIVATE rt)
endif()

set_target_properties(mixology_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Mixology")
