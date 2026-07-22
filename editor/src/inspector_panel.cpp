#include "inspector_panel.hpp"

#include "mesh_import.hpp"
#include "vulkan_viewport_window.hpp"

#include "editor_viewport_renderer.hpp"

#include "polyizon/scene/components.hpp"
#include "polyizon/vulkan/context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <filesystem>
#include <stdexcept>

namespace {

// Same executable-relative resolution convention as every other asset
// loader in this codebase (HierarchyPanel, ScriptEngine, SceneSerializer) —
// duplicated per that same established convention.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

// A path picked via QFileDialog is absolute. If it happens to live under
// the executable's own directory (e.g. the engine's core/models/,
// core/scripts/, or a project's own folder if colocated), store it relative
// to that directory for portability, matching what SceneSerializer writes
// for the built-in sample scene. Otherwise store the absolute path as-is —
// std::filesystem::path's operator/ used everywhere paths are resolved
// (GetExecutableDirectory() / storedPath) already returns an absolute
// right-hand operand unchanged, so this needs no special-casing on load.
std::string ToStoredAssetPath(const std::filesystem::path& absolutePath) {
    const std::filesystem::path exeDir = GetExecutableDirectory();
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, exeDir);
    if (!relative.empty() && relative.native().rfind(L"..", 0) != 0) {
        return relative.generic_string();
    }
    return absolutePath.generic_string();
}

// Recursively deletes every item (widget or nested layout) in `layout`,
// leaving it empty but reusable — used by InspectorPanel::Rebuild() to
// clear the previous entity's form before building the new one.
void ClearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        } else if (QLayout* childLayout = item->layout()) {
            ClearLayout(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

} // namespace

InspectorPanel::InspectorPanel(VulkanViewportWindow* viewportWindow, QWidget* parent)
    : QWidget(parent), m_ViewportWindow(viewportWindow) {
    m_RootLayout = new QVBoxLayout(this);
    m_RootLayout->setContentsMargins(4, 4, 4, 4);
    m_RootLayout->addWidget(new QLabel("No entity selected", this));
    m_RootLayout->addStretch();
}

void InspectorPanel::SetSelectedEntity(entt::entity entity) {
    m_SelectedEntity = entity;
    Rebuild();
}

void InspectorPanel::Rebuild() {
    ClearLayout(m_RootLayout);

    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer || m_SelectedEntity == entt::null) {
        m_RootLayout->addWidget(new QLabel("No entity selected", this));
        m_RootLayout->addStretch();
        return;
    }

    entt::registry& registry = renderer->GetScene().GetRegistry();
    if (!registry.valid(m_SelectedEntity)) {
        m_RootLayout->addWidget(new QLabel("No entity selected", this));
        m_RootLayout->addStretch();
        return;
    }

    auto* form = new QFormLayout();
    BuildTagSection(form, registry);
    BuildTransformSection(form, registry);
    m_RootLayout->addLayout(form);

    BuildMaterialSection(m_RootLayout, registry);
    BuildMeshSection(m_RootLayout, registry);
    BuildScriptSection(m_RootLayout, registry);

    m_RootLayout->addStretch();
}

void InspectorPanel::BuildTagSection(QFormLayout* form, entt::registry& registry) {
    auto& tag = registry.get<polyizon::TagComponent>(m_SelectedEntity);

    auto* nameEdit = new QLineEdit(QString::fromStdString(tag.name), this);
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit]() {
        polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
        if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
            return;
        }
        renderer->GetScene().GetRegistry().get<polyizon::TagComponent>(m_SelectedEntity).name =
            nameEdit->text().toStdString();
    });
    form->addRow("Name", nameEdit);
}

