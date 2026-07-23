#pragma once

#include "project.hpp"

#include "polyizon/scene/lighting_settings.hpp"

#include <QMainWindow>

#include <filesystem>
#include <memory>
#include <optional>

class QAction;
class QTimer;
class ConsolePanel;
class ContentBrowserPanel;
class HierarchyPanel;
class InspectorPanel;
class VulkanViewportWindow;

// Editor's top-level window: hosts VulkanViewportWindow as the central
// widget via QWidget::createWindowContainer, plus four dock panels around
// it — Hierarchy (left), Inspector (right), Content Browser + Console
// (tabbed, bottom) — the standard Unity/Unreal/Godot editor layout.
//
// Phase 16 gave this a File menu (New/Open Project) — the viewport starts
// empty (see EditorViewportRenderer) until one of those loads a project's
// default scene into it. Phase 17 adds the panels above plus a Save Scene
// action (the panels' edits are otherwise never written back to disk) and
// generalizes scene-loading so Content Browser can switch to a different
// scene file within the same project, not just the project's default one.
// Phase 18 adds a Lighting menu (Realistic/Voxel) that toggles the loaded
// scene's SceneLightingSettings::mode directly — a live in-memory switch,
// persisted only through the existing Save Scene action.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void OnNewProject();
    void OnOpenProject();
    void OnSaveScene();

    // Connected to VulkanViewportWindow::SceneLoaded (same signal
    // HierarchyPanel::Refresh() is connected to) rather than called directly
    // from LoadSceneFile — a scene requested before the viewport's first
    // paint is applied later, deferred (see SceneLoaded's doc comment), and
    // this must run after either case, not just the synchronous one.
    void SyncLightingModeMenu();

private:
    // Shared by OnNewProject/OnOpenProject/ContentBrowserPanel's
    // double-click handler: points the viewport at `scenePath` (which need
    // not be the project's default scene) and updates window/Save-action
    // state. Refreshes HierarchyPanel afterward.
    void LoadSceneFile(const std::filesystem::path& scenePath);

    // Shared by the Lighting menu's two actions: writes straight into the
    // loaded scene's live SceneLightingSettings — a no-op if no
    // project/scene is loaded yet (renderer is null).
    void SetLightingMode(polyizon::LightingMode mode);

    VulkanViewportWindow* m_ViewportWindow = nullptr; // owned by its container widget, not directly by this
    QTimer* m_RenderTimer = nullptr; // owned by Qt's parent-child hierarchy (parented to this)

    HierarchyPanel* m_HierarchyPanel = nullptr;
    InspectorPanel* m_InspectorPanel = nullptr;
    ContentBrowserPanel* m_ContentBrowserPanel = nullptr;
    ConsolePanel* m_ConsolePanel = nullptr;

    QAction* m_SaveSceneAction = nullptr;
    QAction* m_RealisticLightingAction = nullptr;
    QAction* m_VoxelLightingAction = nullptr;

    std::unique_ptr<Project> m_CurrentProject;
    // Distinct from m_CurrentProject->GetDefaultScenePath() — Content
    // Browser can load a different scene within the same project, and Save
    // Scene must write back to whichever one is actually open.
    std::optional<std::filesystem::path> m_CurrentScenePath;
};
