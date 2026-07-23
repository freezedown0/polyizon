#include "lighting_settings_dialog.hpp"

#include "editor_viewport_renderer.hpp"
#include "vulkan_viewport_window.hpp"

#include "polyizon/scene/lighting_settings.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>

LightingSettingsDialog::LightingSettingsDialog(VulkanViewportWindow* viewportWindow, QWidget* parent)
    : QDialog(parent), m_ViewportWindow(viewportWindow) {
    setWindowTitle("Lighting Settings");

    auto* form = new QFormLayout(this);

    m_ElevationSpin = new QDoubleSpinBox(this);
    m_ElevationSpin->setRange(-90.0, 90.0);
    m_ElevationSpin->setDecimals(1);
    m_ElevationSpin->setSingleStep(1.0);
    form->addRow("Sun Elevation", m_ElevationSpin);

    m_AzimuthSpin = new QDoubleSpinBox(this);
    m_AzimuthSpin->setRange(0.0, 360.0);
    m_AzimuthSpin->setDecimals(1);
    m_AzimuthSpin->setSingleStep(1.0);
    form->addRow("Sun Azimuth", m_AzimuthSpin);

    m_AmbientSpin = new QDoubleSpinBox(this);
    m_AmbientSpin->setRange(0.0, 1.0);
    m_AmbientSpin->setDecimals(2);
    m_AmbientSpin->setSingleStep(0.01);
    form->addRow("Ambient Strength", m_AmbientSpin);

    connect(m_ElevationSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
        if (!renderer) {
            return;
        }
        m_ViewportWindow->MaybeWarnEditDuringPlay(this);
        renderer->GetScene().GetLightingSettings().sunElevationDegrees = static_cast<float>(value);
    });
    connect(m_AzimuthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
        if (!renderer) {
            return;
        }
        m_ViewportWindow->MaybeWarnEditDuringPlay(this);
        renderer->GetScene().GetLightingSettings().sunAzimuthDegrees = static_cast<float>(value);
    });
    connect(m_AmbientSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
        if (!renderer) {
            return;
        }
        m_ViewportWindow->MaybeWarnEditDuringPlay(this);
        renderer->GetScene().GetLightingSettings().ambientStrength = static_cast<float>(value);
    });

    connect(m_ViewportWindow, &VulkanViewportWindow::SceneLoaded, this, &LightingSettingsDialog::RefreshFromScene);

    RefreshFromScene();
}

void LightingSettingsDialog::RefreshFromScene() {
    polyizon::EditorViewportRenderer* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        return;
    }

    const polyizon::SceneLightingSettings& lighting = renderer->GetScene().GetLightingSettings();

    // Setting these to values freshly read from the scene would otherwise
    // re-trigger valueChanged -> write straight back into the same scene —
    // harmless (idempotent) but pointless; blocked purely to avoid the
    // redundant round-trip, not because it would misbehave.
    const QSignalBlocker elevationBlocker(m_ElevationSpin);
    const QSignalBlocker azimuthBlocker(m_AzimuthSpin);
    const QSignalBlocker ambientBlocker(m_AmbientSpin);

    m_ElevationSpin->setValue(lighting.sunElevationDegrees);
    m_AzimuthSpin->setValue(lighting.sunAzimuthDegrees);
    m_AmbientSpin->setValue(lighting.ambientStrength);
}
