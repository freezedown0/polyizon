#include "project.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace {

using json = nlohmann::json;

constexpr const char* kEngineVersion = "0.1.0";
constexpr const char* kDefaultSceneRelativePath = "Scenes/Main.json";

// Hand-written to match SceneSerializer's exact JSON schema (see
// scene_serializer.cpp) without needing an actual in-memory Scene/Mesh — a
// new project's default scene is just data on disk, not yet-loaded GPU
// resources, so there's no VulkanContext dependency here. Same plane+cube
// sample used throughout Phases 15-16, including the spin script, so a
// brand-new project demonstrates scripting out of the box.
json BuildDefaultSceneJson() {
    json cubeEntity;
    cubeEntity["transform"] = {
        { "position", json::array({ 0.0, 0.5, 0.0 }) },
        { "rotationEulerDegrees", json::array({ 0.0, 0.0, 0.0 }) },
        { "scale", json::array({ 1.0, 1.0, 1.0 }) },
    };
    cubeEntity["mesh"] = "models/cube.obj";
    cubeEntity["material"] = { { "baseColor", json::array({ 0.8, 0.8, 0.85 }) } };
    cubeEntity["script"] = "scripts/spin.lua";

    json planeEntity;
    planeEntity["transform"] = {
        { "position", json::array({ 0.0, 0.0, 0.0 }) },
        { "rotationEulerDegrees", json::array({ 0.0, 0.0, 0.0 }) },
        { "scale", json::array({ 1.0, 1.0, 1.0 }) },
    };
    planeEntity["mesh"] = "models/plane.obj";
    planeEntity["material"] = { { "baseColor", json::array({ 0.3, 0.6, 0.25 }) } };

    json root;
    root["entities"] = json::array({ cubeEntity, planeEntity });
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
    sceneFile << BuildDefaultSceneJson().dump(2);

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
    return project;
}
