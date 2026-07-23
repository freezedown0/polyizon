#include "polyizon/scene/scene_serializer.hpp"

#include "polyizon/assets/mesh_import.hpp"

#include "polyizon/scene/components.hpp"
#include "polyizon/scene/lighting_settings.hpp"
#include "polyizon/vulkan/context.hpp"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace polyizon {

namespace {

using json = nlohmann::json;

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

json Vec3ToJson(const glm::vec3& v) {
    return json::array({ v.x, v.y, v.z });
}

glm::vec3 JsonToVec3(const json& j) {
    return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
}

const char* LightingModeToString(LightingMode mode) {
    switch (mode) {
        case LightingMode::Voxel: return "Voxel";
        case LightingMode::Realistic:
        default: return "Realistic";
    }
}

LightingMode LightingModeFromString(const std::string& s) {
    return s == "Voxel" ? LightingMode::Voxel : LightingMode::Realistic;
}

} // namespace

nlohmann::json SerializeSceneToJson(const Scene& scene) {
    const entt::registry& registry = scene.GetRegistry();

    json entitiesJson = json::array();

    auto view = registry.view<const TransformComponent>();
    for (const entt::entity entity : view) {
        json entityJson;

        if (const auto* tag = registry.try_get<const TagComponent>(entity)) {
            entityJson["tag"] = tag->name;
        }

        const auto& transform = view.get<const TransformComponent>(entity);
        entityJson["transform"] = {
            { "position", Vec3ToJson(transform.position) },
            { "rotationEulerDegrees", Vec3ToJson(transform.rotationEulerDegrees) },
            { "scale", Vec3ToJson(transform.scale) },
        };

        if (const auto* mesh = registry.try_get<const MeshComponent>(entity)) {
            entityJson["mesh"] = mesh->sourcePath;
        }
        if (const auto* material = registry.try_get<const MaterialComponent>(entity)) {
            entityJson["material"] = { { "baseColor", Vec3ToJson(material->baseColor) } };
        }
        if (const auto* script = registry.try_get<const ScriptComponent>(entity)) {
            entityJson["script"] = script->scriptPath;
        }
        if (const auto* pointLight = registry.try_get<const PointLightComponent>(entity)) {
            entityJson["pointLight"] = {
                { "color", Vec3ToJson(pointLight->color) },
                { "intensity", pointLight->intensity },
                { "range", pointLight->range },
            };
        }
        if (const auto* spotLight = registry.try_get<const SpotLightComponent>(entity)) {
            entityJson["spotLight"] = {
                { "color", Vec3ToJson(spotLight->color) },
                { "intensity", spotLight->intensity },
                { "range", spotLight->range },
                { "innerConeDegrees", spotLight->innerConeDegrees },
                { "outerConeDegrees", spotLight->outerConeDegrees },
            };
        }

        entitiesJson.push_back(std::move(entityJson));
    }

    const SceneLightingSettings& lighting = scene.GetLightingSettings();
    json lightingJson;
    lightingJson["mode"] = LightingModeToString(lighting.mode);
    lightingJson["sunElevationDegrees"] = lighting.sunElevationDegrees;
    lightingJson["sunAzimuthDegrees"] = lighting.sunAzimuthDegrees;
    lightingJson["ambientStrength"] = lighting.ambientStrength;

    json root;
    root["entities"] = std::move(entitiesJson);
    root["lighting"] = std::move(lightingJson);
    return root;
}

