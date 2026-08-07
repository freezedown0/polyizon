#include "project.hpp"

#include "polyizon/scene/entity_uuid.hpp"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <stdexcept>

namespace {

using json = nlohmann::json;

constexpr const char* kEngineVersion = "0.1.0";
constexpr const char* kDefaultSceneRelativePath = "Scenes/Main.scene";

// Resolved relative to the running executable's own directory, matching
// every other asset-loading class's identical private helper — duplicated
// here rather than shared for the same reason they duplicate it from each
// other.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

// Hand-written to match SceneSerializer's exact JSON schema (see
// scene_serializer.cpp) without needing an actual in-memory Scene/Mesh — a
// new project's default scene is just data on disk, not yet-loaded GPU
// resources, so there's no VulkanContext dependency here. Same plane+cube
// sample used throughout Phases 15-16, including the spin script, so a
// brand-new project demonstrates scripting out of the box.
//
// spinScriptPath is an ABSOLUTE path to the project's own copy of
// spin.lua (see CreateNew, which copies it there before calling this) —
// not the engine-relative "scripts/spin.lua" this used to be. The whole
// point of a project having its own Scripts/ folder (see the Phase 16
// plan) is that its scripts live IN the project, not as a reference back
// into the shared engine installation; storing an absolute path here means
// ScriptEngine/SceneSerializer's executable-relative path resolution
// (GetExecutableDirectory() / path) resolves it correctly regardless,
// since std::filesystem::path's operator/ returns an absolute right-hand
// operand unchanged.
json BuildDefaultSceneJson(const std::filesystem::path& spinScriptPath) {
    json cubeEntity;
    cubeEntity["id"] = polyizon::GenerateEntityUuid();
    cubeEntity["metadata"] = { { "enabled", true }, { "staticForLighting", false } };
    cubeEntity["tag"] = "Cube";
    cubeEntity["transform"] = {
        { "position", json::array({ 0.0, 0.5, 0.0 }) },
        { "rotationEulerDegrees", json::array({ 0.0, 0.0, 0.0 }) },
        { "scale", json::array({ 1.0, 1.0, 1.0 }) },
    };
    cubeEntity["mesh"] = "models/cube.obj";
    cubeEntity["material"] = {
        { "baseColor", json::array({ 0.8, 0.8, 0.85 }) },
        { "metallic", 0.0 }, { "roughness", 0.35 },
        { "emissiveColor", json::array({ 0.0, 0.0, 0.0 }) }, { "emissiveIntensity", 0.0 }
    };
    cubeEntity["script"] = spinScriptPath.generic_string();

    json planeEntity;
    planeEntity["id"] = polyizon::GenerateEntityUuid();
    planeEntity["metadata"] = { { "enabled", true }, { "staticForLighting", true } };
    planeEntity["tag"] = "Plane";
    planeEntity["transform"] = {
        { "position", json::array({ 0.0, 0.0, 0.0 }) },
        { "rotationEulerDegrees", json::array({ 0.0, 0.0, 0.0 }) },
        { "scale", json::array({ 1.0, 1.0, 1.0 }) },
    };
    planeEntity["mesh"] = "models/plane.obj";
    planeEntity["material"] = {
        { "baseColor", json::array({ 0.3, 0.6, 0.25 }) },
        { "metallic", 0.0 }, { "roughness", 0.8 },
        { "emissiveColor", json::array({ 0.0, 0.0, 0.0 }) }, { "emissiveIntensity", 0.0 }
    };

    json directionalLightEntity;
    directionalLightEntity["id"] = polyizon::GenerateEntityUuid();
    directionalLightEntity["metadata"] = { { "enabled", true }, { "staticForLighting", false } };
    directionalLightEntity["tag"] = "Directional Light";
    directionalLightEntity["transform"] = {
        { "position", json::array({ 0.0, 4.0, 0.0 }) },
        { "rotationEulerDegrees", json::array({ -25.0, 90.0, 0.0 }) },
        { "scale", json::array({ 1.0, 1.0, 1.0 }) },
    };
    directionalLightEntity["directionalLight"] = {
        { "color", json::array({ 1.0, 0.95, 0.85 }) },
        { "intensity", 1.0 }, { "enabled", true },
        { "castsShadows", true }, { "mobility", "realtime" }
    };

    // Explicit rather than relying on SceneSerializer's missing-key defaults:
    // a new project's environment should be self-describing.
    json lighting;
    lighting["sunElevationDegrees"] = 25.0;
    lighting["sunAzimuthDegrees"] = 0.0;
    lighting["directionalLightColor"] = json::array({ 1.0, 0.95, 0.85 });
    lighting["directionalLightIntensity"] = 1.0;
    lighting["ambientStrength"] = 0.15;
    lighting["skyExposure"] = 1.15;
    lighting["skySunIntensity"] = 10.0;
    lighting["clouds"] = {
        { "enabled", true }, { "layerBottomKm", 1.5 }, { "layerTopKm", 4.0 },
        { "coverage", 0.22 }, { "density", 0.75 }, { "noiseScale", 0.14 },
        { "windSpeed", 0.0015 }, { "windDirectionDegrees", 0.0 },
        { "powderStrength", 0.8 }, { "ambientStrength", 0.25 },
        { "primarySteps", 80 }, { "shadowSteps", 10 }
    };

    json root;
    root["schemaVersion"] = 2;
    root["entities"] = json::array({ directionalLightEntity, cubeEntity, planeEntity });
    root["lighting"] = lighting;
    return root;
}

} // namespace

