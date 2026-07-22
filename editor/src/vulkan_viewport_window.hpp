#pragma once

#include <QPoint>
#include <QWindow>

#include <chrono>
#include <cstdint>
#include <memory>

namespace polyizon {
class EditorViewportRenderer;
}

// QWindow subclass hosting the embedded Vulkan viewport: constructs its
// EditorViewportRenderer on first expose (once QWindow::create() has run and
// winId() is a real native HWND), forwards resize/keyboard/mouse events to
// it, and is driven by MainWindow's QTimer via RenderIfExposed() — no
// Vulkan/Qt render thread yet, single-threaded on Qt's GUI thread for this
// phase (see the Phase 14 plan's "correctness first" note).
class VulkanViewportWindow : public QWindow {
public:
    VulkanViewportWindow();
    ~VulkanViewportWindow() override;

    // Called by MainWindow's QTimer (~16ms). No-op until the first
    // exposeEvent has constructed m_Renderer, and again whenever the window
    // isn't currently exposed (minimized/occluded) — mirrors
    // Swapchain::AcquireResult::NotReady's zero-extent guard on the GLFW path.
    void RenderIfExposed();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void SetMovementKeyState(int qtKey, bool pressed);

    std::unique_ptr<polyizon::EditorViewportRenderer> m_Renderer;

    // Held-key state for Camera::ProcessKeyboard's GLFW-agnostic overload —
    // maintained from keyPressEvent/keyReleaseEvent instead of GLFW's
    // per-frame glfwGetKey() polling (see camera.hpp).
    bool m_MoveForward = false;
    bool m_MoveBackward = false;
    bool m_MoveLeft = false;
    bool m_MoveRight = false;
    bool m_MoveUp = false;
    bool m_MoveDown = false;

    // Hold-right-mouse-button-and-drag look, rather than GLFW's
    // hide-and-unbound-cursor scheme (Application::SetCursorCaptured) — Qt
    // has no direct equivalent this phase needs to reproduce; a simple
    // held-button delta is enough to prove the embedding works.
    bool m_MouseCaptured = false;
    QPointF m_LastMousePos;

    std::chrono::steady_clock::time_point m_LastFrameTime;
};
