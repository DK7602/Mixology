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
    test_polish.cpp
    test_suite.cpp
    test_setup.cpp
    test_presets.cpp
    test_history.cpp)

target_link_libraries(mixology_tests PRIVATE mixology_dsp Catch2::Catch2WithMain)
target_compile_definitions(mixology_tests PRIVATE MIXOLOGY_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(mixology_tests PROPERTIES FOLDER "Mixology")

include(Catch)
catch_discover_tests(mixology_tests)

# Line-based wrapper used by the end-to-end bridge simulator (tools/bridge-sim/run.js runs the real
# Cubase script in Node against it). Registered as a test when Node is available.
add_executable(bridge_session_cli ../tools/bridge-sim/session_cli.cpp)
target_link_libraries(bridge_session_cli PRIVATE mixology_dsp)
set_target_properties(bridge_session_cli PROPERTIES FOLDER "Mixology")

# The FL Studio and Ableton Live bridges (Python): the real scripts against fake DAWs and the same session, and
# the bridge core is the same file in bridge/common and in each script's folder.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME python_bridges_end_to_end
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bridge-sim/run_py.py $<TARGET_FILE:bridge_session_cli>)
    add_test(NAME bridge_core_copies_in_sync
             COMMAND ${CMAKE_COMMAND} -E compare_files
                     ${CMAKE_CURRENT_SOURCE_DIR}/../bridge/common/mixology_bridge_core.py
                     "${CMAKE_CURRENT_SOURCE_DIR}/../bridge/flstudio/Mixology Bridge/mixology_bridge_core.py")
    add_test(NAME bridge_core_copies_in_sync_live
             COMMAND ${CMAKE_COMMAND} -E compare_files
                     ${CMAKE_CURRENT_SOURCE_DIR}/../bridge/common/mixology_bridge_core.py
                     ${CMAKE_CURRENT_SOURCE_DIR}/../bridge/ableton/MixologyBridge/mixology_bridge_core.py)
endif()

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME bridge_end_to_end
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bridge-sim/run.js $<TARGET_FILE:bridge_session_cli>)
endif()

if(NODE_EXECUTABLE)
    # The interface's copy of the track-name rules must give the same roles as the engine's.
    add_test(NAME role_names_in_sync
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/rolecheck/rolecheck.js
                     ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/ui/roles.js ${CMAKE_CURRENT_SOURCE_DIR}/role_names.txt)
    # Every DAW has its words for every job, and every {daw:phrase} in the sources is one of them.
    add_test(NAME daw_words
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/dawcheck/dawcheck.mjs
                     ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/ui/daws.js ${CMAKE_CURRENT_SOURCE_DIR}/../dsp ${CMAKE_CURRENT_SOURCE_DIR}/../plugin)
endif()

# The interface's copy of the processing control table must match the engine's.
add_executable(procspec ../tools/procspec/procspec.cpp)
target_link_libraries(procspec PRIVATE mixology_dsp)
set_target_properties(procspec PROPERTIES FOLDER "Mixology")
add_test(NAME procspec_in_sync COMMAND procspec ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/ui/procspec.js)
