#pragma once

#include <filesystem>
#include <string>

namespace polyizon {

// Minimal read-only view of a project.json manifest — just enough for a
// compiled game (see Application's project mode) to find its default scene.
// Deliberately NOT the editor's own Project class (editor/src/project.hpp):
// that one also scaffolds brand-new projects (CreateNew), which is an
// editor-only concern a shipped game never needs.
struct ProjectManifest {
    std::string name;
    std::string defaultScene; // relative to projectRootDir
};

// Reads <projectRootDir>/project.json. Throws on a missing file or missing
// required fields.
ProjectManifest ReadProjectManifest(const std::filesystem::path& projectRootDir);

} // namespace polyizon
