# External dependencies configuration
#
# All of these (except Vulkan itself) are resolved via vcpkg. Target names
# below match exactly what each port's installed *-config.cmake exports —
# confirmed against the "provides CMake targets" usage text vcpkg prints
# after `vcpkg install`, not guessed.

# Vulkan (1.3+) — resolved via CMake's bundled FindVulkan module against the
# LunarG SDK (VULKAN_SDK env var), independent of vcpkg.
find_package(Vulkan 1.3 REQUIRED)

# GLFW3 -> target: glfw
find_package(glfw3 CONFIG REQUIRED)

# GLM (math library) -> target: glm::glm
find_package(glm CONFIG REQUIRED)

# EnTT (ECS library) -> target: EnTT::EnTT
find_package(EnTT CONFIG REQUIRED)

# Dear ImGui -> target: imgui::imgui
find_package(imgui CONFIG REQUIRED)

# Volk (Vulkan loader) -> targets: volk::volk, volk::volk_headers
find_package(volk CONFIG REQUIRED)

# VMA (Vulkan Memory Allocator) -> target: GPUOpen::VulkanMemoryAllocator
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# stb (single-header image loading) -> vcpkg's stb port is header-only and
# exposes itself via find_package(Stb REQUIRED) + the Stb_INCLUDE_DIR
# variable, NOT a linkable target — confirmed by installing the port and
# reading its own printed usage instructions, same discipline as every
# other dependency here.
find_package(Stb REQUIRED)
