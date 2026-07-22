#pragma once

#include <entt/entt.hpp>

namespace polyizon {

// Thin wrapper over an EnTT registry — the engine's first actual ECS usage
// (EnTT has been linked since early on but never used until now). Not
// over-built: just enough for a caller (see EditorViewportRenderer) to
// create entities and iterate components. No serialization, no hierarchy/
// parent-child relationships, no systems abstraction yet — those are a
// later phase, once there's an authored scene worth persisting/editing (see
// the Phase 15 plan's scope notes).
class Scene {
public:
    entt::entity CreateEntity() { return m_Registry.create(); }

    entt::registry& GetRegistry() noexcept { return m_Registry; }
    const entt::registry& GetRegistry() const noexcept { return m_Registry; }

private:
    entt::registry m_Registry;
};

} // namespace polyizon
