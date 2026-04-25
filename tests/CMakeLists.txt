cmake_minimum_required(VERSION 3.22)
project(MotiveTests LANGUAGES CXX)

set(MOTIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
option(MOTIVE_ENABLE_INTEGRATION_TESTS "Enable REST/editor integration tests" ON)
set(MOTIVE_EDITOR_BIN "${MOTIVE_ROOT}/build/motive_editor" CACHE FILEPATH "Path to motive_editor binary for integration tests")

# Add doctest include directory
include_directories(
    ${MOTIVE_ROOT}/jolt/UnitTests
    ${MOTIVE_ROOT}
    ${MOTIVE_ROOT}/glm
    ${MOTIVE_ROOT}/glm/glm
)

# Find Qt6 for UI tests (optional)
find_package(Qt6 QUIET COMPONENTS Core)

# Create test executable
add_executable(motive_tests
    unit/test_main.cpp
    unit/test_camera_follow_settings.cpp
    unit/test_orbit_follow_rig.cpp
    unit/test_camera_mode_rules.cpp
    unit/test_input_mode_rules.cpp
    unit/test_follow_target_tracker.cpp
    unit/test_inspector_ui.cpp
    unit/test_control_server.cpp
    ${MOTIVE_ROOT}/orbit_follow_rig.cpp
    ${MOTIVE_ROOT}/follow_target_tracker.cpp
)

# Link with math library for M_PI etc.
target_link_libraries(motive_tests m)

if(Qt6_FOUND)
    target_link_libraries(motive_tests Qt6::Core)
endif()

# Add test
enable_testing()
add_test(NAME motive_unit_tests COMMAND motive_tests)

if(MOTIVE_ENABLE_INTEGRATION_TESTS)
    find_package(Python3 QUIET COMPONENTS Interpreter)

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME motive_rest_animation_physics
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_rest_animation_physics.py
                --editor ${MOTIVE_EDITOR_BIN}
                --project-dir /tmp/motive_rest_project_small
        )
        set_tests_properties(
            motive_rest_animation_physics
            PROPERTIES
                TIMEOUT 120
                LABELS "integration;rest"
        )

        if(NOT EXISTS "${MOTIVE_EDITOR_BIN}")
            message(STATUS "motive_editor not found at ${MOTIVE_EDITOR_BIN}; disabling integration test")
            set_tests_properties(motive_rest_animation_physics PROPERTIES DISABLED TRUE)
        endif()
    else()
        message(STATUS "Python3 interpreter not found; integration test disabled")
    endif()
endif()
