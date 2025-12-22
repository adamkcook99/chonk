# Engine CMakeLists.txt

# Math library
add_library(engine_math
    math/vec3.cpp
    math/vec3.h
)

# Voxels library
add_library(engine_voxels
    voxels/tree27.cpp
    voxels/chunk.cpp
    voxels/chunk_array.cpp
    voxels/chunk_manager.cpp
    voxels/world_tree.cpp
    voxels/tree27.h
    voxels/chunk.h
    voxels/chunk_array.h
    voxels/chunk_manager.h
    voxels/world_tree.h
    voxels/world_constants.h
)

target_link_libraries(engine_voxels PRIVATE engine_math)

# ENet include directories (needed because chunk_manager includes network.h)
# Will be set up below after finding ENet

# Networking library
add_library(engine_networking
    networking/network.cpp
    networking/network.h
)

# Find ENet - try pkg-config first, then fallback to manual search
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(ENET QUIET enet)
endif()

# Find ENet include directories and library
# Try common homebrew paths first
find_path(ENET_INCLUDE_DIR enet/enet.h
    PATHS 
        /opt/homebrew/opt/enet/include
        /opt/homebrew/include
        /usr/local/include 
        /usr/include
    NO_DEFAULT_PATH
)

# If not found, try without NO_DEFAULT_PATH
if(NOT ENET_INCLUDE_DIR)
    find_path(ENET_INCLUDE_DIR enet/enet.h)
endif()

find_library(ENET_LIBRARY enet
    PATHS 
        /opt/homebrew/opt/enet/lib
        /opt/homebrew/lib
        /usr/local/lib 
        /usr/lib
    NO_DEFAULT_PATH
)

# If not found, try without NO_DEFAULT_PATH
if(NOT ENET_LIBRARY)
    find_library(ENET_LIBRARY enet)
endif()

# Also try pkg-config
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND AND NOT ENET_INCLUDE_DIR)
    pkg_check_modules(ENET QUIET enet)
    if(ENET_FOUND)
        set(ENET_INCLUDE_DIR ${ENET_INCLUDE_DIRS})
        set(ENET_LIBRARY ${ENET_LIBRARIES})
    endif()
endif()

if(ENET_INCLUDE_DIR AND ENET_LIBRARY)
    # Set up networking library
    target_include_directories(engine_networking PRIVATE ${ENET_INCLUDE_DIR})
    target_link_libraries(engine_networking PRIVATE ${ENET_LIBRARY})
    
    # Also make ENet includes available to voxels (since chunk_manager includes network.h)
    target_include_directories(engine_voxels PRIVATE ${ENET_INCLUDE_DIR})
    
    message(STATUS "Found ENet: ${ENET_INCLUDE_DIR}, Library: ${ENET_LIBRARY}")
else()
    message(WARNING "ENet not found. Networking will not work. Install enet via: brew install enet")
    message(STATUS "Searched for enet.h in: /opt/homebrew/opt/enet/include, /opt/homebrew/include, /usr/local/include, /usr/include")
    message(STATUS "Searched for libenet in: /opt/homebrew/opt/enet/lib, /opt/homebrew/lib, /usr/local/lib, /usr/lib")
endif()

# Graphics library
add_library(engine_graphics
    graphics/renderer.cpp
    graphics/renderer.h
    $<$<PLATFORM_ID:Darwin>:graphics/macos_surface.mm>
)

# Fetch MoltenVK
include(FetchContent)
FetchContent_Declare(
    MoltenVK
    GIT_REPOSITORY https://github.com/KhronosGroup/MoltenVK.git
    GIT_TAG refs/tags/v1.4.1
)
FetchContent_MakeAvailable(MoltenVK)

# Fetch GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

target_link_libraries(engine_graphics PRIVATE 
    glfw 
    MoltenVK
    engine_voxels
    engine_math
)

# Find Vulkan headers
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
    PATHS
        /opt/homebrew/include
        /opt/homebrew/opt/vulkan-headers/include
        /usr/local/include
        /usr/include
        ${VULKAN_SDK}/include
    NO_DEFAULT_PATH
)

if(NOT VULKAN_INCLUDE_DIR)
    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h)
endif()

if(VULKAN_INCLUDE_DIR)
    target_include_directories(engine_graphics PRIVATE 
        ${MoltenVK_SOURCE_DIR}/MoltenVK/include
        ${VULKAN_INCLUDE_DIR}
        /opt/homebrew/include
    )
    message(STATUS "Found Vulkan headers at: ${VULKAN_INCLUDE_DIR}")
else()
    target_include_directories(engine_graphics PRIVATE 
        ${MoltenVK_SOURCE_DIR}/MoltenVK/include
        /opt/homebrew/include
    )
    message(WARNING "Vulkan headers not found. Install vulkan-headers via: brew install vulkan-headers")
endif()

# Core library (utilities)
add_library(engine_core
    core/logging.cpp
    core/logging.h
)

# Combined engine library
add_library(engine INTERFACE)
target_link_libraries(engine INTERFACE
    engine_math
    engine_voxels
    engine_networking
    engine_graphics
    engine_core
)

target_include_directories(engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Propagate Vulkan includes through engine interface
# Use the same VULKAN_INCLUDE_DIR found above for engine_graphics
if(VULKAN_INCLUDE_DIR)
    target_include_directories(engine INTERFACE ${VULKAN_INCLUDE_DIR})
else()
    # Fallback - try to find it again
    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
        PATHS
            /opt/homebrew/include
            /opt/homebrew/opt/vulkan-headers/include
            /usr/local/include
            /usr/include
            ${VULKAN_SDK}/include
    )
    if(VULKAN_INCLUDE_DIR)
        target_include_directories(engine INTERFACE ${VULKAN_INCLUDE_DIR})
    else()
        # Last fallback
        target_include_directories(engine INTERFACE /opt/homebrew/include)
    endif()
endif()