Scene DeserializeSceneFromJson(VulkanContext& context, const nlohmann::json& root) {
    Scene scene;
    // Multiple entities referencing the same mesh file (e.g. several copies
    // of the same prop) shouldn't each re-run Assimp import - keyed by the
    // same sourcePath string MeshComponent/the JSON both use.
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshCache;

    for (const auto& entityJson : root.at("entities")) {
        const entt::entity entity = scene.CreateEntity();

        // Older (Phase 15/16) scene files predate TagComponent — default to
        // "Entity" rather than requiring every existing scene file to be
        // migrated.
        const std::string tagName = entityJson.contains("tag")
            ? entityJson.at("tag").get<std::string>()
            : "Entity";
        scene.GetRegistry().emplace<TagComponent>(entity, tagName);

        TransformComponent transform;
        const auto& transformJson = entityJson.at("transform");
        transform.position = JsonToVec3(transformJson.at("position"));
        transform.rotationEulerDegrees = JsonToVec3(transformJson.at("rotationEulerDegrees"));
        transform.scale = JsonToVec3(transformJson.at("scale"));
        scene.GetRegistry().emplace<TransformComponent>(entity, transform);

        if (entityJson.contains("mesh")) {
            const std::string meshPath = entityJson.at("mesh").get<std::string>();

            std::shared_ptr<Mesh> mesh;
            const auto it = meshCache.find(meshPath);
            if (it != meshCache.end()) {
                mesh = it->second;
            } else {
                mesh = LoadMesh(context, GetExecutableDirectory() / meshPath);
                meshCache.emplace(meshPath, mesh);
            }

            scene.GetRegistry().emplace<MeshComponent>(entity, mesh, meshPath);
        }

        if (entityJson.contains("material")) {
            MaterialComponent material;
            material.baseColor = JsonToVec3(entityJson.at("material").at("baseColor"));
            scene.GetRegistry().emplace<MaterialComponent>(entity, material);
        }

        if (entityJson.contains("script")) {
            scene.GetRegistry().emplace<ScriptComponent>(entity, entityJson.at("script").get<std::string>());
        }

        if (entityJson.contains("pointLight")) {
            const auto& pointLightJson = entityJson.at("pointLight");
            PointLightComponent pointLight;
            pointLight.color = JsonToVec3(pointLightJson.at("color"));
            pointLight.intensity = pointLightJson.value("intensity", 1.0f);
            pointLight.range = pointLightJson.value("range", 10.0f);
            scene.GetRegistry().emplace<PointLightComponent>(entity, pointLight);
        }

        if (entityJson.contains("spotLight")) {
            const auto& spotLightJson = entityJson.at("spotLight");
            SpotLightComponent spotLight;
            spotLight.color = JsonToVec3(spotLightJson.at("color"));
            spotLight.intensity = spotLightJson.value("intensity", 1.0f);
            spotLight.range = spotLightJson.value("range", 10.0f);
            spotLight.innerConeDegrees = spotLightJson.value("innerConeDegrees", 20.0f);
            spotLight.outerConeDegrees = spotLightJson.value("outerConeDegrees", 30.0f);
            scene.GetRegistry().emplace<SpotLightComponent>(entity, spotLight);
        }
    }

    // Pre-Phase-18 scene files have no "lighting" key at all — the
    // default-constructed SceneLightingSettings (Realistic, same sun/ambient
    // defaults EditorViewportRenderer used to hardcode) matches their prior
    // behavior exactly, so this is skippable rather than required.
    if (root.contains("lighting")) {
        const auto& lightingJson = root.at("lighting");
        SceneLightingSettings lighting;
        lighting.mode = LightingModeFromString(lightingJson.value("mode", std::string("Realistic")));
        lighting.sunElevationDegrees = lightingJson.value("sunElevationDegrees", 25.0f);
        lighting.sunAzimuthDegrees = lightingJson.value("sunAzimuthDegrees", 0.0f);
        lighting.ambientStrength = lightingJson.value("ambientStrength", 0.15f);
        scene.GetLightingSettings() = lighting;
    }

    return scene;
}

void SaveScene(const Scene& scene, const std::filesystem::path& path) {
    const json root = SerializeSceneToJson(scene);

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open scene file for writing: " + path.string());
    }
    file << root.dump(2);
}

Scene LoadScene(VulkanContext& context, const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open scene file: " + path.string());
    }

    json root;
    file >> root;
    return DeserializeSceneFromJson(context, root);
}

} // namespace polyizon
