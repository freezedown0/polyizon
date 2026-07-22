#include "main_window.hpp"

#include "console_panel.hpp"
#include "content_browser_panel.hpp"
#include "editor_viewport_renderer.hpp"
#include "hierarchy_panel.hpp"
#include "inspector_panel.hpp"
#include "scene_serializer.hpp"
#include "vulkan_viewport_window.hpp"

#include <entt/entt.hpp>

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>

#include <filesystem>

namespace {
// ~60 FPS. Single-threaded on Qt's GUI thread for this phase (see the
// VulkanViewportWindow header comment) — correctness first, a render thread
// is a later optimization if frame pacing becomes an issue.
constexpr int kRenderIntervalMs = 16;
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Polyizon Editor");
    resize(1600, 900);

    m_ViewportWindow = new VulkanViewportWindow();
    QWidget* container = QWidget::createWindowContainer(m_ViewportWindow, this);
    container->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(container);

    m_HierarchyPanel = new HierarchyPanel(m_ViewportWindow, this);
    m_InspectorPanel = new InspectorPanel(m_ViewportWindow, this);
    m_ContentBrowserPanel = new ContentBrowserPanel(this);
    m_ConsolePanel = new ConsolePanel(this);

    auto* hierarchyDock = new QDockWidget("Hierarchy", this);
    hierarchyDock->setWidget(m_HierarchyPanel);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    auto* inspectorDock = new QDockWidget("Inspector", this);
    inspectorDock->setWidget(m_InspectorPanel);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    auto* contentBrowserDock = new QDockWidget("Content Browser", this);
    contentBrowserDock->setWidget(m_ContentBrowserPanel);
    addDockWidget(Qt::BottomDockWidgetArea, contentBrowserDock);

    auto* consoleDock = new QDockWidget("Console", this);
    consoleDock->setWidget(m_ConsolePanel);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
    tabifyDockWidget(contentBrowserDock, consoleDock);
    contentBrowserDock->raise(); // shown on top of the tab group initially

    connect(m_HierarchyPanel, &HierarchyPanel::EntitySelected, m_InspectorPanel, &InspectorPanel::SetSelectedEntity);
    connect(m_ContentBrowserPanel, &ContentBrowserPanel::SceneFileActivated, this, &MainWindow::LoadSceneFile);
    // Covers both LoadScene() paths (synchronous and the exposeEvent-deferred
    // one — see VulkanViewportWindow::SceneLoaded's doc comment) rather than
    // refreshing directly from LoadSceneFile, which would miss the deferred
    // case entirely.
    connect(m_ViewportWindow, &VulkanViewportWindow::SceneLoaded, m_HierarchyPanel, &HierarchyPanel::Refresh);

    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New Project...", this, &MainWindow::OnNewProject);
    fileMenu->addAction("&Open Project...", this, &MainWindow::OnOpenProject);
    fileMenu->addSeparator();
    m_SaveSceneAction = fileMenu->addAction("&Save Scene", QKeySequence::Save, this, &MainWindow::OnSaveScene);
    m_SaveSceneAction->setEnabled(false); // nothing to save until a project/scene is loaded

    m_RenderTimer = new QTimer(this);
    connect(m_RenderTimer, &QTimer::timeout, this, [this]() {
        m_ViewportWindow->RenderIfExposed();
        m_ConsolePanel->Poll();
    });
    m_RenderTimer->start(kRenderIntervalMs);
}

void MainWindow::OnNewProject() {
    const QString parentDir = QFileDialog::getExistingDirectory(this, "Choose a location for the new project");
    if (parentDir.isEmpty()) {
        return; // user cancelled
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Project", "Project name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) {
        return; // user cancelled
    }

    try {
        m_CurrentProject = std::make_unique<Project>(
            Project::CreateNew(name.toStdString(), std::filesystem::path(parentDir.toStdString())));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to create project", e.what());
        return;
    }

    setWindowTitle(QString("Polyizon Editor - %1").arg(QString::fromStdString(m_CurrentProject->GetName())));
    m_ContentBrowserPanel->SetRootDirectory(m_CurrentProject->GetRootDir());
    LoadSceneFile(m_CurrentProject->GetDefaultScenePath());
}

void MainWindow::OnOpenProject() {
    const QString manifestFile = QFileDialog::getOpenFileName(
        this, "Open Project", QString(), "Polyizon project (project.json)");
    if (manifestFile.isEmpty()) {
        return; // user cancelled
    }

    try {
        m_CurrentProject = std::make_unique<Project>(Project::Open(std::filesystem::path(manifestFile.toStdString())));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to open project", e.what());
        return;
    }

    setWindowTitle(QString("Polyizon Editor - %1").arg(QString::fromStdString(m_CurrentProject->GetName())));
    m_ContentBrowserPanel->SetRootDirectory(m_CurrentProject->GetRootDir());
    LoadSceneFile(m_CurrentProject->GetDefaultScenePath());
}

void MainWindow::OnSaveScene() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!m_CurrentScenePath || !renderer) {
        return;
    }

    try {
        ::SaveScene(renderer->GetScene(), *m_CurrentScenePath);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to save scene", e.what());
    }
}

void MainWindow::LoadSceneFile(const std::filesystem::path& scenePath) {
    try {
        m_ViewportWindow->LoadScene(scenePath);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to load scene", e.what());
        return;
    }

    m_CurrentScenePath = scenePath;
    m_SaveSceneAction->setEnabled(true);
    m_InspectorPanel->SetSelectedEntity(entt::null);
}
