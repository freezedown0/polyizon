#pragma once

#include <QPoint>
#include <QWindow>

#include <entt/entt.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

class QWidget;

namespace polyizon {
class EditorViewportRenderer;
enum class GizmoMode;
}

// QWindow subclass hosting the embedded Vulkan viewport: constructs its
// EditorViewportRenderer on first expose (once QWindow::create() has run and
// winId() is a real native HWND), forwards resize/keyboard/mouse events to
// it, and is driven by MainWindow's QTimer via RenderIfExposed() — no
// Vulkan/Qt render thread yet, single-threaded on Qt's GUI thread for this
// phase (see the Phase 14 plan's "correctness first" note).
class VulkanViewportWindow : public QWindow {
    Q_OBJECT

public:
    VulkanViewportWindow();
    ~VulkanViewportWindow() override;

    // Called by MainWindow's QTimer (~16ms). No-op until the first
    // exposeEvent has constructed m_Renderer, and again whenever the window
    // isn't currently exposed (minimized/occluded) — mirrors
    // Swapchain::AcquireResult::NotReady's zero-extent guard on the GLFW path.
    void RenderIfExposed();

    // Called from MainWindow's File > New/Open Project handlers. m_Renderer
    // doesn't exist until this window's first exposeEvent has fired (it
    // needs a real HWND, which only exists after QWindow::create() has run —
    // see the header/class doc comment) — a request that arrives before
    // that point is stashed and applied right after construction instead of
    // being silently dropped.
    void LoadScene(const std::filesystem::path& sceneFile);

    // Phase 17: editor panels reach the live Scene/VulkanContext through
    // this — null until the first exposeEvent, same lifetime rule as
    // LoadScene()'s pending-path fallback above. Panels re-fetch this every
    // time they need it rather than caching it.
    polyizon::EditorViewportRenderer* GetRenderer() noexcept { return m_Renderer.get(); }

    // Phase 19: called from InspectorPanel/HierarchyPanel's mutation handlers
    // and MainWindow::OnSaveScene before they touch the scene/disk. Per the
    // user's explicit call: no locking of edits during Play/Pause, just a
    // one-time non-blocking heads-up (changes made now are discarded on
    // Stop's revert) so it isn't silently surprising — shown once per Play
    // session, not on every single edit, or it'd be pure noise. No-op
    // (returns false) if not currently Playing/Paused or no renderer exists
    // yet. dialogParent is typically the calling panel (`this`).
    bool MaybeWarnEditDuringPlay(QWidget* dialogParent);

    // Called from MainWindow::OnPlay() on a fresh Stopped->Playing
    // transition (not on a Paused->Playing resume, which is still the same
    // Play session) so the heads-up above can fire again for the next
    // session.
    void ResetEditWarning() noexcept { m_EditWarningShown = false; }

    // Phase 20: Move/Rotate viewport gizmos. Called from MainWindow whenever
    // the Hierarchy/Inspector selection changes (same EntitySelected signal
    // both panels already share) and from the Move/Rotate toolbar buttons —
    // both no-ops until m_Renderer exists (nothing to draw a gizmo into yet).
    void SetSelectedEntity(entt::entity entity);
    void SetGizmoMode(polyizon::GizmoMode mode);

signals:
    // Emitted once a scene has actually finished loading into m_Renderer —
    // either synchronously (LoadScene() called while m_Renderer already
    // exists) or deferred (exposeEvent() applying a pending path stashed
    // before the renderer existed). HierarchyPanel::Refresh() must run
    // after either case, not just the synchronous one, or a scene requested
    // before the window's first paint (e.g. immediately after launch) would
    // leave the Hierarchy panel silently empty.
    void SceneLoaded();

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
    // Set by LoadScene() when called before m_Renderer exists; applied and
    // cleared in exposeEvent right after construction.
    std::optional<std::filesystem::path> m_PendingScenePath;

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

    // See MaybeWarnEditDuringPlay()/ResetEditWarning() above.
    bool m_EditWarningShown = false;
};
