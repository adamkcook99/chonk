cmake_minimum_required(VERSION 3.31)

# Unit tests executable
add_executable(unit_tests
    main.cpp
    test_framework.h
    test_svo.cpp
    test_chunk.cpp
)

# Link against engine libraries
target_link_libraries(unit_tests
    engine_core
    engine_math
    engine_voxels
    game
)

# Include directories
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/game
)

# Set C++ standard
set_target_properties(unit_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

