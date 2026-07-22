#include "vulkan_viewport_window.hpp"

#include "editor_viewport_renderer.hpp"

// Only used to resolve winId()'s raw HWND and this process's HINSTANCE for
// EditorViewportRenderer's Win32-facing constructor (see
// VulkanContext(HWND, HINSTANCE, ...)) — no Vulkan/Windows API calls happen
// directly in this file beyond that.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <QExposeEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

VulkanViewportWindow::VulkanViewportWindow() {
    // Tells Qt's platform integration not to attach its own backing store
    // (QBackingStore/GDI) over this HWND, even though this project never
    // touches QVulkanInstance/QRhi — same trick QOpenGLWindow uses for its
    // own surface type. Vulkan presentation is handled entirely by
    // EditorViewportRenderer's own VulkanContext/Swapchain.
    setSurfaceType(QWindow::VulkanSurface);
}

VulkanViewportWindow::~VulkanViewportWindow() = default;

void VulkanViewportWindow::exposeEvent(QExposeEvent* event) {
    QWindow::exposeEvent(event);

    if (isExposed() && !m_Renderer) {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const HINSTANCE hinstance = GetModuleHandle(nullptr);
        m_Renderer = std::make_unique<polyizon::EditorViewportRenderer>(
            hwnd, hinstance, static_cast<std::uint32_t>(width()), static_cast<std::uint32_t>(height()));
        m_LastFrameTime = std::chrono::steady_clock::now();

        // Apply whatever scene was requested (via LoadScene()) before the
        // renderer existed to construct against.
        if (m_PendingScenePath.has_value()) {
            m_Renderer->LoadScene(*m_PendingScenePath);
            m_PendingScenePath.reset();
            emit SceneLoaded();
        }
    }
}

void VulkanViewportWindow::LoadScene(const std::filesystem::path& sceneFile) {
    if (m_Renderer) {
        m_Renderer->LoadScene(sceneFile);
        emit SceneLoaded();
    } else {
        m_PendingScenePath = sceneFile;
    }
}

void VulkanViewportWindow::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
    if (m_Renderer) {
        m_Renderer->Resize(static_cast<std::uint32_t>(width()), static_cast<std::uint32_t>(height()));
    }
}

void VulkanViewportWindow::RenderIfExposed() {
    if (!isExposed() || !m_Renderer) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - m_LastFrameTime).count();
    m_LastFrameTime = now;

    m_Renderer->UpdateCamera(m_MoveForward, m_MoveBackward, m_MoveLeft, m_MoveRight, m_MoveUp, m_MoveDown, deltaTime);
    m_Renderer->RenderFrame();
}

void VulkanViewportWindow::SetMovementKeyState(int qtKey, bool pressed) {
    switch (qtKey) {
        case Qt::Key_W: m_MoveForward = pressed; break;
        case Qt::Key_S: m_MoveBackward = pressed; break;
        case Qt::Key_A: m_MoveLeft = pressed; break;
        case Qt::Key_D: m_MoveRight = pressed; break;
        case Qt::Key_Space: m_MoveUp = pressed; break;
        case Qt::Key_Shift: m_MoveDown = pressed; break;
        default: break;
    }
}

void VulkanViewportWindow::keyPressEvent(QKeyEvent* event) {
    // Qt auto-repeats key-press events for a held key (GLFW instead reports
    // a real GLFW_PRESS/GLFW_REPEAT/GLFW_RELEASE stream polled directly via
    // glfwGetKey — see camera.hpp); auto-repeat presses would otherwise be
    // harmless no-ops here (state is already true), but skipping them keeps
    // this handler's intent explicit.
    if (!event->isAutoRepeat()) {
        SetMovementKeyState(event->key(), true);
    }
}

void VulkanViewportWindow::keyReleaseEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        SetMovementKeyState(event->key(), false);
    }
}

void VulkanViewportWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        m_MouseCaptured = true;
        m_LastMousePos = event->position();
    }
}

void VulkanViewportWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        m_MouseCaptured = false;
    }
}

void VulkanViewportWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!m_MouseCaptured || !m_Renderer) {
        return;
    }

    const QPointF delta = event->position() - m_LastMousePos;
    m_LastMousePos = event->position();

    // Y inverted, same convention as Application::OnCursorPos: "mouse up"
    // means "look up".
    m_Renderer->GetCamera().ProcessMouseMovement(static_cast<float>(delta.x()), static_cast<float>(-delta.y()));
}
