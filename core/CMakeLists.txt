# Core Engine Library Target

add_library(polyizon_core STATIC)

# Public include directory
target_include_directories(polyizon_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Stb_INCLUDE_DIR}
)

# Source files
target_sources(polyizon_core
    PRIVATE
        # Application/window lifecycle
        src/window.cpp
        src/application.cpp
        src/camera.cpp

        # Vulkan abstraction layer
        src/vulkan/context.cpp
        src/vulkan/swapchain.cpp
        src/vulkan/pipeline.cpp
        src/vulkan/buffer.cpp
        src/vulkan/image.cpp

        # Rendering subsystem (not yet implemented)
        # src/rendering/renderer.cpp
        # src/rendering/voxel_shadow.cpp
        # src/rendering/render_graph.cpp

        # ECS subsystem (not yet implemented)
        # src/ecs/registry.cpp
        # src/ecs/systems.cpp
)

# Public dependencies
target_link_libraries(polyizon_core
    PUBLIC
        glfw
        Vulkan::Vulkan
        glm::glm
        EnTT::EnTT
        # volk::volk (the precompiled static lib) is deliberately NOT linked:
        # it reliably crashed inside its own genload code on this machine even
        # though the Vulkan driver/loader and GLFW's loader path both work
        # fine (verified by calling vkCreateInstance directly). Only
        # volk_headers is used; context.cpp compiles volk from source itself
        # via VOLK_IMPLEMENTATION, guaranteeing header/impl consistency.
        volk::volk_headers
        GPUOpen::VulkanMemoryAllocator
)

# Compiler features
target_compile_features(polyizon_core PUBLIC cxx_std_20)

# Validation layers macro (if enabled)
if(POLYIZON_ENABLE_VALIDATION)
    target_compile_definitions(polyizon_core PUBLIC POLYIZON_ENABLE_VALIDATION)
endif()

if(POLYIZON_ENABLE_PROFILING)
    target_compile_definitions(polyizon_core PUBLIC POLYIZON_ENABLE_PROFILING)
endif()

# Vulkan uses a [0,1] clip-space depth range and Y-down NDC; GLM defaults to
# OpenGL conventions ([-1,1] depth). Set as compiler-level defines rather
# than a header macro so every translation unit gets the correct value
# regardless of which header includes <glm/...> first — a header-based
# approach is fragile since GLM's headers are individually header-guarded,
# so whichever TU includes glm/glm.hpp earliest would silently "win" with no
# compile error otherwise.
target_compile_definitions(polyizon_core PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
)

# --- Shader compilation (GLSL -> SPIR-V) ---
# Shaders live in core/shaders/ (parallel to include/ and src/). Compiled by
# glslc into <build>/bin/shaders — the same directory game/CMakeLists.txt
# and editor/CMakeLists.txt place their executables in — so GraphicsPipeline
# can find them relative to the running executable's own directory,
# regardless of process CWD.
set(POLYIZON_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(POLYIZON_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
set(POLYIZON_SHADER_SOURCES
    ${POLYIZON_SHADER_SOURCE_DIR}/triangle.vert
    ${POLYIZON_SHADER_SOURCE_DIR}/triangle.frag
)

set(POLYIZON_SHADER_OUTPUTS "")
foreach(shader_source ${POLYIZON_SHADER_SOURCES})
    get_filename_component(shader_name ${shader_source} NAME)
    set(shader_output "${POLYIZON_SHADER_OUTPUT_DIR}/${shader_name}.spv")

    add_custom_command(
        OUTPUT ${shader_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${POLYIZON_SHADER_OUTPUT_DIR}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${shader_source} -o ${shader_output}
        DEPENDS ${shader_source}
        COMMENT "Compiling shader ${shader_name} -> SPIR-V"
        VERBATIM
    )
    list(APPEND POLYIZON_SHADER_OUTPUTS ${shader_output})
endforeach()

add_custom_target(polyizon_shaders DEPENDS ${POLYIZON_SHADER_OUTPUTS})
add_dependencies(polyizon_core polyizon_shaders)

# --- Texture asset copying ---
# Textures live in core/textures/ (parallel to shaders/). Copied verbatim
# into <build>/bin/textures — same directory both executables run from —
# so Image::CreateFromFile can find them relative to the running
# executable's own directory, mirroring the shader lookup above.
set(POLYIZON_TEXTURE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/textures")
set(POLYIZON_TEXTURE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/textures")
set(POLYIZON_TEXTURE_SOURCES
    ${POLYIZON_TEXTURE_SOURCE_DIR}/checkerboard.png
)

set(POLYIZON_TEXTURE_OUTPUTS "")
foreach(texture_source ${POLYIZON_TEXTURE_SOURCES})
    get_filename_component(texture_name ${texture_source} NAME)
    set(texture_output "${POLYIZON_TEXTURE_OUTPUT_DIR}/${texture_name}")

    add_custom_command(
        OUTPUT ${texture_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${POLYIZON_TEXTURE_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${texture_source} ${texture_output}
        DEPENDS ${texture_source}
        COMMENT "Copying texture ${texture_name}"
        VERBATIM
    )
    list(APPEND POLYIZON_TEXTURE_OUTPUTS ${texture_output})
endforeach()

add_custom_target(polyizon_textures DEPENDS ${POLYIZON_TEXTURE_OUTPUTS})
add_dependencies(polyizon_core polyizon_textures)
