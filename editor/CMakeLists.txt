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
        src/editor_theme.cpp
        src/component_icons.cpp
        src/main_window.cpp
        src/vulkan_viewport_window.cpp
        src/editor_viewport_renderer.cpp
        src/project.cpp
        src/game_builder.cpp
        src/hierarchy_panel.cpp
        src/inspector_panel.cpp
        src/content_browser_panel.cpp
        src/console_panel.cpp
        src/lighting_settings_dialog.cpp
)

# Dependencies. assimp/Lua/sol2/nlohmann_json are no longer linked directly
# here — they come in transitively via polyizon_core, which PUBLICly links
# them now that ScriptEngine/SceneSerializer/mesh_import live there (Phase
# 20) — project.cpp's own direct nlohmann::json usage still resolves fine
# through that transitive include path.
target_link_libraries(polyizon_editor
    PRIVATE
        polyizon_core
        Qt6::Widgets
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
polyizon_add_app_icon(polyizon_editor "${CMAKE_SOURCE_DIR}/temporaryassets/Mark1.ico")

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

# Temporary PNG brand assets exported from Figma. Keep these external to the
# executable so they can be replaced without touching editor code while the
# final multi-resolution Windows icon package is being prepared.
add_custom_command(TARGET polyizon_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:polyizon_editor>/branding"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/temporaryassets/Mark.png"
        "$<TARGET_FILE_DIR:polyizon_editor>/branding/PolyizonMark.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/temporaryassets/Horizontal Lockup.png"
        "$<TARGET_FILE_DIR:polyizon_editor>/branding/PolyizonHorizontalLockup.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/temporaryassets/Wordmark.png"
        "$<TARGET_FILE_DIR:polyizon_editor>/branding/PolyizonWordmark.png"
    COMMENT "Deploying temporary Polyizon brand assets"
)
