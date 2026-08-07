#pragma once

#include <QIcon>

#include <entt/entt.hpp>

enum class ComponentIconKind {
    Entity,
    Transform,
    Mesh,
    Material,
    Script,
    DirectionalLight,
    PointLight,
    SpotLight,
    Environment,
    LightBake,
};

// Vector-drawn editor icons stay sharp at every Windows scale factor and do
// not add a fragile collection of external image files to deployed builds.
QIcon GetComponentIcon(ComponentIconKind kind, int logicalSize = 18);
QIcon GetEntityIcon(const entt::registry& registry, entt::entity entity, int logicalSize = 18);

