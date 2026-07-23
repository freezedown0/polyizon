#pragma once

#include <filesystem>

class Project;

// Exports `project` into a fully self-contained, standalone folder at
// outputDir: a copy of polyizon.exe (renamed to "<ProjectName>.exe") plus
// the engine's shared runtime (shaders/textures/models/scripts and every
// non-Qt DLL — the game links none of Qt) and the project's own
// Scenes/Scripts/Assets/project.json — with any scene's absolute script/
// mesh paths (see InspectorPanel's ToStoredAssetPath, which stores a
// project's own Scripts/Assets references as absolute since they live
// outside the engine's executable directory) rewritten to be relative to
// the project root, so the exported folder runs standalone on a machine
// that never had this project's original location at all — see
// polyizon::Application's project mode, which is what actually loads and
// renders it. Throws on failure (missing polyizon.exe, filesystem errors).
void BuildGame(const Project& project, const std::filesystem::path& outputDir);
