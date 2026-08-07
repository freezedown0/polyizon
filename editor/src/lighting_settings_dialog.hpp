#pragma once

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QWidget;
class VulkanViewportWindow;

class LightingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit LightingSettingsDialog(VulkanViewportWindow* viewportWindow, QWidget* parent = nullptr);

public slots:
    void RefreshFromScene();

private:
    void UpdateCloudControlsEnabled();
    void UpdateSunColorButton();

    VulkanViewportWindow* m_ViewportWindow;
    QTabWidget* m_Tabs = nullptr;

    QDoubleSpinBox* m_ElevationSpin = nullptr;
    QDoubleSpinBox* m_AzimuthSpin = nullptr;
    QPushButton* m_SunColorButton = nullptr;
    QDoubleSpinBox* m_SunIntensitySpin = nullptr;
    QDoubleSpinBox* m_AmbientSpin = nullptr;

    QDoubleSpinBox* m_SkyExposureSpin = nullptr;
    QDoubleSpinBox* m_SkySunIntensitySpin = nullptr;

    QCheckBox* m_CloudsEnabledCheck = nullptr;
    QWidget* m_CloudControls = nullptr;
    QDoubleSpinBox* m_CloudCoverageSpin = nullptr;
    QDoubleSpinBox* m_CloudDensitySpin = nullptr;
    QDoubleSpinBox* m_CloudBottomSpin = nullptr;
    QDoubleSpinBox* m_CloudTopSpin = nullptr;
    QDoubleSpinBox* m_CloudScaleSpin = nullptr;
    QDoubleSpinBox* m_CloudWindSpeedSpin = nullptr;
    QDoubleSpinBox* m_CloudWindDirectionSpin = nullptr;
    QDoubleSpinBox* m_CloudPowderSpin = nullptr;
    QDoubleSpinBox* m_CloudAmbientSpin = nullptr;
    QSpinBox* m_CloudStepsSpin = nullptr;
    QSpinBox* m_CloudShadowStepsSpin = nullptr;
};
