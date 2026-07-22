#include "script_engine.hpp"

#include "polyizon/log.hpp"

#include <glm/glm.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <stdexcept>

namespace {

// Resolved relative to the running executable's own directory, matching
// every other asset-loading class's identical private helper (Image,
// GraphicsPipeline, EditorViewportRenderer, etc.) — duplicated here rather
// than shared for the same reason they duplicate it from each other.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

} // namespace

ScriptEngine::ScriptEngine() {
    m_Lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);

    // Deliberately minimal binding surface: only what spin.lua (and any
    // similarly-scoped future test script) needs to prove the round-trip —
    // see the header's doc comment for why the API stays this small.
    m_Lua.new_usertype<glm::vec3>("Vec3",
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z);

    m_Lua.new_usertype<polyizon::TransformComponent>("Transform",
        "position", &polyizon::TransformComponent::position,
        "rotationEulerDegrees", &polyizon::TransformComponent::rotationEulerDegrees,
        "scale", &polyizon::TransformComponent::scale);
}

void ScriptEngine::Update(entt::registry& registry, float deltaTime) {
    auto view = registry.view<polyizon::TransformComponent, polyizon::ScriptComponent>();
    for (auto entity : view) {
        const auto& [transform, scriptComponent] =
            view.get<polyizon::TransformComponent, polyizon::ScriptComponent>(entity);
        if (scriptComponent.scriptPath.empty()) {
            continue;
        }

        LoadedScript& script = GetOrLoadScript(scriptComponent.scriptPath);
        if (!script.onUpdate.valid()) {
            continue; // script file has no onUpdate function defined - nothing to call this frame
        }

        const sol::protected_function_result result = script.onUpdate(transform, deltaTime);
        if (!result.valid()) {
            const sol::error err = result;
            throw std::runtime_error("Script error in '" + scriptComponent.scriptPath + "': " + err.what());
        }
    }
}

ScriptEngine::LoadedScript& ScriptEngine::GetOrLoadScript(const std::string& scriptPath) {
    const auto it = m_LoadedScripts.find(scriptPath);
    if (it != m_LoadedScripts.end()) {
        return it->second;
    }

    const std::filesystem::path resolvedPath = GetExecutableDirectory() / scriptPath;

    // Own environment (a private globals table layered over the shared Lua
    // state's globals) so this script's top-level `onUpdate` can't collide
    // with a different script's — see the header's doc comment.
    sol::environment env(m_Lua, sol::create, m_Lua.globals());

    LoadedScript loaded;
    {
        // sol::script_file's default error policy (script_default_on_error,
        // when SOL_DEFAULT_PASS_ON_ERROR isn't set) is script_throw_on_error
        // — it THROWS a sol::error C++ exception directly from inside this
        // call on a load failure, rather than returning an invalid result.
        // That meant `loadResult.valid()` below was never actually reached
        // for a missing/broken script: the throw happened first, skipped
        // the emplace() at the bottom of this function entirely, and left
        // the failure never cached — every frame re-attempted the load
        // from scratch forever (spamming sol2's own stderr print each
        // time), and reliably crashed the process after enough of that
        // churn. sol::script_pass_on_error makes it return the invalid
        // result instead, so the `else` branch below actually runs and the
        // failure gets cached like any other outcome.
        const sol::protected_function_result loadResult =
            m_Lua.script_file(resolvedPath.string(), env, sol::script_pass_on_error);
        if (loadResult.valid()) {
            loaded.env = env;
            loaded.onUpdate = env["onUpdate"];
        } else {
            // Deliberately NOT thrown: a missing/broken script file must
            // only ever be attempted once. Throwing here meant every entity
            // referencing it re-parsed from scratch every single frame
            // forever — besides being wasteful, this leaked a fresh
            // sol::environment/Lua registry entry per attempt with nothing
            // ever freeing the old ones, and reliably crashed the whole
            // process after a minute or two of 60Hz retries. Caching an
            // entry with an invalid onUpdate (Update()'s existing
            // `!script.onUpdate.valid()` check already skips those) makes a
            // broken script a one-time cost instead.
            const sol::error err = loadResult;
            polyizon::Log::Error("Failed to load script '" + resolvedPath.string() + "': " + err.what());
        }
    }

    const auto [inserted, wasInserted] = m_LoadedScripts.emplace(scriptPath, std::move(loaded));
    return inserted->second;
}
