#include "hierarchy_panel.hpp"

#include "vulkan_viewport_window.hpp"

#include "editor_viewport_renderer.hpp"
#include "component_icons.hpp"

#include "polyizon/assets/mesh_import.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/vulkan/context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <QHBoxLayout>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QToolButton>
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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* toolbar = new QHBoxLayout();
    auto* addButton = new QToolButton(this);
    addButton->setText("+  Create");
    addButton->setObjectName("CreateEntityButton");
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto* createMenu = new QMenu(addButton);
    QAction* addEmptyAction = createMenu->addAction("Empty Entity");
    addEmptyAction->setIcon(GetComponentIcon(ComponentIconKind::Entity));
    createMenu->addSeparator();
    QAction* addCubeAction = createMenu->addAction("Cube");
    QAction* addPlaneAction = createMenu->addAction("Plane");
    addCubeAction->setIcon(GetComponentIcon(ComponentIconKind::Mesh));
    addPlaneAction->setIcon(GetComponentIcon(ComponentIconKind::Mesh));
    createMenu->addSeparator();
    QAction* addDirectionalLightAction = createMenu->addAction("Directional Light");
    QAction* addPointLightAction = createMenu->addAction("Point Light");
    QAction* addSpotLightAction = createMenu->addAction("Spot Light");
    addDirectionalLightAction->setIcon(GetComponentIcon(ComponentIconKind::DirectionalLight));
    addPointLightAction->setIcon(GetComponentIcon(ComponentIconKind::PointLight));
    addSpotLightAction->setIcon(GetComponentIcon(ComponentIconKind::SpotLight));
    addButton->setMenu(createMenu);

    auto* deleteButton = new QPushButton("Delete", this);
    deleteButton->setObjectName("DangerButton");
    deleteButton->setToolTip("Delete the selected entity");
    toolbar->addWidget(addButton);
    toolbar->addStretch();
    toolbar->addWidget(deleteButton);
    layout->addLayout(toolbar);

    m_ListWidget = new QListWidget(this);
    m_ListWidget->setAlternatingRowColors(true);
    m_ListWidget->setSpacing(1);
    layout->addWidget(m_ListWidget);

    connect(addEmptyAction, &QAction::triggered, this, &HierarchyPanel::OnAddEmpty);
    connect(addCubeAction, &QAction::triggered, this, &HierarchyPanel::OnAddCube);
    connect(addPlaneAction, &QAction::triggered, this, &HierarchyPanel::OnAddPlane);
    connect(addDirectionalLightAction, &QAction::triggered, this, &HierarchyPanel::OnAddDirectionalLight);
    connect(addPointLightAction, &QAction::triggered, this, &HierarchyPanel::OnAddPointLight);
    connect(addSpotLightAction, &QAction::triggered, this, &HierarchyPanel::OnAddSpotLight);
    connect(deleteButton, &QPushButton::clicked, this, &HierarchyPanel::OnDeleteSelected);
    connect(m_ListWidget, &QListWidget::itemSelectionChanged, this, &HierarchyPanel::OnSelectionChanged);
}

void HierarchyPanel::Refresh() {
    entt::entity previouslySelected = entt::null;
    if (QListWidgetItem* current = m_ListWidget->currentItem()) {
        previouslySelected = static_cast<entt::entity>(current->data(kEntityRole).toUInt());
    }

    const QSignalBlocker selectionBlocker(m_ListWidget);
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
        item->setIcon(GetEntityIcon(registry, entity));
        if (const auto* metadata = registry.try_get<polyizon::EntityMetadataComponent>(entity)) {
            QStringList state;
            if (!metadata->enabled) {
                item->setForeground(QBrush(QColor("#737c89")));
                QFont font = item->font();
                font.setItalic(true);
                item->setFont(font);
                state.push_back("Inactive");
            }
            if (metadata->staticForLighting) state.push_back("Contributes to baked lighting");
            if (!state.isEmpty()) item->setToolTip(state.join(" · "));
        }
        item->setData(kEntityRole, QVariant(static_cast<quint32>(entity)));
        m_ListWidget->addItem(item);
        if (entity == previouslySelected) {
            m_ListWidget->setCurrentItem(item);
        }
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

void HierarchyPanel::OnAddDirectionalLight() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    polyizon::Scene& scene = renderer->GetScene();
    const entt::entity entity = scene.CreateEntity();
    scene.GetRegistry().emplace<polyizon::TagComponent>(entity, "Directional Light");
    polyizon::TransformComponent transform;
    transform.rotationEulerDegrees = glm::vec3(-25.0f, 90.0f, 0.0f);
    scene.GetRegistry().emplace<polyizon::TransformComponent>(entity, transform);
    scene.GetRegistry().emplace<polyizon::DirectionalLightComponent>(entity);
    Refresh();
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
