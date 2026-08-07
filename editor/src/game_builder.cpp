#include "game_builder.hpp"

#include "project.hpp"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <stdexcept>

namespace {

using json = nlohmann::json;

// Resolved relative to the running executable's own directory, matching
// every other asset-loading class's identical private helper — duplicated
// here rather than shared for the same reason they duplicate it from each
// other. For the editor, this is also where polyizon.exe and the engine's
// shared shaders/textures/models/scripts live (same CMake output directory).
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

void CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!std::filesystem::exists(source)) {
        return; // e.g. an empty Assets/ folder some projects never populate
    }
    std::error_code error;
    std::filesystem::copy(source, destination,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        throw std::runtime_error("Failed to copy " + source.string() + ": " + error.message());
    }
}

// Rewrites every "script"/"mesh" path in `scenePath` that's an absolute path
// under `projectRootDir` into one relative to it — see the header comment
// for why such absolute paths exist. The whole project tree is copied
// preserving this same relative structure (see BuildGame), so the rewritten
// path resolves correctly against the exported game's own executable
// directory.
void RewriteAssetPathsInScene(const std::filesystem::path& scenePath, const std::filesystem::path& projectRootDir) {
    std::ifstream inFile(scenePath);
    if (!inFile) {
        throw std::runtime_error("Failed to open scene file: " + scenePath.string());
    }
    json root;
    inFile >> root;
    inFile.close();

    bool changed = false;
    if (root.contains("entities")) {
        for (auto& entityJson : root["entities"]) {
            for (const char* key : { "script", "mesh" }) {
                if (!entityJson.contains(key)) {
                    continue;
                }
                const std::filesystem::path asPath(entityJson[key].get<std::string>());
                if (!asPath.is_absolute()) {
                    continue; // already engine-relative (e.g. "models/cube.obj") - resolves fine as-is
                }
                std::error_code error;
                const std::filesystem::path relative = std::filesystem::relative(asPath, projectRootDir, error);
                if (error || relative.empty() || relative.generic_string().rfind("..", 0) == 0) {
                    continue; // outside the project root entirely - leave as-is, best effort
                }
                entityJson[key] = relative.generic_string();
                changed = true;
            }
        }
    }

    if (!changed) {
        return;
    }

    std::ofstream outFile(scenePath);
    if (!outFile) {
        throw std::runtime_error("Failed to rewrite scene file: " + scenePath.string());
    }
    outFile << root.dump(2);
}

} // namespace

void BuildGame(const Project& project, const std::filesystem::path& outputDir) {
    const std::filesystem::path engineDir = GetExecutableDirectory();

    std::filesystem::create_directories(outputDir);

    // 1. The game executable itself, renamed to the project's own name.
    const std::filesystem::path sourceExe = engineDir / "polyizon.exe";
    if (!std::filesystem::exists(sourceExe)) {
        throw std::runtime_error("polyizon.exe not found next to the editor - build the 'polyizon' target first");
    }
    std::error_code copyError;
    std::filesystem::copy_file(sourceExe, outputDir / (project.GetName() + ".exe"),
        std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("Failed to copy polyizon.exe: " + copyError.message());
    }

    // 2. Every runtime DLL the game needs (GLFW/Vulkan loader/Assimp/Lua and
    // their own transitive dependencies) - everything except Qt's own DLLs,
    // which only the editor (never the game, see game/CMakeLists.txt) links.
    for (const auto& entry : std::filesystem::directory_iterator(engineDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dll") {
            continue;
        }
        const std::string fileName = entry.path().filename().string();
        if (fileName.rfind("Qt6", 0) == 0) {
            continue; // Qt-only, never needed by the game
        }
        std::filesystem::copy_file(entry.path(), outputDir / entry.path().filename(),
            std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError) {
            throw std::runtime_error("Failed to copy " + fileName + ": " + copyError.message());
        }
    }

    // 3. Engine-shared runtime assets (see core/CMakeLists.txt's shader/
    // texture/model/script asset-copying steps) - resolved relative to the
    // game exe's own directory at runtime, same as the editor.
    CopyDirectoryRecursive(engineDir / "shaders", outputDir / "shaders");
    CopyDirectoryRecursive(engineDir / "textures", outputDir / "textures");
    CopyDirectoryRecursive(engineDir / "models", outputDir / "models");
    CopyDirectoryRecursive(engineDir / "scripts", outputDir / "scripts");

    // 4. The project's own data.
    std::filesystem::copy_file(project.GetManifestPath(), outputDir / "project.json",
        std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("Failed to copy project.json: " + copyError.message());
    }
    CopyDirectoryRecursive(project.GetScenesDir(), outputDir / "Scenes");
    CopyDirectoryRecursive(project.GetScriptsDir(), outputDir / "Scripts");
    CopyDirectoryRecursive(project.GetAssetsDir(), outputDir / "Assets");

    // 5. Rewrite absolute script/mesh paths in every copied scene file so
    // the exported folder doesn't secretly depend on this machine's
    // original project location (see RewriteAssetPathsInScene).
    const std::filesystem::path outputScenesDir = outputDir / "Scenes";
    if (std::filesystem::exists(outputScenesDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(outputScenesDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".scene") {
                RewriteAssetPathsInScene(entry.path(), project.GetRootDir());
            }
        }
    }
}
