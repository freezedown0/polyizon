#pragma once

#include "polyizon/scene/components.hpp"

#include <sol/sol.hpp>

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>

namespace polyizon {

// The engine's scripting runtime: Lua via sol2. Deliberately small API
// surface exposed to scripts — just a TransformComponent (position/rotation/
// scale) and deltaTime, no full ECS/registry access — see the Phase 16 plan
// for why (this proves the binding round-trips correctly; a broader script
// API is a later phase's concern once real gameplay needs exist). Lives in
// core (Phase 20) rather than editor-only — the standalone game client runs
// scripts too, to make a compiled/exported project actually playable outside
// the editor (see Application's project mode).
//
// Each distinct script file gets its own sol::environment (a private globals
// table layered over the shared Lua state) rather than sharing one global
// namespace — without this, two entities running two different scripts
// would silently stomp each other's top-level `onUpdate` function since Lua
// globals are otherwise all one shared table.
class ScriptEngine {
public:
    ScriptEngine();

    // Iterates every entity with both a TransformComponent and a
    // ScriptComponent (see polyizon/scene/components.hpp), lazily loading
    // and caching each distinct script file on first use, then calling its
    // Lua `onUpdate(transform, deltaTime)` function.
    void Update(entt::registry& registry, float deltaTime);

private:
    struct LoadedScript {
        sol::environment env;
        sol::protected_function onUpdate;
    };

    // scriptPath (see ScriptComponent) is resolved relative to the running
    // executable's own directory — same convention as Image::CreateFromFile
    // resolving "textures/foo.png" — and the loaded/compiled result is
    // cached here, keyed by that same path string.
    LoadedScript& GetOrLoadScript(const std::string& scriptPath);

    sol::state m_Lua;
    std::unordered_map<std::string, LoadedScript> m_LoadedScripts;
};

} // namespace polyizon
