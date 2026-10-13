add_library(mixology_dsp STATIC
    src/BridgeProtocol.cpp
    src/BridgeSession.cpp
    src/FaderLaw.cpp
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

set_target_properties(mixology_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Mixology")
