cmake_minimum_required(VERSION 3.31)
project(VulkanCube LANGUAGES CXX C OBJC OBJCXX)

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

add_executable(vulkan_cube 
    src/main.cpp
    $<$<PLATFORM_ID:Darwin>:src/macos_surface.mm>
)

# Link both
target_link_libraries(vulkan_cube PRIVATE glfw MoltenVK)
target_include_directories(vulkan_cube PRIVATE ${MoltenVK_SOURCE_DIR}/MoltenVK/include)
target_include_directories(vulkan_cube PRIVATE /opt/homebrew/include)