void InspectorPanel::BuildTransformSection(QFormLayout* form, entt::registry& registry) {
    auto& transform = registry.get<polyizon::TransformComponent>(m_SelectedEntity);

    // field: pointer-to-member into glm::vec3 (x/y/z), row label prefix.
    auto addVec3Row = [&](const char* label, glm::vec3 polyizon::TransformComponent::*field) {
        auto* rowLayout = new QHBoxLayout();
        for (int axis = 0; axis < 3; ++axis) {
            auto* spinBox = new QDoubleSpinBox(this);
            spinBox->setRange(-100000.0, 100000.0);
            spinBox->setDecimals(3);
            spinBox->setSingleStep(0.1);
            spinBox->setValue((transform.*field)[axis]);
            connect(spinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, field, axis](double value) {
                    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
                    if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                        return;
                    }
                    auto& liveTransform = renderer->GetScene().GetRegistry().get<polyizon::TransformComponent>(m_SelectedEntity);
                    (liveTransform.*field)[axis] = static_cast<float>(value);
                });
            rowLayout->addWidget(spinBox);
        }
        form->addRow(label, rowLayout);
    };

    addVec3Row("Position", &polyizon::TransformComponent::position);
    addVec3Row("Rotation", &polyizon::TransformComponent::rotationEulerDegrees);
    addVec3Row("Scale", &polyizon::TransformComponent::scale);
}

void InspectorPanel::BuildMaterialSection(QVBoxLayout* container, entt::registry& registry) {
    container->addWidget(new QLabel("<b>Material</b>", this));

    if (auto* material = registry.try_get<polyizon::MaterialComponent>(m_SelectedEntity)) {
        auto* rowLayout = new QHBoxLayout();
        const QColor current(
            static_cast<int>(material->baseColor.r * 255.0f),
            static_cast<int>(material->baseColor.g * 255.0f),
            static_cast<int>(material->baseColor.b * 255.0f));

        auto* swatchButton = new QPushButton(this);
        swatchButton->setFixedWidth(48);
        swatchButton->setStyleSheet(QString("background-color: %1;").arg(current.name()));
        connect(swatchButton, &QPushButton::clicked, this, [this, swatchButton]() {
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            auto* liveMaterial = renderer->GetScene().GetRegistry().try_get<polyizon::MaterialComponent>(m_SelectedEntity);
            if (!liveMaterial) {
                return;
            }
            const QColor existing(
                static_cast<int>(liveMaterial->baseColor.r * 255.0f),
                static_cast<int>(liveMaterial->baseColor.g * 255.0f),
                static_cast<int>(liveMaterial->baseColor.b * 255.0f));
            const QColor chosen = QColorDialog::getColor(existing, this, "Base Color");
            if (!chosen.isValid()) {
                return;
            }
            liveMaterial->baseColor = glm::vec3(chosen.redF(), chosen.greenF(), chosen.blueF());
            swatchButton->setStyleSheet(QString("background-color: %1;").arg(chosen.name()));
        });
        rowLayout->addWidget(swatchButton);

        auto* removeButton = new QPushButton("Remove Material", this);
        connect(removeButton, &QPushButton::clicked, this, [this]() {
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            renderer->GetScene().GetRegistry().remove<polyizon::MaterialComponent>(m_SelectedEntity);
            Rebuild();
        });
        rowLayout->addWidget(removeButton);
        container->addLayout(rowLayout);
    } else {
        auto* addButton = new QPushButton("Add Material", this);
        connect(addButton, &QPushButton::clicked, this, [this]() {
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            renderer->GetScene().GetRegistry().emplace<polyizon::MaterialComponent>(m_SelectedEntity);
            Rebuild();
        });
        container->addWidget(addButton);
    }
}

