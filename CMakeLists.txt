cmake_minimum_required(VERSION 3.25)
project(
    Polyizon
    VERSION 0.1.0
    DESCRIPTION "High-performance 3D graphics engine on C++20 and Vulkan 1.3+"
    LANGUAGES CXX
)

# C++ Standard Configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Global project settings
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Configuration options
option(POLYIZON_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)
option(POLYIZON_BUILD_EDITOR "Build ImGui editor" ON)
option(POLYIZON_BUILD_GAME "Build game executable" ON)
option(POLYIZON_ENABLE_PROFILING "Enable performance profiling" ON)

# Include compiler configuration
include(CompilerFlags)

# Include dependency management
include(DependencyManagement)

# Add sub-projects
add_subdirectory(core)

if(POLYIZON_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(POLYIZON_BUILD_GAME)
    add_subdirectory(game)
endif()

# Installation rules (optional, for later)
# install(TARGETS polyizon_core polyizon_editor polyizon_game)
