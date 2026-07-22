#pragma once

#include <entt/entt.hpp>

#include <QWidget>

#include <filesystem>

class QListWidget;
class VulkanViewportWindow;

// Left dock panel: a flat list of every entity in the currently loaded
// scene, labelled by TagComponent (see polyizon/scene/components.hpp).
// Flat, not a tree — Scene has no parent/child relationship between
// entities yet, so a tree here would only be misleading decoration.
//
// Reads/writes the live scene directly through VulkanViewportWindow's
// lazily-constructed EditorViewportRenderer (GetRenderer() may be null
// before the viewport's first exposeEvent, or before any project/scene is
// loaded — every operation here no-ops in that case rather than asserting,
// since a fresh editor window with no project open is a normal state, not
// an error).
class HierarchyPanel : public QWidget {
    Q_OBJECT

public:
    explicit HierarchyPanel(VulkanViewportWindow* viewportWindow, QWidget* parent = nullptr);

    // Repopulates the list from the registry. Called by MainWindow after a
    // scene loads, and internally after any create/delete this panel
    // performs itself.
    void Refresh();

signals:
    // Emitted whenever the list selection changes, including to
    // entt::null when nothing is selected (e.g. after a delete) — the
    // Inspector panel treats entt::null as "show nothing".
    void EntitySelected(entt::entity entity);

private slots:
    void OnAddEmpty();
    void OnAddCube();
    void OnAddPlane();
    void OnDeleteSelected();
    void OnSelectionChanged();

private:
    // Shared by OnAddCube/OnAddPlane: imports the given engine-relative
    // model path (e.g. "models/cube.obj", same convention as
    // Project::BuildDefaultSceneJson's default scene) via mesh_import.hpp's
    // LoadMesh, then creates a Tag+Transform+Mesh+Material entity from it.
    void AddMeshEntity(const QString& tagName, const std::filesystem::path& modelRelativePath);

    VulkanViewportWindow* m_ViewportWindow;
    QListWidget* m_ListWidget = nullptr;
};
