#include "main_window.hpp"

#include "vulkan_viewport_window.hpp"

#include <QTimer>
#include <QWidget>

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

    m_RenderTimer = new QTimer(this);
    connect(m_RenderTimer, &QTimer::timeout, this, [this]() { m_ViewportWindow->RenderIfExposed(); });
    m_RenderTimer->start(kRenderIntervalMs);
}
