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
        # Vendored imgui_impl_vulkan (see target_sources below for why).
        ${CMAKE_SOURCE_DIR}/third_party/imgui_impl_vulkan
)

# Source files
target_sources(polyizon_core
    PRIVATE
        # Application/window lifecycle
        src/window.cpp
        src/application.cpp
        src/camera.cpp
        src/noise.cpp

        # Vulkan abstraction layer
        src/vulkan/context.cpp
        src/vulkan/swapchain.cpp
        src/vulkan/pipeline.cpp
        src/vulkan/sky_pipeline.cpp
        src/vulkan/classic_sky_pipeline.cpp
        src/vulkan/lit_pipeline.cpp
        src/vulkan/voxel_lit_pipeline.cpp
        src/vulkan/shadow_pipeline.cpp
        src/vulkan/buffer.cpp
        src/vulkan/image.cpp
        src/vulkan/texture3d.cpp
        src/vulkan/mesh.cpp
        src/vulkan/shadow_map.cpp

        # Vendored imgui_impl_vulkan.cpp (see third_party/imgui_impl_vulkan/):
        # vcpkg's imgui port is installed WITHOUT its vulkan-binding feature
        # on purpose. That prebuilt backend calls plain <vulkan/vulkan.h>
        # functions directly, which collide at link time with this project's
        # volk setup (context.cpp's VOLK_IMPLEMENTATION defines global
        # PFN_vk*-typed variables using the exact same names as the real
        # Vulkan functions — vkCreateInstance, vkCreateDescriptorPool, etc.
        # With both linked into one binary, the linker resolves the prebuilt
        # backend's direct function calls against our volk data variables
        # instead of the real vulkan-1.dll exports, which crashes immediately
        # (confirmed: the linked .exe imported zero functions from
        # vulkan-1.dll, and the backend's first real Vulkan call crashed with
        # an access violation, deterministically, at Application::InitImGui).
        # The fix is imgui's own documented interop path (IMGUI_IMPL_VULKAN_USE_VOLK,
        # see imgui_impl_vulkan.h's "Convenience support for Volk" section):
        # compile the backend ourselves with that define so it goes through
        # <volk.h> instead, sharing our exact same global function pointers —
        # no separate/duplicate/colliding symbol names.
        ${CMAKE_SOURCE_DIR}/third_party/imgui_impl_vulkan/imgui_impl_vulkan.cpp

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
        # imgui::imgui bundles the compiled glfw backend (installed with the
        # glfw-binding vcpkg feature). The vulkan backend is deliberately
        # NOT installed via vcpkg's vulkan-binding feature — see the
        # target_sources comment above for why it's vendored/compiled
        # ourselves instead.
        imgui::imgui
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

# Makes both the vendored imgui_impl_vulkan.cpp and our own code's inclusion
# of its header go through <volk.h> instead of <vulkan/vulkan.h> — see the
# target_sources comment above for why this must be consistent project-wide.
target_compile_definitions(polyizon_core PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)

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
    ${POLYIZON_SHADER_SOURCE_DIR}/sky.vert
    ${POLYIZON_SHADER_SOURCE_DIR}/sky.frag
    ${POLYIZON_SHADER_SOURCE_DIR}/classic_sky.frag
    ${POLYIZON_SHADER_SOURCE_DIR}/lit.vert
    ${POLYIZON_SHADER_SOURCE_DIR}/lit.frag
    ${POLYIZON_SHADER_SOURCE_DIR}/voxel_lit.frag
    ${POLYIZON_SHADER_SOURCE_DIR}/shadow.vert
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

# --- 3D model asset copying ---
# Models live in core/models/ (parallel to shaders/textures/). Copied
# verbatim into <build>/bin/models — same directory both executables run
# from — mirroring the texture-copy pattern above exactly. Only the editor
# target loads these this phase (see editor/src/mesh_import.hpp), but models
# are asset content like shaders/textures, not editor-specific code, so they
# live and get copied alongside them here rather than from editor/CMakeLists.txt.
set(POLYIZON_MODEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models")
set(POLYIZON_MODEL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/models")
set(POLYIZON_MODEL_SOURCES
    ${POLYIZON_MODEL_SOURCE_DIR}/plane.obj
    ${POLYIZON_MODEL_SOURCE_DIR}/cube.obj
)

set(POLYIZON_MODEL_OUTPUTS "")
foreach(model_source ${POLYIZON_MODEL_SOURCES})
    get_filename_component(model_name ${model_source} NAME)
    set(model_output "${POLYIZON_MODEL_OUTPUT_DIR}/${model_name}")

    add_custom_command(
        OUTPUT ${model_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${POLYIZON_MODEL_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${model_source} ${model_output}
        DEPENDS ${model_source}
        COMMENT "Copying model ${model_name}"
        VERBATIM
    )
    list(APPEND POLYIZON_MODEL_OUTPUTS ${model_output})
endforeach()

add_custom_target(polyizon_models DEPENDS ${POLYIZON_MODEL_OUTPUTS})
add_dependencies(polyizon_core polyizon_models)

# --- Lua script asset copying ---
# Built-in/sample scripts live in core/scripts/ (parallel to models/shaders/
# textures/), copied verbatim into <build>/bin/scripts — mirroring the
# model-copy pattern above exactly. Same "asset content, not editor-specific
# code" reasoning as models: only the editor's ScriptEngine (see
# editor/src/script_engine.hpp) loads these this phase, but they live here
# alongside the other asset kinds rather than under editor/.
set(POLYIZON_SCRIPT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(POLYIZON_SCRIPT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/scripts")
set(POLYIZON_SCRIPT_SOURCES
    ${POLYIZON_SCRIPT_SOURCE_DIR}/spin.lua
)

set(POLYIZON_SCRIPT_OUTPUTS "")
foreach(script_source ${POLYIZON_SCRIPT_SOURCES})
    get_filename_component(script_name ${script_source} NAME)
    set(script_output "${POLYIZON_SCRIPT_OUTPUT_DIR}/${script_name}")

    add_custom_command(
        OUTPUT ${script_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${POLYIZON_SCRIPT_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${script_source} ${script_output}
        DEPENDS ${script_source}
        COMMENT "Copying script ${script_name}"
        VERBATIM
    )
    list(APPEND POLYIZON_SCRIPT_OUTPUTS ${script_output})
endforeach()

add_custom_target(polyizon_scripts DEPENDS ${POLYIZON_SCRIPT_OUTPUTS})
add_dependencies(polyizon_core polyizon_scripts)
