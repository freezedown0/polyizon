#include "main_window.hpp"

#include "console_panel.hpp"
#include "content_browser_panel.hpp"
#include "editor_viewport_renderer.hpp"
#include "game_builder.hpp"
#include "hierarchy_panel.hpp"
#include "inspector_panel.hpp"
#include "lighting_settings_dialog.hpp"
#include "vulkan_viewport_window.hpp"

#include "polyizon/scene/scene_serializer.hpp"

#include <entt/entt.hpp>

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include <filesystem>

namespace {
// ~60 FPS. Single-threaded on Qt's GUI thread for this phase (see the
// VulkanViewportWindow header comment) — correctness first, a render thread
// is a later optimization if frame pacing becomes an issue.
constexpr int kRenderIntervalMs = 16;

enum class TransportIcon { Play, Pause, Stop };

QIcon MakeTransportIcon(TransportIcon icon) {
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#e6ebf3"));
    if (icon == TransportIcon::Play) {
        painter.drawPolygon(QPolygonF({QPointF(5, 3), QPointF(15, 9), QPointF(5, 15)}));
    } else if (icon == TransportIcon::Pause) {
        painter.drawRoundedRect(QRectF(4, 3, 4, 12), 1, 1);
        painter.drawRoundedRect(QRectF(11, 3, 4, 12), 1, 1);
    } else {
        painter.drawRoundedRect(QRectF(4, 4, 10, 10), 1.5, 1.5);
    }
    return QIcon(pixmap);
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Polyizon Editor");
    resize(1600, 900);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    m_ViewportWindow = new VulkanViewportWindow();
    QWidget* container = QWidget::createWindowContainer(m_ViewportWindow, this);
    container->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(container);

    m_HierarchyPanel = new HierarchyPanel(m_ViewportWindow, this);
    m_InspectorPanel = new InspectorPanel(m_ViewportWindow, this);
    m_ContentBrowserPanel = new ContentBrowserPanel(this);
    m_ConsolePanel = new ConsolePanel(this);

    auto* hierarchyDock = new QDockWidget("Hierarchy", this);
    hierarchyDock->setObjectName("HierarchyDock");
    hierarchyDock->setMinimumWidth(250);
    hierarchyDock->setWidget(m_HierarchyPanel);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    auto* inspectorDock = new QDockWidget("Inspector", this);
    inspectorDock->setObjectName("InspectorDock");
    inspectorDock->setMinimumWidth(280);
    inspectorDock->setWidget(m_InspectorPanel);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    auto* contentBrowserDock = new QDockWidget("Content Browser", this);
    contentBrowserDock->setObjectName("ContentBrowserDock");
    contentBrowserDock->setMinimumHeight(180);
    contentBrowserDock->setWidget(m_ContentBrowserPanel);
    addDockWidget(Qt::BottomDockWidgetArea, contentBrowserDock);

    auto* consoleDock = new QDockWidget("Console", this);
    consoleDock->setObjectName("ConsoleDock");
    consoleDock->setWidget(m_ConsolePanel);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
    tabifyDockWidget(contentBrowserDock, consoleDock);
    contentBrowserDock->raise(); // shown on top of the tab group initially
    resizeDocks({hierarchyDock, inspectorDock}, {290, 320}, Qt::Horizontal);
    resizeDocks({contentBrowserDock}, {220}, Qt::Vertical);

    connect(m_HierarchyPanel, &HierarchyPanel::EntitySelected, m_InspectorPanel, &InspectorPanel::SetSelectedEntity);
    connect(m_InspectorPanel, &InspectorPanel::EntityPresentationChanged, m_HierarchyPanel, &HierarchyPanel::Refresh);
    // Phase 20: the viewport gizmo needs to know the current selection too
    // (to know what to draw/drag) — same signal, a second slot.
    connect(m_HierarchyPanel, &HierarchyPanel::EntitySelected, m_ViewportWindow, &VulkanViewportWindow::SetSelectedEntity);
    connect(m_ContentBrowserPanel, &ContentBrowserPanel::SceneFileActivated, this, &MainWindow::LoadSceneFile);
    // Covers both LoadScene() paths (synchronous and the exposeEvent-deferred
    // one — see VulkanViewportWindow::SceneLoaded's doc comment) rather than
    // refreshing directly from LoadSceneFile, which would miss the deferred
    // case entirely.
    connect(m_ViewportWindow, &VulkanViewportWindow::SceneLoaded, m_HierarchyPanel, &HierarchyPanel::Refresh);
    connect(m_ViewportWindow, &VulkanViewportWindow::SceneLoaded, this, &MainWindow::UpdatePlayActionsEnabled);

    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New Project...", this, &MainWindow::OnNewProject);
    fileMenu->addAction("&Open Project...", this, &MainWindow::OnOpenProject);
    fileMenu->addSeparator();
    m_SaveSceneAction = fileMenu->addAction("&Save Scene", QKeySequence::Save, this, &MainWindow::OnSaveScene);
    m_SaveSceneAction->setEnabled(false); // nothing to save until a project/scene is loaded
    fileMenu->addSeparator();
    m_BuildGameAction = fileMenu->addAction("&Build Game...", this, &MainWindow::OnBuildGame);
    m_BuildGameAction->setEnabled(false); // nothing to export until a project is loaded

    QMenu* lightingMenu = menuBar()->addMenu("&Lighting");
    lightingMenu->addAction("&Environment Editor...", this, &MainWindow::OnOpenLightingSettings);

    // Play/Pause/Stop (Phase 19) — see EditorViewportRenderer::PlayState.
    // Same QAction objects added to both a menu (keyboard/menu-driven access)
    // and a toolbar (one-click access, standard for this kind of
    // frequently-used editor action) rather than duplicating them.
    QMenu* playMenu = menuBar()->addMenu("&Play");
    m_PlayAction = playMenu->addAction("&Play", this, &MainWindow::OnPlay);
    m_PauseAction = playMenu->addAction("Pa&use", this, &MainWindow::OnPause);
    m_StopAction = playMenu->addAction("&Stop", this, &MainWindow::OnStop);

    m_PlayAction->setIcon(MakeTransportIcon(TransportIcon::Play));
    m_PauseAction->setIcon(MakeTransportIcon(TransportIcon::Pause));
    m_StopAction->setIcon(MakeTransportIcon(TransportIcon::Stop));
    m_PlayAction->setToolTip("Play scene");
    m_PauseAction->setToolTip("Pause simulation");
    m_StopAction->setToolTip("Stop and restore scene");

    QToolBar* playToolBar = addToolBar("Play");
    playToolBar->setObjectName("PlayToolbar");
    playToolBar->setMovable(false);
    playToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    playToolBar->addAction(m_PlayAction);
    playToolBar->addAction(m_PauseAction);
    playToolBar->addAction(m_StopAction);
    playToolBar->widgetForAction(m_PlayAction)->setObjectName("PlayButton");
    playToolBar->widgetForAction(m_PauseAction)->setObjectName("TransportButton");
    playToolBar->widgetForAction(m_StopAction)->setObjectName("TransportButton");

    UpdatePlayActionsEnabled(); // nothing loaded yet -> all disabled until a scene loads

    // Move/Rotate viewport gizmo toggle (Phase 20) — exclusive toolbar
    // buttons, same QActionGroup pattern as the Lighting mode toggle above.
    // Move is the default (matches EditorViewportRenderer::m_GizmoMode).
    QToolBar* gizmoToolBar = addToolBar("Gizmo");
    gizmoToolBar->setObjectName("GizmoToolbar");
    gizmoToolBar->setMovable(false);
    auto* gizmoModeGroup = new QActionGroup(this);
    gizmoModeGroup->setExclusive(true);

    QAction* moveGizmoAction = gizmoToolBar->addAction("Move");
    moveGizmoAction->setToolTip("Move selected entity");
    moveGizmoAction->setCheckable(true);
    moveGizmoAction->setChecked(true);
    gizmoModeGroup->addAction(moveGizmoAction);
    connect(moveGizmoAction, &QAction::triggered, this,
        [this]() { m_ViewportWindow->SetGizmoMode(polyizon::GizmoMode::Move); });

    QAction* rotateGizmoAction = gizmoToolBar->addAction("Rotate");
    rotateGizmoAction->setToolTip("Rotate selected entity");
    rotateGizmoAction->setCheckable(true);
    gizmoModeGroup->addAction(rotateGizmoAction);
    connect(rotateGizmoAction, &QAction::triggered, this,
        [this]() { m_ViewportWindow->SetGizmoMode(polyizon::GizmoMode::Rotate); });

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Credits...", this, &MainWindow::OnCredits);

    statusBar()->showMessage("Ready  •  Open or create a project to begin");

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
    m_BuildGameAction->setEnabled(true);
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
    m_BuildGameAction->setEnabled(true);
    LoadSceneFile(m_CurrentProject->GetDefaultScenePath());
}

void MainWindow::OnSaveScene() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!m_CurrentScenePath || !renderer) {
        return;
    }

    // Saving mid-Play persists whatever Play has mutated straight to disk —
    // the single most consequential "edit" the heads-up in
    // MaybeWarnEditDuringPlay covers, so it applies here too even though
    // Save Scene itself isn't blocked (see the class doc comment / the
    // user's explicit "no locking" call for this phase).
    m_ViewportWindow->MaybeWarnEditDuringPlay(this);

    try {
        polyizon::SaveScene(renderer->GetScene(), *m_CurrentScenePath);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to save scene", e.what());
    }
}

