#include "polyizon/window.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace polyizon {

std::uint32_t Window::s_GLFWWindowCount = 0;

namespace {

void GLFWErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[GLFW Error %d] %s\n", error, description);
}

} // namespace

Window::Window(const WindowProps& props) {
    m_Data.title = props.title;
    m_Data.width = props.width;
    m_Data.height = props.height;

    if (s_GLFWWindowCount == 0) {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
        glfwSetErrorCallback(GLFWErrorCallback);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, props.resizable ? GLFW_TRUE : GLFW_FALSE);

    m_Handle = glfwCreateWindow(
        static_cast<int>(props.width),
        static_cast<int>(props.height),
        m_Data.title.c_str(),
        nullptr,
        nullptr);

    if (!m_Handle) {
        if (s_GLFWWindowCount == 0) {
            glfwTerminate();
        }
        throw std::runtime_error("Failed to create GLFW window");
    }

    ++s_GLFWWindowCount;

    glfwSetWindowUserPointer(m_Handle, &m_Data);
    InstallCallbacks();
}

Window::~Window() {
    Destroy();
}

Window::Window(Window&& other) noexcept
    : m_Handle(std::exchange(other.m_Handle, nullptr))
    , m_Data(std::move(other.m_Data)) {
    if (m_Handle) {
        glfwSetWindowUserPointer(m_Handle, &m_Data);
    }
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_Handle = std::exchange(other.m_Handle, nullptr);
        m_Data = std::move(other.m_Data);
        if (m_Handle) {
            glfwSetWindowUserPointer(m_Handle, &m_Data);
        }
    }
    return *this;
}

void Window::Destroy() {
    if (!m_Handle) {
        return;
    }

    glfwDestroyWindow(m_Handle);
    m_Handle = nullptr;

    --s_GLFWWindowCount;
    if (s_GLFWWindowCount == 0) {
        glfwTerminate();
    }
}

void Window::PollEvents() const {
    glfwPollEvents();
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_Handle) == GLFW_TRUE;
}

void Window::SetShouldClose(bool value) {
    glfwSetWindowShouldClose(m_Handle, value ? GLFW_TRUE : GLFW_FALSE);
}

void Window::SetResizeCallback(ResizeCallback callback) { m_Data.onResize = std::move(callback); }
void Window::SetCloseCallback(CloseCallback callback) { m_Data.onClose = std::move(callback); }
void Window::SetKeyCallback(KeyCallback callback) { m_Data.onKey = std::move(callback); }
void Window::SetMouseButtonCallback(MouseButtonCallback callback) { m_Data.onMouseButton = std::move(callback); }
void Window::SetCursorPosCallback(CursorPosCallback callback) { m_Data.onCursorPos = std::move(callback); }
void Window::SetScrollCallback(ScrollCallback callback) { m_Data.onScroll = std::move(callback); }
void Window::SetFocusCallback(FocusCallback callback) { m_Data.onFocus = std::move(callback); }
void Window::SetCursorEnterCallback(CursorEnterCallback callback) { m_Data.onCursorEnter = std::move(callback); }

void Window::SetCursorCaptured(bool captured) {
    glfwSetInputMode(m_Handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

bool Window::IsCursorCaptured() const {
    return glfwGetInputMode(m_Handle, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
}

void Window::InstallCallbacks() {
    glfwSetFramebufferSizeCallback(m_Handle, [](GLFWwindow* window, int width, int height) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        data.width = static_cast<std::uint32_t>(width);
        data.height = static_cast<std::uint32_t>(height);
        if (data.onResize) {
            data.onResize(data.width, data.height);
        }
    });

    glfwSetWindowCloseCallback(m_Handle, [](GLFWwindow* window) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onClose) {
            data.onClose();
        }
    });

    glfwSetKeyCallback(m_Handle, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onKey) {
            data.onKey(key, scancode, action, mods);
        }
    });

    glfwSetMouseButtonCallback(m_Handle, [](GLFWwindow* window, int button, int action, int mods) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onMouseButton) {
            data.onMouseButton(button, action, mods);
        }
    });

    glfwSetCursorPosCallback(m_Handle, [](GLFWwindow* window, double x, double y) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onCursorPos) {
            data.onCursorPos(x, y);
        }
    });

    glfwSetScrollCallback(m_Handle, [](GLFWwindow* window, double xOffset, double yOffset) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onScroll) {
            data.onScroll(xOffset, yOffset);
        }
    });

    glfwSetWindowFocusCallback(m_Handle, [](GLFWwindow* window, int focused) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onFocus) {
            data.onFocus(focused);
        }
    });

    glfwSetCursorEnterCallback(m_Handle, [](GLFWwindow* window, int entered) {
        auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        if (data.onCursorEnter) {
            data.onCursorEnter(entered);
        }
    });
}

} // namespace polyizon
