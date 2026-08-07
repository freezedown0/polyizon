#include "lighting_settings_dialog.hpp"

#include "editor_viewport_renderer.hpp"
#include "vulkan_viewport_window.hpp"

#include "polyizon/scene/lighting_settings.hpp"
#include "polyizon/scene/components.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

struct DirectionalLightSelection {
    polyizon::TransformComponent* transform = nullptr;
    polyizon::DirectionalLightComponent* light = nullptr;
};

DirectionalLightSelection FindDirectionalLight(polyizon::EditorViewportRenderer* renderer) {
    if (!renderer) return {};
    auto& registry = renderer->GetScene().GetRegistry();
    const auto view = registry.view<polyizon::TransformComponent, polyizon::DirectionalLightComponent>();
    if (view.begin() == view.end()) return {};
    const entt::entity entity = *view.begin();
    return {
        &view.get<polyizon::TransformComponent>(entity),
        &view.get<polyizon::DirectionalLightComponent>(entity)
    };
}

QDoubleSpinBox* MakeDoubleSpin(QWidget* parent, double minimum, double maximum,
    double step, int decimals, const QString& suffix = {}) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight);
    spin->setKeyboardTracking(false);
    return spin;
}

QWidget* MakeTab(const QString& description, QFormLayout*& form, QWidget* parent) {
    auto* tab = new QWidget(parent);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);
    auto* label = new QLabel(description, tab);
    label->setObjectName("DialogDescription");
    label->setWordWrap(true);
    layout->addWidget(label);
    form = new QFormLayout();
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    layout->addStretch();
    return tab;
}

} // namespace

