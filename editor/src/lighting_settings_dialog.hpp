#pragma once

#include <QDialog>

class QDoubleSpinBox;
class VulkanViewportWindow;

// Phase 19: the scene-wide sun/ambient sliders SceneLightingSettings has had
// since Phase 18 (sunElevationDegrees/sunAzimuthDegrees/ambientStrength) but
// no UI could edit until now — hand-edited via scene JSON only before this.
// Modeless (MainWindow shows it with show(), never exec()) so the render
// timer keeps running and dragging a slider previews live in the viewport,
// the same "immediate write into the live scene" convention InspectorPanel's
// fields already use. Deliberately separate from InspectorPanel: these
// settings are scene-level, not per-entity, so they don't belong in a panel
// that rebuilds around whichever entity Hierarchy last selected.
//
// Mode (Realistic/Voxel) stays on MainWindow's Lighting menu, not duplicated
// here — one source of truth for it.
class LightingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit LightingSettingsDialog(VulkanViewportWindow* viewportWindow, QWidget* parent = nullptr);

public slots:
    // Connected to VulkanViewportWindow::SceneLoaded — a different scene (or
    // Stop's revert) may have entirely different lighting values, so the
    // displayed sliders must be refreshed from whatever's actually live
    // rather than going stale.
    void RefreshFromScene();

private:
    VulkanViewportWindow* m_ViewportWindow;

    QDoubleSpinBox* m_ElevationSpin = nullptr;
    QDoubleSpinBox* m_AzimuthSpin = nullptr;
    QDoubleSpinBox* m_AmbientSpin = nullptr;
};
