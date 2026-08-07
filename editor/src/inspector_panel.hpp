#pragma once

#include <entt/entt.hpp>

#include <QWidget>

class QFormLayout;
class QVBoxLayout;
class VulkanViewportWindow;

// Right dock panel: shows and edits every component on whichever entity
// HierarchyPanel last selected (wired via MainWindow connecting
// HierarchyPanel::EntitySelected to SetSelectedEntity). Every field writes
// straight into the live registry component on change — there is no
// separate "apply"/"revert" step, matching how Hierarchy's own edits
// (add/delete) are already immediate. Persistence to disk only happens via
// MainWindow's explicit File > Save Scene action.
//
// Rebuilds its entire form from scratch on every selection change rather
// than trying to diff/reuse widgets across entities — the component set
// (Tag/Transform always present, Material/Mesh/Script optional) differs per
// entity, so a full rebuild is simpler and cheap enough at this scale (a
// handful of rows) to not be worth optimizing.
//
// Deliberately does NOT poll the registry every frame to reflect
// script-driven changes (e.g. spin.lua rotating a cube) live while
// selected — doing so would fight the user's own in-progress edits in the
// same fields. Phase 19 added the "playing" vs. "editing" mode distinction
// this comment used to say was future work (see EditorViewportRenderer::
// PlayState) — but this panel's own behavior here is unchanged: still a
// snapshot as of selection time, editable at any PlayState (see
// VulkanViewportWindow::MaybeWarnEditDuringPlay for the one-time heads-up
// shown instead of blocking edits during Play/Pause).
class InspectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit InspectorPanel(VulkanViewportWindow* viewportWindow, QWidget* parent = nullptr);

public slots:
    void SetSelectedEntity(entt::entity entity);

signals:
    void EntityPresentationChanged();

private:
    void Rebuild();
    void BuildTagSection(QFormLayout* form, entt::registry& registry);
    void BuildTransformSection(QFormLayout* form, entt::registry& registry);
    void BuildMaterialSection(QVBoxLayout* container, entt::registry& registry);
    void BuildMeshSection(QVBoxLayout* container, entt::registry& registry);
    void BuildScriptSection(QVBoxLayout* container, entt::registry& registry);
    void BuildDirectionalLightSection(QVBoxLayout* container, entt::registry& registry);
    void BuildPointLightSection(QVBoxLayout* container, entt::registry& registry);
    void BuildSpotLightSection(QVBoxLayout* container, entt::registry& registry);
    void BuildAddComponentMenu(QVBoxLayout* container, entt::registry& registry);

    VulkanViewportWindow* m_ViewportWindow;
    entt::entity m_SelectedEntity = entt::null;

    // Root layout of this widget; cleared and rebuilt wholesale by
    // Rebuild() each time the selection changes.
    QVBoxLayout* m_RootLayout = nullptr;
};
