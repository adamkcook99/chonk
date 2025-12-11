cmake_minimum_required(VERSION 3.31)
project(CHONK LANGUAGES CXX C OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add subdirectories
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(unit_tests)