void MainWindow::OnBuildGame() {
    if (!m_CurrentProject) {
        return; // nothing to export until a project is loaded
    }

    const QString outputDir = QFileDialog::getExistingDirectory(this, "Choose an output folder for the built game");
    if (outputDir.isEmpty()) {
        return; // user cancelled
    }

    try {
        ::BuildGame(*m_CurrentProject, std::filesystem::path(outputDir.toStdWString()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to build game", e.what());
        return;
    }

    QMessageBox::information(this, "Build Game",
        QString("\"%1\" was built successfully to:\n%2").arg(
            QString::fromStdString(m_CurrentProject->GetName()), outputDir));
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
    m_ViewportWindow->SetSelectedEntity(entt::null);
    // A new scene always starts Stopped (see EditorViewportRenderer::
    // LoadScene) — UpdatePlayActionsEnabled() is also connected to
    // SceneLoaded, but that fires the deferred exposeEvent-path load too,
    // which this call already covers for the synchronous path.
    UpdatePlayActionsEnabled();
}

void MainWindow::OnPlay() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    if (renderer->GetPlayState() == polyizon::PlayState::Stopped) {
        // A fresh Play session — the one-time edit-during-play heads-up
        // should be able to fire again (it's per-session, not per-app-run).
        m_ViewportWindow->ResetEditWarning();
    }
    renderer->Play();
    UpdatePlayActionsEnabled();
}

void MainWindow::OnPause() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    renderer->Pause();
    UpdatePlayActionsEnabled();
}