Project Project::CreateNew(const std::string& name, const std::filesystem::path& parentDir) {
    Project project;
    project.m_Name = name;
    project.m_EngineVersion = kEngineVersion;
    project.m_DefaultScene = kDefaultSceneRelativePath;
    project.m_RootDir = parentDir / name;

    std::filesystem::create_directories(project.GetScenesDir());
    std::filesystem::create_directories(project.GetScriptsDir());
    std::filesystem::create_directories(project.GetAssetsDir());

    // Copies the engine's built-in sample script into the new project's own
    // Scripts/ folder rather than leaving the default scene pointing back
    // at the engine's shared core/scripts/spin.lua — a project's Scripts/
    // folder must actually contain its scripts, not just exist as an empty
    // decoration next to a scene that secretly depends on the engine
    // install still being there.
    const std::filesystem::path engineSpinScript = GetExecutableDirectory() / "scripts" / "spin.lua";
    const std::filesystem::path projectSpinScript = project.GetScriptsDir() / "spin.lua";
    std::error_code copyError;
    std::filesystem::copy_file(
        engineSpinScript, projectSpinScript, std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("Failed to copy sample script into new project: " + copyError.message());
    }

    json manifest;
    manifest["name"] = project.m_Name;
    manifest["engineVersion"] = project.m_EngineVersion;
    manifest["defaultScene"] = project.m_DefaultScene;

    std::ofstream manifestFile(project.GetManifestPath());
    if (!manifestFile) {
        throw std::runtime_error("Failed to write project manifest: " + project.GetManifestPath().string());
    }
    manifestFile << manifest.dump(2);
    manifestFile.close();

    std::ofstream sceneFile(project.GetDefaultScenePath());
    if (!sceneFile) {
        throw std::runtime_error("Failed to write default scene: " + project.GetDefaultScenePath().string());
    }
    sceneFile << BuildDefaultSceneJson(projectSpinScript).dump(2);

    return project;
}

Project Project::Open(const std::filesystem::path& manifestPath) {
    std::ifstream file(manifestPath);
    if (!file) {
        throw std::runtime_error("Failed to open project manifest: " + manifestPath.string());
    }

    json manifest;
    file >> manifest;

    Project project;
    project.m_Name = manifest.at("name").get<std::string>();
    project.m_EngineVersion = manifest.at("engineVersion").get<std::string>();
    project.m_DefaultScene = manifest.at("defaultScene").get<std::string>();
    project.m_RootDir = manifestPath.parent_path();

    // Older Polyizon projects stored scenes with a .json extension. Scene
    // contents remain JSON internally, but .scene is now the public project
    // format so configuration files and editable scenes are unambiguous.
    std::filesystem::path currentScenePath = project.GetDefaultScenePath();
    if (currentScenePath.extension() == ".json" && std::filesystem::exists(currentScenePath)) {
        std::filesystem::path migratedScenePath = currentScenePath;
        migratedScenePath.replace_extension(".scene");

        std::error_code migrationError;
        if (!std::filesystem::exists(migratedScenePath)) {
            std::filesystem::rename(currentScenePath, migratedScenePath, migrationError);
            if (migrationError) {
                throw std::runtime_error("Failed to upgrade scene file to .scene: " + migrationError.message());
            }
        }

        project.m_DefaultScene = std::filesystem::relative(migratedScenePath, project.m_RootDir).generic_string();
        manifest["defaultScene"] = project.m_DefaultScene;
        std::ofstream updatedManifest(manifestPath);
        if (!updatedManifest) {
            throw std::runtime_error("Failed to update project manifest after scene upgrade");
        }
        updatedManifest << manifest.dump(2);
    }
    return project;
}
