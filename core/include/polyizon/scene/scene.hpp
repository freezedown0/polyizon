#pragma once

#include "polyizon/scene/lighting_settings.hpp"

#include <entt/entt.hpp>

namespace polyizon {

// Thin wrapper over an EnTT registry — the engine's first actual ECS usage
// (EnTT has been linked since early on but never used until now). Not
// over-built: just enough for a caller (see EditorViewportRenderer) to
// create entities and iterate components. No hierarchy/parent-child
// relationships, no systems abstraction yet — those are a later phase, once
// there's an authored scene worth persisting/editing (see the Phase 15
// plan's scope notes).
//
// Also owns scene-level (not per-entity) lighting settings (see
// lighting_settings.hpp) — Phase 18 moved these here from being
// EditorViewportRenderer's own hardcoded fields, so different scenes can
// have different sun positions/lighting modes.
class Scene {
public:
    entt::entity CreateEntity() { return m_Registry.create(); }

    entt::registry& GetRegistry() noexcept { return m_Registry; }
    const entt::registry& GetRegistry() const noexcept { return m_Registry; }

    SceneLightingSettings& GetLightingSettings() noexcept { return m_LightingSettings; }
    const SceneLightingSettings& GetLightingSettings() const noexcept { return m_LightingSettings; }

private:
    entt::registry m_Registry;
    SceneLightingSettings m_LightingSettings;
};

} // namespace polyizon
