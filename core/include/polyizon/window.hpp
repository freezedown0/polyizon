#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;

namespace polyizon {

struct WindowProps {
    std::string title = "Polyizon";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
};

// RAII wrapper around a single GLFW window. Vulkan-only: no client API is
// attached, so there is no SwapBuffers() — the renderer owns presentation.
class Window {
public:
    using ResizeCallback = std::function<void(std::uint32_t width, std::uint32_t height)>;
    using CloseCallback = std::function<void()>;
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using CursorPosCallback = std::function<void(double x, double y)>;
    using ScrollCallback = std::function<void(double xOffset, double yOffset)>;

    explicit Window(const WindowProps& props = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    void PollEvents() const;

    bool ShouldClose() const;
    void SetShouldClose(bool value);

    std::uint32_t GetWidth() const noexcept { return m_Data.width; }
    std::uint32_t GetHeight() const noexcept { return m_Data.height; }
    GLFWwindow* GetNativeHandle() const noexcept { return m_Handle; }

    void SetResizeCallback(ResizeCallback callback);
    void SetCloseCallback(CloseCallback callback);
    void SetKeyCallback(KeyCallback callback);
    void SetMouseButtonCallback(MouseButtonCallback callback);
    void SetCursorPosCallback(CursorPosCallback callback);
    void SetScrollCallback(ScrollCallback callback);

private:
    // Lives at a stable address (glfwSetWindowUserPointer) so GLFW's static
    // C callbacks can reach back into the owning Window's std::function state.
    struct WindowData {
        std::string title;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        ResizeCallback onResize;
        CloseCallback onClose;
        KeyCallback onKey;
        MouseButtonCallback onMouseButton;
        CursorPosCallback onCursorPos;
        ScrollCallback onScroll;
    };

    void InstallCallbacks();
    void Destroy();

    GLFWwindow* m_Handle = nullptr;
    WindowData m_Data;

    static std::uint32_t s_GLFWWindowCount;
};

} // namespace polyizon
