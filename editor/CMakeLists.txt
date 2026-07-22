# Editor GUI Application Target

# Qt6 is this target's own dependency, not core/game's — scoped here rather
# than cmake/DependencyManagement.cmake (see that file's comment) so
# configuring the GLFW-only game client never requires Qt6 to be installed.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)

# The QPA Windows platform plugin (qwindows[d].dll): loaded dynamically at
# runtime by QGuiApplication, not linked via the import table, so vcpkg's
# applocal-deps DLL copying (which already handles Qt6Widgets(d)/Qt6Gui(d)/
# etc. — see the plugin-copy step below) can't discover it automatically.
# Without it deployed to <exe-dir>/platforms/, the app aborts immediately
# with "This application failed to start because no Qt platform plugin
# could be initialized." find_package'd as its own component (Qt6Gui
# exports it as a separate optional CMake package) purely to get its
# resolved IMPORTED_LOCATION_DEBUG/RELEASE path below — never linked.
find_package(Qt6QWindowsIntegrationPlugin CONFIG REQUIRED PATHS "${Qt6Gui_DIR}")

# Assimp is this target's own dependency too, not core/game's — same
# reasoning/scoping as Qt6 above (see mesh_import.hpp): only editor/
# imports .obj/.fbx files, so the GLFW-only game client's configure never
# needs Assimp installed either.
find_package(assimp CONFIG REQUIRED)

add_executable(polyizon_editor)

# Required for QObject-derived classes (MainWindow's Q_OBJECT) to have their
# moc-generated code compiled in automatically.
set_target_properties(polyizon_editor PROPERTIES AUTOMOC ON)

# Include directories
target_include_directories(polyizon_editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Source files
target_sources(polyizon_editor
    PRIVATE
        src/main.cpp
        src/main_window.cpp
        src/vulkan_viewport_window.cpp
        src/editor_viewport_renderer.cpp
        src/mesh_import.cpp

        # Not yet implemented
        # src/inspector.cpp
        # src/hierarchy.cpp
        # src/profiler.cpp
)

# Dependencies
target_link_libraries(polyizon_editor
    PRIVATE
        polyizon_core
        Qt6::Widgets
        Vulkan::Vulkan
        assimp::assimp
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

# Deploy the QPA Windows platform plugin next to the executable (see the
# find_package(Qt6QWindowsIntegrationPlugin) comment above for why this is
# needed at all).
add_custom_command(TARGET polyizon_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:polyizon_editor>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
        "$<TARGET_FILE_DIR:polyizon_editor>/platforms/"
    COMMENT "Deploying Qt platform plugin (qwindows.dll)"
)
