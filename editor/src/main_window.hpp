#pragma once

#include "project.hpp"

#include <QMainWindow>

#include <memory>

class QTimer;
class VulkanViewportWindow;

// Editor's top-level window: hosts VulkanViewportWindow as the central
// widget via QWidget::createWindowContainer. No docked panels yet
// (hierarchy/inspector/cloud-settings remain a later phase) — just the
// viewport filling the window.
//
// Phase 16: gained a File menu (New/Open Project) — the viewport starts
// empty (see EditorViewportRenderer) until one of those loads a project's
// default scene into it. No "recent projects" list or project browser this
// phase.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void OnNewProject();
    void OnOpenProject();

private:
    // Common tail of both handlers: points the viewport at the project's
    // default scene and updates the window title.
    void OpenProjectScene(const Project& project);

    VulkanViewportWindow* m_ViewportWindow = nullptr; // owned by its container widget, not directly by this
    QTimer* m_RenderTimer = nullptr; // owned by Qt's parent-child hierarchy (parented to this)
    std::unique_ptr<Project> m_CurrentProject;
};
