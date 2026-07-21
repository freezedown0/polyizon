# Editor GUI Application Target

add_executable(polyizon_editor)

# Include directories
target_include_directories(polyizon_editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Source files
target_sources(polyizon_editor
    PRIVATE
        src/main.cpp

        # Not yet implemented
        # src/editor.cpp
        # src/viewport.cpp
        # src/inspector.cpp
        # src/hierarchy.cpp
        # src/profiler.cpp
)

# Dependencies
target_link_libraries(polyizon_editor
    PRIVATE
        polyizon_core
        glfw
        imgui::imgui
        Vulkan::Vulkan
)

# C++ standard
target_compile_features(polyizon_editor PRIVATE cxx_std_20)

# Set output directory
set_target_properties(polyizon_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Application icon (window/taskbar/Explorer)
include(AppIcon)
polyizon_add_app_icon(polyizon_editor "${CMAKE_SOURCE_DIR}/resources/Polyizon.ico")
