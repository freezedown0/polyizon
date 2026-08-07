#pragma once

#include <filesystem>
#include <string>

// A user-created project: its own Scenes/Scripts/Assets folders plus a
// project.json manifest. Deliberately data-only — there is no separate
// per-project build step (see the Phase 16 plan's "shared engine source"
// decision: scripts are Lua, interpreted at runtime by this same compiled
// editor, so a project never needs its own CMake/build wiring against the
// engine). No "recent projects" list or project browser this phase.
class Project {
public:
    // Scaffolds <parentDir>/<name>/{Scenes,Scripts,Assets}/, writes
    // project.json, and writes a default Scenes/Main.scene (the same
    // plane+cube sample used throughout Phases 15-16, including the spin
    // script — see scene_serializer.hpp for the JSON schema this matches)
    // so a brand-new project has something to open immediately. The default
    // scene's mesh/script paths point at the engine's own core/models/ and
    // core/scripts/ (resolved executable-relative, same convention
    // everywhere else) — nothing is copied into the new project's Assets/;
    // per-project asset import is out of scope this phase.
    static Project CreateNew(const std::string& name, const std::filesystem::path& parentDir);

    // Reads an existing project.json.
    static Project Open(const std::filesystem::path& manifestPath);

    const std::string& GetName() const noexcept { return m_Name; }
    const std::filesystem::path& GetRootDir() const noexcept { return m_RootDir; }
    std::filesystem::path GetScenesDir() const { return m_RootDir / "Scenes"; }
    std::filesystem::path GetScriptsDir() const { return m_RootDir / "Scripts"; }
    std::filesystem::path GetAssetsDir() const { return m_RootDir / "Assets"; }
    std::filesystem::path GetManifestPath() const { return m_RootDir / "project.json"; }
    std::filesystem::path GetDefaultScenePath() const { return m_RootDir / m_DefaultScene; }

private:
    Project() = default;

    std::string m_Name;
    std::string m_EngineVersion;
    std::string m_DefaultScene; // relative to m_RootDir, e.g. "Scenes/Main.scene"
    std::filesystem::path m_RootDir;
};
