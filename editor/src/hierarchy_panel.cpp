#include "hierarchy_panel.hpp"

#include "vulkan_viewport_window.hpp"

#include "editor_viewport_renderer.hpp"

#include "polyizon/assets/mesh_import.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/vulkan/context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <stdexcept>

namespace {

// Resolved relative to the running executable's own directory, matching
// every other asset-loading class's identical private helper (Image,
// ScriptEngine, SceneSerializer, etc.) — duplicated here rather than shared
// for the same reason they duplicate it from each other.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

constexpr int kEntityRole = Qt::UserRole;

} // namespace

HierarchyPanel::HierarchyPanel(VulkanViewportWindow* viewportWindow, QWidget* parent)
    : QWidget(parent), m_ViewportWindow(viewportWindow) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* toolbar = new QHBoxLayout();
    auto* addEmptyButton = new QPushButton("Add Empty", this);
    auto* addCubeButton = new QPushButton("Add Cube", this);
    auto* addPlaneButton = new QPushButton("Add Plane", this);
    auto* addPointLightButton = new QPushButton("Add Point Light", this);
    auto* addSpotLightButton = new QPushButton("Add Spot Light", this);
    auto* deleteButton = new QPushButton("Delete Selected", this);
    toolbar->addWidget(addEmptyButton);
    toolbar->addWidget(addCubeButton);
    toolbar->addWidget(addPlaneButton);
    toolbar->addWidget(addPointLightButton);
    toolbar->addWidget(addSpotLightButton);
    toolbar->addWidget(deleteButton);
    layout->addLayout(toolbar);

    m_ListWidget = new QListWidget(this);
    layout->addWidget(m_ListWidget);

    connect(addEmptyButton, &QPushButton::clicked, this, &HierarchyPanel::OnAddEmpty);
    connect(addCubeButton, &QPushButton::clicked, this, &HierarchyPanel::OnAddCube);
    connect(addPlaneButton, &QPushButton::clicked, this, &HierarchyPanel::OnAddPlane);
    connect(addPointLightButton, &QPushButton::clicked, this, &HierarchyPanel::OnAddPointLight);
    connect(addSpotLightButton, &QPushButton::clicked, this, &HierarchyPanel::OnAddSpotLight);
    connect(deleteButton, &QPushButton::clicked, this, &HierarchyPanel::OnDeleteSelected);
    connect(m_ListWidget, &QListWidget::itemSelectionChanged, this, &HierarchyPanel::OnSelectionChanged);
}

void HierarchyPanel::Refresh() {
    m_ListWidget->clear();

    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }

    entt::registry& registry = renderer->GetScene().GetRegistry();
    auto view = registry.view<polyizon::TagComponent>();
    for (const entt::entity entity : view) {
        const auto& tag = view.get<polyizon::TagComponent>(entity);
        auto* item = new QListWidgetItem(QString::fromStdString(tag.name));
        item->setData(kEntityRole, QVariant(static_cast<quint32>(entity)));
        m_ListWidget->addItem(item);
    }
}

void HierarchyPanel::OnAddEmpty() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    polyizon::Scene& scene = renderer->GetScene();
    const entt::entity entity = scene.CreateEntity();
    scene.GetRegistry().emplace<polyizon::TagComponent>(entity, "Empty Entity");
    scene.GetRegistry().emplace<polyizon::TransformComponent>(entity);
    Refresh();
}

void HierarchyPanel::OnAddCube() {
    AddMeshEntity("Cube", "models/cube.obj");
}

void HierarchyPanel::OnAddPlane() {
    AddMeshEntity("Plane", "models/plane.obj");
}

void HierarchyPanel::OnAddPointLight() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    polyizon::Scene& scene = renderer->GetScene();
    const entt::entity entity = scene.CreateEntity();
    scene.GetRegistry().emplace<polyizon::TagComponent>(entity, "Point Light");
    scene.GetRegistry().emplace<polyizon::TransformComponent>(entity);
    scene.GetRegistry().emplace<polyizon::PointLightComponent>(entity);
    Refresh();
}

void HierarchyPanel::OnAddSpotLight() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    polyizon::Scene& scene = renderer->GetScene();
    const entt::entity entity = scene.CreateEntity();
    scene.GetRegistry().emplace<polyizon::TagComponent>(entity, "Spot Light");
    scene.GetRegistry().emplace<polyizon::TransformComponent>(entity);
    scene.GetRegistry().emplace<polyizon::SpotLightComponent>(entity);
    Refresh();
}

void HierarchyPanel::AddMeshEntity(const QString& tagName, const std::filesystem::path& modelRelativePath) {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    std::shared_ptr<polyizon::Mesh> mesh;
    try {
        mesh = LoadMesh(renderer->GetVulkanContext(), GetExecutableDirectory() / modelRelativePath);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to add entity", e.what());
        return;
    }

    polyizon::Scene& scene = renderer->GetScene();
    const entt::entity entity = scene.CreateEntity();
    scene.GetRegistry().emplace<polyizon::TagComponent>(entity, tagName.toStdString());
    scene.GetRegistry().emplace<polyizon::TransformComponent>(entity);
    scene.GetRegistry().emplace<polyizon::MeshComponent>(entity, mesh, modelRelativePath.generic_string());
    scene.GetRegistry().emplace<polyizon::MaterialComponent>(entity);
    Refresh();
}

void HierarchyPanel::OnDeleteSelected() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }

    QListWidgetItem* item = m_ListWidget->currentItem();
    if (!item) {
        return;
    }
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    const entt::entity entity = static_cast<entt::entity>(item->data(kEntityRole).toUInt());

    // A deleted entity's MeshComponent (if any) destroys GPU buffers that
    // must not be in flight — same precedent as
    // EditorViewportRenderer::LoadScene's vkDeviceWaitIdle before replacing
    // the whole scene.
    vkDeviceWaitIdle(renderer->GetVulkanContext().GetDevice());
    renderer->GetScene().GetRegistry().destroy(entity);

    Refresh();
    emit EntitySelected(entt::null);
}

void HierarchyPanel::OnSelectionChanged() {
    QListWidgetItem* item = m_ListWidget->currentItem();
    if (!item) {
        emit EntitySelected(entt::null);
        return;
    }

    emit EntitySelected(static_cast<entt::entity>(item->data(kEntityRole).toUInt()));
}