LightingSettingsDialog::LightingSettingsDialog(VulkanViewportWindow* viewportWindow, QWidget* parent)
    : QDialog(parent), m_ViewportWindow(viewportWindow) {
    setWindowTitle("Environment Editor");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(540, 700);
    setMinimumSize(460, 480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_Tabs = new QTabWidget(this);
    root->addWidget(m_Tabs);

    QFormLayout* lightForm = nullptr;
    QWidget* lightTab = MakeTab(
        "The directional light drives the sun, surface shading, and realistic shadows.", lightForm, m_Tabs);
    m_ElevationSpin = MakeDoubleSpin(lightTab, -10.0, 90.0, 1.0, 1, "°");
    m_AzimuthSpin = MakeDoubleSpin(lightTab, 0.0, 360.0, 1.0, 1, "°");
    m_SunColorButton = new QPushButton("Choose Color", lightTab);
    m_SunIntensitySpin = MakeDoubleSpin(lightTab, 0.0, 20.0, 0.1, 2);
    lightForm->addRow("Elevation", m_ElevationSpin);
    lightForm->addRow("Azimuth", m_AzimuthSpin);
    lightForm->addRow("Light Color", m_SunColorButton);
    lightForm->addRow("Intensity", m_SunIntensitySpin);
    m_Tabs->addTab(lightTab, "Directional Light");

    QFormLayout* skyForm = nullptr;
    QWidget* skyTab = MakeTab(
        "Tune atmospheric brightness independently from the directional light.", skyForm, m_Tabs);
    m_SkyExposureSpin = MakeDoubleSpin(skyTab, 0.1, 5.0, 0.05, 2);
    m_SkySunIntensitySpin = MakeDoubleSpin(skyTab, 0.0, 40.0, 0.5, 1);
    m_AmbientSpin = MakeDoubleSpin(skyTab, 0.0, 1.0, 0.01, 2);
    skyForm->addRow("Sky Exposure", m_SkyExposureSpin);
    skyForm->addRow("Sun Scattering", m_SkySunIntensitySpin);
    skyForm->addRow("Ambient Fill", m_AmbientSpin);
    m_Tabs->addTab(skyTab, "Sky Environment");

    auto* cloudsTab = new QWidget(m_Tabs);
    auto* cloudsLayout = new QVBoxLayout(cloudsTab);
    cloudsLayout->setContentsMargins(14, 14, 14, 14);
    cloudsLayout->setSpacing(12);
    m_CloudsEnabledCheck = new QCheckBox("Enable Volumetric Clouds", cloudsTab);
    m_CloudsEnabledCheck->setObjectName("FeatureToggle");
    cloudsLayout->addWidget(m_CloudsEnabledCheck);
    auto* cloudDescription = new QLabel(
        "Lower coverage creates broad clean sky regions. Disabling clouds skips their raymarch entirely.", cloudsTab);
    cloudDescription->setObjectName("DialogDescription");
    cloudDescription->setWordWrap(true);
    cloudsLayout->addWidget(cloudDescription);

    m_CloudControls = new QWidget(cloudsTab);
    auto* cloudForm = new QFormLayout(m_CloudControls);
    cloudForm->setHorizontalSpacing(24);
    cloudForm->setVerticalSpacing(9);
    cloudForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_CloudCoverageSpin = MakeDoubleSpin(m_CloudControls, 0.0, 1.0, 0.01, 2);
    m_CloudDensitySpin = MakeDoubleSpin(m_CloudControls, 0.0, 2.0, 0.05, 2);
    m_CloudBottomSpin = MakeDoubleSpin(m_CloudControls, 0.2, 10.0, 0.1, 1, " km");
    m_CloudTopSpin = MakeDoubleSpin(m_CloudControls, 0.3, 15.0, 0.1, 1, " km");
    m_CloudScaleSpin = MakeDoubleSpin(m_CloudControls, 0.02, 0.5, 0.01, 3);
    m_CloudWindSpeedSpin = MakeDoubleSpin(m_CloudControls, 0.0, 0.02, 0.0005, 4);
    m_CloudWindDirectionSpin = MakeDoubleSpin(m_CloudControls, 0.0, 360.0, 5.0, 1, "°");
    m_CloudPowderSpin = MakeDoubleSpin(m_CloudControls, 0.0, 2.0, 0.05, 2);
    m_CloudAmbientSpin = MakeDoubleSpin(m_CloudControls, 0.0, 2.0, 0.05, 2);
    m_CloudStepsSpin = new QSpinBox(m_CloudControls);
    m_CloudStepsSpin->setRange(24, 160);
    m_CloudStepsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_CloudShadowStepsSpin = new QSpinBox(m_CloudControls);
    m_CloudShadowStepsSpin->setRange(4, 24);
    m_CloudShadowStepsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    cloudForm->addRow("Coverage", m_CloudCoverageSpin);
    cloudForm->addRow("Density", m_CloudDensitySpin);
    cloudForm->addRow("Layer Bottom", m_CloudBottomSpin);
    cloudForm->addRow("Layer Top", m_CloudTopSpin);
    cloudForm->addRow("Shape Scale", m_CloudScaleSpin);
    cloudForm->addRow("Wind Speed", m_CloudWindSpeedSpin);
    cloudForm->addRow("Wind Direction", m_CloudWindDirectionSpin);
    cloudForm->addRow("Edge Lighting", m_CloudPowderSpin);
    cloudForm->addRow("Ambient Light", m_CloudAmbientSpin);
    cloudForm->addRow("Quality Steps", m_CloudStepsSpin);
    cloudForm->addRow("Shadow Steps", m_CloudShadowStepsSpin);
    cloudsLayout->addWidget(m_CloudControls);
    cloudsLayout->addStretch();
    m_Tabs->addTab(cloudsTab, "Clouds");

    auto sceneSettings = [this]() -> polyizon::SceneLightingSettings* {
        auto* renderer = m_ViewportWindow->GetRenderer();
        return renderer ? &renderer->GetScene().GetLightingSettings() : nullptr;
    };
    auto connectDouble = [this, sceneSettings](QDoubleSpinBox* spin, float polyizon::SceneLightingSettings::*member) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this, sceneSettings, member](double value) {
                if (auto* settings = sceneSettings()) {
                    m_ViewportWindow->MaybeWarnEditDuringPlay(this);
                    settings->*member = static_cast<float>(value);
                }
            });
    };
    connectDouble(m_AmbientSpin, &polyizon::SceneLightingSettings::ambientStrength);
    connectDouble(m_SkyExposureSpin, &polyizon::SceneLightingSettings::skyExposure);
    connectDouble(m_SkySunIntensitySpin, &polyizon::SceneLightingSettings::skySunIntensity);
    connectDouble(m_CloudCoverageSpin, &polyizon::SceneLightingSettings::cloudCoverage);
    connectDouble(m_CloudDensitySpin, &polyizon::SceneLightingSettings::cloudDensity);
    connectDouble(m_CloudBottomSpin, &polyizon::SceneLightingSettings::cloudLayerBottomKm);
    connectDouble(m_CloudTopSpin, &polyizon::SceneLightingSettings::cloudLayerTopKm);
    connectDouble(m_CloudScaleSpin, &polyizon::SceneLightingSettings::cloudNoiseScale);
    connectDouble(m_CloudWindSpeedSpin, &polyizon::SceneLightingSettings::cloudWindSpeed);
    connectDouble(m_CloudWindDirectionSpin, &polyizon::SceneLightingSettings::cloudWindDirectionDegrees);
    connectDouble(m_CloudPowderSpin, &polyizon::SceneLightingSettings::cloudPowderStrength);
    connectDouble(m_CloudAmbientSpin, &polyizon::SceneLightingSettings::cloudAmbientStrength);

    auto updateDirectionalRotation = [this, sceneSettings]() {
        auto* renderer = m_ViewportWindow->GetRenderer();
        const DirectionalLightSelection selection = FindDirectionalLight(renderer);
        if (!selection.transform) return;
        m_ViewportWindow->MaybeWarnEditDuringPlay(this);
        selection.transform->rotationEulerDegrees.x = -static_cast<float>(m_ElevationSpin->value());
        selection.transform->rotationEulerDegrees.y = 90.0f - static_cast<float>(m_AzimuthSpin->value());
        selection.transform->rotationEulerDegrees.z = 0.0f;

        // Keep the atmospheric sun aligned with the authored light. These
        // values also remain the compatibility fallback for scenes with no
        // DirectionalLightComponent.
        if (auto* settings = sceneSettings()) {
            settings->sunElevationDegrees = static_cast<float>(m_ElevationSpin->value());
            settings->sunAzimuthDegrees = static_cast<float>(m_AzimuthSpin->value());
        }
    };
    connect(m_ElevationSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [updateDirectionalRotation](double) { updateDirectionalRotation(); });
    connect(m_AzimuthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [updateDirectionalRotation](double) { updateDirectionalRotation(); });
    connect(m_SunIntensitySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this, sceneSettings](double value) {
            const DirectionalLightSelection selection = FindDirectionalLight(m_ViewportWindow->GetRenderer());
            if (!selection.light) return;
            m_ViewportWindow->MaybeWarnEditDuringPlay(this);
            selection.light->intensity = static_cast<float>(value);
            if (auto* settings = sceneSettings()) settings->directionalLightIntensity = static_cast<float>(value);
        });

    connect(m_CloudsEnabledCheck, &QCheckBox::toggled, this, [this, sceneSettings](bool enabled) {
        if (auto* settings = sceneSettings()) {
            m_ViewportWindow->MaybeWarnEditDuringPlay(this);
            settings->cloudsEnabled = enabled;
        }
        UpdateCloudControlsEnabled();
    });
    connect(m_CloudStepsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, sceneSettings](int value) {
        if (auto* settings = sceneSettings()) settings->cloudPrimarySteps = value;
    });
    connect(m_CloudShadowStepsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, sceneSettings](int value) {
        if (auto* settings = sceneSettings()) settings->cloudShadowSteps = value;
    });
    connect(m_SunColorButton, &QPushButton::clicked, this, [this, sceneSettings]() {
        const DirectionalLightSelection selection = FindDirectionalLight(m_ViewportWindow->GetRenderer());
        if (!selection.light) return;
        QColor current = QColor::fromRgbF(selection.light->color.r,
            selection.light->color.g, selection.light->color.b);
        QColor chosen = QColorDialog::getColor(current, this, "Directional Light Color");
        if (!chosen.isValid()) return;
        m_ViewportWindow->MaybeWarnEditDuringPlay(this);
        selection.light->color = glm::vec3(chosen.redF(), chosen.greenF(), chosen.blueF());
        if (auto* settings = sceneSettings()) settings->directionalLightColor = selection.light->color;
        UpdateSunColorButton();
    });

    connect(m_ViewportWindow, &VulkanViewportWindow::SceneLoaded, this, &LightingSettingsDialog::RefreshFromScene);
    RefreshFromScene();
}

