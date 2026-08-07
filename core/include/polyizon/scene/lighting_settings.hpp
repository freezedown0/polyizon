#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Scene-level lighting data, owned by Scene (see scene.hpp) and persisted by
// SceneSerializer — replaces what used to be EditorViewportRenderer's own
// hardcoded m_SunElevationDegrees/m_SunAzimuthDegrees/m_AmbientStrength
// fields, so different scenes can have different sun positions and lighting.
// The sun fields also provide a compatibility fallback when no authored
// DirectionalLightComponent exists. Default values match the old editor, so
// scene files that predate this struct (missing the "lighting" JSON key)
// load with identical behavior to before.
struct SceneLightingSettings {
    float sunElevationDegrees = 25.0f;
    float sunAzimuthDegrees = 0.0f;
    glm::vec3 directionalLightColor{1.0f, 0.95f, 0.85f};
    float directionalLightIntensity = 1.0f;
    float ambientStrength = 0.15f;
    float skyExposure = 1.15f;
    float skySunIntensity = 10.0f;

    bool cloudsEnabled = true;
    float cloudLayerBottomKm = 1.5f;
    float cloudLayerTopKm = 4.0f;
    float cloudCoverage = 0.22f;
    float cloudDensity = 0.75f;
    float cloudNoiseScale = 0.14f;
    float cloudWindSpeed = 0.0015f;
    float cloudWindDirectionDegrees = 0.0f;
    float cloudPowderStrength = 0.8f;
    float cloudAmbientStrength = 0.25f;
    int cloudPrimarySteps = 80;
    int cloudShadowSteps = 10;
};

} // namespace polyizon
