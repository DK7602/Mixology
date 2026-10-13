cmake_minimum_required(VERSION 3.25)

# Universal binaries on macOS (Apple silicon + Intel). Must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(Mixology VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Static MSVC runtime so users don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MIX_BUILD_PLUGIN "Build the JUCE plug-in (VST3 / Standalone)" ON)
option(MIX_BUILD_TESTS  "Build the engine unit tests" ON)

# Framework-free engine: Cubase bridge protocol + session, analysis (later), mix decisions (later).
add_subdirectory(dsp)

if(MIX_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()

if(MIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
