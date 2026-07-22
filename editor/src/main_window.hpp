#pragma once

#include <QMainWindow>

class QTimer;
class VulkanViewportWindow;

// Editor's top-level window: hosts VulkanViewportWindow as the central
// widget via QWidget::createWindowContainer. No docked panels yet
// (hierarchy/inspector/cloud-settings are Phase 15+) — just the viewport
// filling the window, confirming the Qt+Vulkan embedding mechanism works.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    VulkanViewportWindow* m_ViewportWindow = nullptr; // owned by its container widget, not directly by this
    QTimer* m_RenderTimer = nullptr; // owned by Qt's parent-child hierarchy (parented to this)
};