void LightingSettingsDialog::UpdateCloudControlsEnabled() {
    m_CloudControls->setEnabled(m_CloudsEnabledCheck->isChecked());
}

void LightingSettingsDialog::UpdateSunColorButton() {
    auto* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) return;
    const DirectionalLightSelection selection = FindDirectionalLight(renderer);
    const glm::vec3 color = selection.light
        ? selection.light->color
        : renderer->GetScene().GetLightingSettings().directionalLightColor;
    QColor qcolor = QColor::fromRgbF(color.r, color.g, color.b);
    m_SunColorButton->setStyleSheet(QString("background-color:%1; color:%2;")
        .arg(qcolor.name(), qcolor.lightnessF() > 0.55 ? "#111318" : "#f5f7fb"));
}

void LightingSettingsDialog::RefreshFromScene() {
    auto* renderer = m_ViewportWindow->GetRenderer();
    if (!renderer) {
        setEnabled(false);
        return;
    }
    setEnabled(true);
    const auto& s = renderer->GetScene().GetLightingSettings();
    const DirectionalLightSelection selection = FindDirectionalLight(renderer);
    const QSignalBlocker blockers[] = {
        QSignalBlocker(m_ElevationSpin), QSignalBlocker(m_AzimuthSpin), QSignalBlocker(m_SunIntensitySpin),
        QSignalBlocker(m_AmbientSpin), QSignalBlocker(m_SkyExposureSpin), QSignalBlocker(m_SkySunIntensitySpin),
        QSignalBlocker(m_CloudsEnabledCheck), QSignalBlocker(m_CloudCoverageSpin), QSignalBlocker(m_CloudDensitySpin),
        QSignalBlocker(m_CloudBottomSpin), QSignalBlocker(m_CloudTopSpin), QSignalBlocker(m_CloudScaleSpin),
        QSignalBlocker(m_CloudWindSpeedSpin), QSignalBlocker(m_CloudWindDirectionSpin),
        QSignalBlocker(m_CloudPowderSpin), QSignalBlocker(m_CloudAmbientSpin),
        QSignalBlocker(m_CloudStepsSpin), QSignalBlocker(m_CloudShadowStepsSpin)
    };
    const float elevation = selection.transform ? -selection.transform->rotationEulerDegrees.x : s.sunElevationDegrees;
    float azimuth = selection.transform ? 90.0f - selection.transform->rotationEulerDegrees.y : s.sunAzimuthDegrees;
    while (azimuth < 0.0f) azimuth += 360.0f;
    while (azimuth > 360.0f) azimuth -= 360.0f;
    m_ElevationSpin->setValue(elevation);
    m_AzimuthSpin->setValue(azimuth);
    m_SunIntensitySpin->setValue(selection.light ? selection.light->intensity : s.directionalLightIntensity);
    m_ElevationSpin->setEnabled(selection.transform != nullptr);
    m_AzimuthSpin->setEnabled(selection.transform != nullptr);
    m_SunColorButton->setEnabled(selection.light != nullptr);
    m_SunIntensitySpin->setEnabled(selection.light != nullptr);
    m_AmbientSpin->setValue(s.ambientStrength);
    m_SkyExposureSpin->setValue(s.skyExposure);
    m_SkySunIntensitySpin->setValue(s.skySunIntensity);
    m_CloudsEnabledCheck->setChecked(s.cloudsEnabled);
    m_CloudCoverageSpin->setValue(s.cloudCoverage);
    m_CloudDensitySpin->setValue(s.cloudDensity);
    m_CloudBottomSpin->setValue(s.cloudLayerBottomKm);
    m_CloudTopSpin->setValue(s.cloudLayerTopKm);
    m_CloudScaleSpin->setValue(s.cloudNoiseScale);
    m_CloudWindSpeedSpin->setValue(s.cloudWindSpeed);
    m_CloudWindDirectionSpin->setValue(s.cloudWindDirectionDegrees);
    m_CloudPowderSpin->setValue(s.cloudPowderStrength);
    m_CloudAmbientSpin->setValue(s.cloudAmbientStrength);
    m_CloudStepsSpin->setValue(s.cloudPrimarySteps);
    m_CloudShadowStepsSpin->setValue(s.cloudShadowSteps);
    UpdateCloudControlsEnabled();
    UpdateSunColorButton();
}
