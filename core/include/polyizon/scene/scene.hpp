#pragma once

#include "polyizon/scene/lighting_settings.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/scene/entity_uuid.hpp"

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
// Also owns scene-level (not per-entity) environment settings so different
// scenes can have different sky and cloud authoring.
class Scene {
public:
    entt::entity CreateEntity() {
        const entt::entity entity = m_Registry.create();
        m_Registry.emplace<IdentityComponent>(entity, GenerateEntityUuid());
        m_Registry.emplace<EntityMetadataComponent>(entity);
        return entity;
    }

    entt::registry& GetRegistry() noexcept { return m_Registry; }
    const entt::registry& GetRegistry() const noexcept { return m_Registry; }

    SceneLightingSettings& GetLightingSettings() noexcept { return m_LightingSettings; }
    const SceneLightingSettings& GetLightingSettings() const noexcept { return m_LightingSettings; }

private:
    entt::registry m_Registry;
    SceneLightingSettings m_LightingSettings;
};

} // namespace polyizon
