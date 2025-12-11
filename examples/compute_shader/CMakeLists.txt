cmake_minimum_required(VERSION 3.31)
project(ComputeShader LANGUAGES CXX C OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch MoltenVK from GitHub
include(FetchContent)
FetchContent_Declare(
    MoltenVK
    GIT_REPOSITORY https://github.com/KhronosGroup/MoltenVK.git
    GIT_TAG refs/tags/v1.4.1
)
FetchContent_MakeAvailable(MoltenVK)

# Fetch GLFW as well
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

add_executable(compute_shader 
    src/main.cpp
    $<$<PLATFORM_ID:Darwin>:src/macos_surface.mm>
)

# Link both
target_link_libraries(compute_shader PRIVATE glfw MoltenVK)
target_include_directories(compute_shader PRIVATE ${MoltenVK_SOURCE_DIR}/MoltenVK/include)
target_include_directories(compute_shader PRIVATE /opt/homebrew/include)

# Copy shader file to build directory
add_custom_command(TARGET compute_shader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:compute_shader>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/shaders/red.comp.spv
        $<TARGET_FILE_DIR:compute_shader>/shaders/red.comp.spv
    COMMENT "Copying shader files to build directory"
)
