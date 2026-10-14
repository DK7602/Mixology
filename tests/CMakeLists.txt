include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(mixology_tests
    test_bridge.cpp
    test_link.cpp
    test_analysis.cpp
    test_proc.cpp
    test_space.cpp
    test_polish.cpp)

target_link_libraries(mixology_tests PRIVATE mixology_dsp Catch2::Catch2WithMain)
set_target_properties(mixology_tests PROPERTIES FOLDER "Mixology")

include(Catch)
catch_discover_tests(mixology_tests)

# Line-based wrapper used by the end-to-end bridge simulator (tools/bridge-sim/run.js runs the real
# Cubase script in Node against it). Registered as a test when Node is available.
add_executable(bridge_session_cli ../tools/bridge-sim/session_cli.cpp)
target_link_libraries(bridge_session_cli PRIVATE mixology_dsp)
set_target_properties(bridge_session_cli PROPERTIES FOLDER "Mixology")

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME bridge_end_to_end
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bridge-sim/run.js $<TARGET_FILE:bridge_session_cli>)
endif()

# The interface's copy of the processing control table must match the engine's.
add_executable(procspec ../tools/procspec/procspec.cpp)
target_link_libraries(procspec PRIVATE mixology_dsp)
set_target_properties(procspec PROPERTIES FOLDER "Mixology")
add_test(NAME procspec_in_sync COMMAND procspec ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/ui/procspec.js)