void InspectorPanel::BuildMeshSection(QVBoxLayout* container, entt::registry& registry) {
    container->addWidget(new QLabel("<b>Mesh</b>", this));

    if (auto* mesh = registry.try_get<polyizon::MeshComponent>(m_SelectedEntity)) {
        auto* pathLabel = new QLabel(QString::fromStdString(mesh->sourcePath), this);
        pathLabel->setWordWrap(true);
        container->addWidget(pathLabel);

        auto* rowLayout = new QHBoxLayout();
        auto* changeButton = new QPushButton("Change...", this);
        connect(changeButton, &QPushButton::clicked, this, [this]() {
            const QString filePath = QFileDialog::getOpenFileName(
                this, "Choose Mesh", QString(), "Meshes (*.obj *.fbx)");
            if (filePath.isEmpty()) {
                return;
            }
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            try {
                const std::filesystem::path chosen(filePath.toStdWString());
                std::shared_ptr<polyizon::Mesh> newMesh = LoadMesh(renderer->GetVulkanContext(), chosen);
                // Replacing the component destroys the previous Mesh's GPU
                // buffers (once the shared_ptr's last reference drops) —
                // must not happen while that mesh could still be in flight.
                vkDeviceWaitIdle(renderer->GetVulkanContext().GetDevice());
                renderer->GetScene().GetRegistry().replace<polyizon::MeshComponent>(
                    m_SelectedEntity, newMesh, ToStoredAssetPath(chosen));
            } catch (const std::exception& e) {
                QMessageBox::critical(this, "Failed to load mesh", e.what());
                return;
            }
            Rebuild();
        });
        rowLayout->addWidget(changeButton);

        auto* removeButton = new QPushButton("Remove Mesh", this);
        connect(removeButton, &QPushButton::clicked, this, [this]() {
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            vkDeviceWaitIdle(renderer->GetVulkanContext().GetDevice());
            renderer->GetScene().GetRegistry().remove<polyizon::MeshComponent>(m_SelectedEntity);
            Rebuild();
        });
        rowLayout->addWidget(removeButton);
        container->addLayout(rowLayout);
    } else {
        auto* addButton = new QPushButton("Add Mesh...", this);
        connect(addButton, &QPushButton::clicked, this, [this]() {
            const QString filePath = QFileDialog::getOpenFileName(
                this, "Choose Mesh", QString(), "Meshes (*.obj *.fbx)");
            if (filePath.isEmpty()) {
                return;
            }
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            try {
                const std::filesystem::path chosen(filePath.toStdWString());
                std::shared_ptr<polyizon::Mesh> newMesh = LoadMesh(renderer->GetVulkanContext(), chosen);
                renderer->GetScene().GetRegistry().emplace<polyizon::MeshComponent>(
                    m_SelectedEntity, newMesh, ToStoredAssetPath(chosen));
            } catch (const std::exception& e) {
                QMessageBox::critical(this, "Failed to load mesh", e.what());
                return;
            }
            Rebuild();
        });
        container->addWidget(addButton);
    }
}

void InspectorPanel::BuildScriptSection(QVBoxLayout* container, entt::registry& registry) {
    container->addWidget(new QLabel("<b>Script</b>", this));

    if (auto* script = registry.try_get<polyizon::ScriptComponent>(m_SelectedEntity)) {
        auto* pathEdit = new QLineEdit(QString::fromStdString(script->scriptPath), this);
        connect(pathEdit, &QLineEdit::editingFinished, this, [this, pathEdit]() {
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            renderer->GetScene().GetRegistry().get<polyizon::ScriptComponent>(m_SelectedEntity).scriptPath =
                pathEdit->text().toStdString();
        });
        container->addWidget(pathEdit);

        auto* rowLayout = new QHBoxLayout();
        auto* browseButton = new QPushButton("Browse...", this);
        connect(browseButton, &QPushButton::clicked, this, [this, pathEdit]() {
            const QString filePath = QFileDialog::getOpenFileName(
                this, "Choose Script", QString(), "Lua scripts (*.lua)");
            if (filePath.isEmpty()) {
                return;
            }
            const std::string stored = ToStoredAssetPath(std::filesystem::path(filePath.toStdWString()));
            pathEdit->setText(QString::fromStdString(stored));
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            renderer->GetScene().GetRegistry().get<polyizon::ScriptComponent>(m_SelectedEntity).scriptPath = stored;
        });
        rowLayout->addWidget(browseButton);

        auto* removeButton = new QPushButton("Remove Script", this);
        connect(removeButton, &QPushButton::clicked, this, [this]() {
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            renderer->GetScene().GetRegistry().remove<polyizon::ScriptComponent>(m_SelectedEntity);
            Rebuild();
        });
        rowLayout->addWidget(removeButton);
        container->addLayout(rowLayout);
    } else {
        auto* addButton = new QPushButton("Add Script...", this);
        connect(addButton, &QPushButton::clicked, this, [this]() {
            const QString filePath = QFileDialog::getOpenFileName(
                this, "Choose Script", QString(), "Lua scripts (*.lua)");
            if (filePath.isEmpty()) {
                return;
            }
            polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
            if (!renderer || !renderer->GetScene().GetRegistry().valid(m_SelectedEntity)) {
                return;
            }
            const std::string stored = ToStoredAssetPath(std::filesystem::path(filePath.toStdWString()));
            renderer->GetScene().GetRegistry().emplace<polyizon::ScriptComponent>(m_SelectedEntity, stored);
            Rebuild();
        });
        container->addWidget(addButton);
    }
}
