#pragma once

namespace polyizon {

// Realistic: raymarched atmosphere + volumetric clouds, a single 2048²
// hardware-PCF directional shadow (see sky_pipeline.hpp/lit_pipeline.hpp) —
// everything the engine has done since Phase 15. Voxel: a flat gradient sky
// with no clouds and a deliberately coarse 4x4x4 shadow volume (see
// classic_sky_pipeline.hpp/voxel_lit_pipeline.hpp) for a blocky, stylized
// look. Chosen per-scene (see SceneLightingSettings below), not globally —
// EditorViewportRenderer keeps both rendering paths resident and picks one
// per frame from whichever scene is currently loaded.
enum class LightingMode {
    Realistic,
    Voxel,
};

// Scene-level lighting data, owned by Scene (see scene.hpp) and persisted by
// SceneSerializer — replaces what used to be EditorViewportRenderer's own
// hardcoded m_SunElevationDegrees/m_SunAzimuthDegrees/m_AmbientStrength
// fields, so different scenes can have different sun positions/lighting
// modes rather than one fixed value for the whole editor session. Default
// values match what EditorViewportRenderer hardcoded before Phase 18, so
// scene files that predate this struct (missing the "lighting" JSON key)
// load with identical behavior to before.
struct SceneLightingSettings {
    LightingMode mode = LightingMode::Realistic;
    float sunElevationDegrees = 25.0f;
    float sunAzimuthDegrees = 0.0f;
    float ambientStrength = 0.15f;
};

} // namespace polyizon
