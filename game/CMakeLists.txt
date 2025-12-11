# Game CMakeLists.txt

add_library(game
    world/world_generator.cpp
    world/world_generator.h
)

target_link_libraries(game PRIVATE engine)

target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