void MainWindow::OnStop() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }
    renderer->Stop();
    // Stop() replaces m_Scene wholesale (reverting to the pre-Play snapshot),
    // same "the old selection/hierarchy no longer necessarily mean anything"
    // situation as LoadSceneFile — refresh the same way it does.
    m_HierarchyPanel->Refresh();
    m_InspectorPanel->SetSelectedEntity(entt::null);
    m_ViewportWindow->SetSelectedEntity(entt::null);
    UpdatePlayActionsEnabled();
}

void MainWindow::OnOpenLightingSettings() {
    if (!m_LightingSettingsDialog) {
        m_LightingSettingsDialog = new LightingSettingsDialog(m_ViewportWindow, this);
    }
    m_LightingSettingsDialog->show();
    m_LightingSettingsDialog->raise();
    m_LightingSettingsDialog->activateWindow();
}

void MainWindow::OnCredits() {
    QMessageBox::about(this, "Credits",
        "<h3>Polyizon</h3>"
        "<p><b>Freezedown</b> &mdash; Creator</p>"
        "<p><b>Claude</b> &mdash; Assistance</p>");
}

void MainWindow::UpdatePlayActionsEnabled() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    // Same gate as m_SaveSceneAction: Play/Pause/Stop are meaningless with no
    // scene loaded at all.
    const bool hasScene = renderer != nullptr && m_CurrentScenePath.has_value();
    const polyizon::PlayState state = renderer ? renderer->GetPlayState() : polyizon::PlayState::Stopped;

    m_PlayAction->setEnabled(hasScene && state != polyizon::PlayState::Playing);
    m_PauseAction->setEnabled(hasScene && state == polyizon::PlayState::Playing);
    m_StopAction->setEnabled(hasScene && state != polyizon::PlayState::Stopped);
}
