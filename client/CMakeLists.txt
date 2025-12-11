# Client CMakeLists.txt

# Find glslangValidator for shader compilation
find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslang
    PATHS
        /usr/bin
        /usr/local/bin
        /opt/homebrew/bin
        ${VULKAN_SDK}/bin
    DOC "glslangValidator shader compiler"
)

# Compile compute shaders
set(RAYTRACE_SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/raytrace.comp)
set(RAYTRACE_SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/raytrace.comp.spv)

set(TEXT_OVERLAY_SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/text_overlay.comp)
set(TEXT_OVERLAY_SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/text_overlay.comp.spv)

# Create shaders directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

# Custom command to compile raytrace shader
if(GLSLANG_VALIDATOR)
    add_custom_command(
        OUTPUT ${RAYTRACE_SHADER_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${RAYTRACE_SHADER_SOURCE} -o ${RAYTRACE_SHADER_OUTPUT}
        DEPENDS ${RAYTRACE_SHADER_SOURCE}
        COMMENT "Compiling compute shader: raytrace.comp"
    )
    
    add_custom_command(
        OUTPUT ${TEXT_OVERLAY_SHADER_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${TEXT_OVERLAY_SHADER_SOURCE} -o ${TEXT_OVERLAY_SHADER_OUTPUT}
        DEPENDS ${TEXT_OVERLAY_SHADER_SOURCE}
        COMMENT "Compiling compute shader: text_overlay.comp"
    )
else()
    message(WARNING "glslangValidator not found. Shader compilation will be skipped.")
    # Create empty files as placeholders
    add_custom_command(
        OUTPUT ${RAYTRACE_SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E touch ${RAYTRACE_SHADER_OUTPUT}
        COMMENT "Creating placeholder shader (glslangValidator not found)"
    )
    add_custom_command(
        OUTPUT ${TEXT_OVERLAY_SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E touch ${TEXT_OVERLAY_SHADER_OUTPUT}
        COMMENT "Creating placeholder shader (glslangValidator not found)"
    )
endif()

add_executable(chonk_client
    main.cpp
    ${RAYTRACE_SHADER_OUTPUT}  # This makes the executable depend on the shader
    ${TEXT_OVERLAY_SHADER_OUTPUT}
    $<$<PLATFORM_ID:Darwin>:../engine/graphics/macos_surface.mm>
)

target_link_libraries(chonk_client PRIVATE engine game)

# Vulkan headers should be propagated through engine interface library
# But if needed, we can add them here too

# Copy shader files to build directory (POST_BUILD doesn't support DEPENDS, 
# but since ${SHADER_OUTPUT} is in the executable sources, it will be built first)
add_custom_command(TARGET chonk_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:chonk_client>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${RAYTRACE_SHADER_OUTPUT}
        $<TARGET_FILE_DIR:chonk_client>/shaders/raytrace.comp.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TEXT_OVERLAY_SHADER_OUTPUT}
        $<TARGET_FILE_DIR:chonk_client>/shaders/text_overlay.comp.spv
    COMMENT "Copying shader files to build directory"
)

