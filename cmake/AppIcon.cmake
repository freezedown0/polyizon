# Embeds an .ico as a Win32 target's window/taskbar/Explorer icon.
#
# GLFW's Win32 backend (win32_window.c) loads an icon resource named exactly
# "GLFW_ICON" for the window class icon, falling back to a generic default
# if absent. Embedding it as a linked resource is the correct approach here
# rather than glfwSetWindowIcon, which only accepts raw pixel data
# (GLFWimage) and wouldn't affect the .exe's own file icon in Explorer.
function(polyizon_add_app_icon target icon_path)
    if(NOT WIN32)
        return()
    endif()

    file(TO_CMAKE_PATH "${icon_path}" POLYIZON_ICON_PATH)
    set(generated_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_app_icon.rc")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/app_icon.rc.in"
        "${generated_rc}"
        @ONLY
    )
    target_sources(${target} PRIVATE "${generated_rc}")
endfunction()
