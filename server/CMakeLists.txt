# Server CMakeLists.txt

add_executable(chonk_server
    main.cpp
)

target_link_libraries(chonk_server PRIVATE engine game)

