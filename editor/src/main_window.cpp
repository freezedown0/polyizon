#include "main_window.hpp"

#include "vulkan_viewport_window.hpp"

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

    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New Project...", this, &MainWindow::OnNewProject);
    fileMenu->addAction("&Open Project...", this, &MainWindow::OnOpenProject);

    m_RenderTimer = new QTimer(this);
    connect(m_RenderTimer, &QTimer::timeout, this, [this]() { m_ViewportWindow->RenderIfExposed(); });
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
        auto project = std::make_unique<Project>(
            Project::CreateNew(name.toStdString(), std::filesystem::path(parentDir.toStdString())));
        OpenProjectScene(*project);
        m_CurrentProject = std::move(project);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to create project", e.what());
    }
}

void MainWindow::OnOpenProject() {
    const QString manifestFile = QFileDialog::getOpenFileName(
        this, "Open Project", QString(), "Polyizon project (project.json)");
    if (manifestFile.isEmpty()) {
        return; // user cancelled
    }

    try {
        auto project = std::make_unique<Project>(Project::Open(std::filesystem::path(manifestFile.toStdString())));
        OpenProjectScene(*project);
        m_CurrentProject = std::move(project);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Failed to open project", e.what());
    }
}

void MainWindow::OpenProjectScene(const Project& project) {
    m_ViewportWindow->LoadScene(project.GetDefaultScenePath());
    setWindowTitle(QString("Polyizon Editor - %1").arg(QString::fromStdString(project.GetName())));
}
