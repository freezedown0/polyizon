#include "polyizon/scene/scene_serializer.hpp"

#include "polyizon/assets/mesh_import.hpp"

#include "polyizon/scene/components.hpp"
#include "polyizon/scene/entity_uuid.hpp"
#include "polyizon/scene/lighting_settings.hpp"
#include "polyizon/vulkan/context.hpp"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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

const char* LightMobilityToString(LightMobility mobility) {
    switch (mobility) {
    case LightMobility::Baked: return "baked";
    case LightMobility::Mixed: return "mixed";
    case LightMobility::Realtime: return "realtime";
    }
    return "realtime";
}

LightMobility JsonToLightMobility(const json& value) {
    const std::string mobility = value.is_string() ? value.get<std::string>() : "realtime";
    if (mobility == "baked") return LightMobility::Baked;
    if (mobility == "mixed") return LightMobility::Mixed;
    return LightMobility::Realtime;
}

} // namespace

nlohmann::json SerializeSceneToJson(const Scene& scene) {
    const entt::registry& registry = scene.GetRegistry();

    json entitiesJson = json::array();

    auto view = registry.view<const TransformComponent>();
    for (const entt::entity entity : view) {
        json entityJson;

        if (const auto* identity = registry.try_get<const IdentityComponent>(entity)) {
            entityJson["id"] = identity->uuid;
        }
        if (const auto* metadata = registry.try_get<const EntityMetadataComponent>(entity)) {
            entityJson["metadata"] = {
                { "enabled", metadata->enabled },
                { "staticForLighting", metadata->staticForLighting },
            };
        }

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
            entityJson["material"] = {
                { "baseColor", Vec3ToJson(material->baseColor) },
                { "metallic", material->metallic },
                { "roughness", material->roughness },
                { "emissiveColor", Vec3ToJson(material->emissiveColor) },
                { "emissiveIntensity", material->emissiveIntensity },
            };
        }
        if (const auto* script = registry.try_get<const ScriptComponent>(entity)) {
            entityJson["script"] = script->scriptPath;
        }
        if (const auto* directionalLight = registry.try_get<const DirectionalLightComponent>(entity)) {
            entityJson["directionalLight"] = {
                { "color", Vec3ToJson(directionalLight->color) },
                { "intensity", directionalLight->intensity },
                { "enabled", directionalLight->enabled },
                { "castsShadows", directionalLight->castsShadows },
                { "mobility", LightMobilityToString(directionalLight->mobility) },
            };
        }
        if (const auto* pointLight = registry.try_get<const PointLightComponent>(entity)) {
            entityJson["pointLight"] = {
                { "color", Vec3ToJson(pointLight->color) },
                { "intensity", pointLight->intensity },
                { "range", pointLight->range },
                { "enabled", pointLight->enabled },
                { "castsShadows", pointLight->castsShadows },
                { "mobility", LightMobilityToString(pointLight->mobility) },
            };
        }
        if (const auto* spotLight = registry.try_get<const SpotLightComponent>(entity)) {
            entityJson["spotLight"] = {
                { "color", Vec3ToJson(spotLight->color) },
                { "intensity", spotLight->intensity },
                { "range", spotLight->range },
                { "innerConeDegrees", spotLight->innerConeDegrees },
                { "outerConeDegrees", spotLight->outerConeDegrees },
                { "enabled", spotLight->enabled },
                { "castsShadows", spotLight->castsShadows },
                { "mobility", LightMobilityToString(spotLight->mobility) },
            };
        }

        entitiesJson.push_back(std::move(entityJson));
    }

    const SceneLightingSettings& lighting = scene.GetLightingSettings();
    json lightingJson;
    lightingJson["sunElevationDegrees"] = lighting.sunElevationDegrees;
    lightingJson["sunAzimuthDegrees"] = lighting.sunAzimuthDegrees;
    lightingJson["directionalLightColor"] = Vec3ToJson(lighting.directionalLightColor);
    lightingJson["directionalLightIntensity"] = lighting.directionalLightIntensity;
    lightingJson["ambientStrength"] = lighting.ambientStrength;
    lightingJson["skyExposure"] = lighting.skyExposure;
    lightingJson["skySunIntensity"] = lighting.skySunIntensity;
    lightingJson["clouds"] = {
        { "enabled", lighting.cloudsEnabled },
        { "layerBottomKm", lighting.cloudLayerBottomKm },
        { "layerTopKm", lighting.cloudLayerTopKm },
        { "coverage", lighting.cloudCoverage },
        { "density", lighting.cloudDensity },
        { "noiseScale", lighting.cloudNoiseScale },
        { "windSpeed", lighting.cloudWindSpeed },
        { "windDirectionDegrees", lighting.cloudWindDirectionDegrees },
        { "powderStrength", lighting.cloudPowderStrength },
        { "ambientStrength", lighting.cloudAmbientStrength },
        { "primarySteps", lighting.cloudPrimarySteps },
        { "shadowSteps", lighting.cloudShadowSteps },
    };

    json root;
    root["schemaVersion"] = 2;
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
    std::unordered_set<std::string> entityIds;

    for (const auto& entityJson : root.at("entities")) {
        const entt::entity entity = scene.CreateEntity();

        auto& identity = scene.GetRegistry().get<IdentityComponent>(entity);
        if (entityJson.contains("id")) {
            const std::string serializedId = entityJson.at("id").get<std::string>();
            if (IsValidEntityUuid(serializedId) && !entityIds.contains(serializedId)) {
                identity.uuid = serializedId;
            }
        }
        while (entityIds.contains(identity.uuid)) {
            identity.uuid = GenerateEntityUuid();
        }
        entityIds.insert(identity.uuid);

        auto& metadata = scene.GetRegistry().get<EntityMetadataComponent>(entity);
        if (entityJson.contains("metadata")) {
            const auto& metadataJson = entityJson.at("metadata");
            metadata.enabled = metadataJson.value("enabled", true);
            metadata.staticForLighting = metadataJson.value("staticForLighting", false);
        }

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
            const auto& materialJson = entityJson.at("material");
            material.baseColor = JsonToVec3(materialJson.at("baseColor"));
            material.metallic = materialJson.value("metallic", 0.0f);
            material.roughness = materialJson.value("roughness", 0.6f);
            if (materialJson.contains("emissiveColor")) {
                material.emissiveColor = JsonToVec3(materialJson.at("emissiveColor"));
            }
            material.emissiveIntensity = materialJson.value("emissiveIntensity", 0.0f);
            scene.GetRegistry().emplace<MaterialComponent>(entity, material);
        }

        if (entityJson.contains("script")) {
            scene.GetRegistry().emplace<ScriptComponent>(entity, entityJson.at("script").get<std::string>());
        }

        if (entityJson.contains("directionalLight")) {
            const auto& lightJson = entityJson.at("directionalLight");
            DirectionalLightComponent light;
            light.color = JsonToVec3(lightJson.at("color"));
            light.intensity = lightJson.value("intensity", 1.0f);
            light.enabled = lightJson.value("enabled", true);
            light.castsShadows = lightJson.value("castsShadows", true);
            if (lightJson.contains("mobility")) {
                light.mobility = JsonToLightMobility(lightJson.at("mobility"));
            }
            scene.GetRegistry().emplace<DirectionalLightComponent>(entity, light);
        }

        if (entityJson.contains("pointLight")) {
            const auto& pointLightJson = entityJson.at("pointLight");
            PointLightComponent pointLight;
            pointLight.color = JsonToVec3(pointLightJson.at("color"));
            pointLight.intensity = pointLightJson.value("intensity", 1.0f);
            pointLight.range = pointLightJson.value("range", 10.0f);
            pointLight.enabled = pointLightJson.value("enabled", true);
            pointLight.castsShadows = pointLightJson.value("castsShadows", false);
            if (pointLightJson.contains("mobility")) {
                pointLight.mobility = JsonToLightMobility(pointLightJson.at("mobility"));
            }
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
            spotLight.enabled = spotLightJson.value("enabled", true);
            spotLight.castsShadows = spotLightJson.value("castsShadows", false);
            if (spotLightJson.contains("mobility")) {
                spotLight.mobility = JsonToLightMobility(spotLightJson.at("mobility"));
            }
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
        // Voxel lighting was retired. Older files may still contain a mode
        // field, but all scenes now load through the realistic environment.
        lighting.sunElevationDegrees = lightingJson.value("sunElevationDegrees", 25.0f);
        lighting.sunAzimuthDegrees = lightingJson.value("sunAzimuthDegrees", 0.0f);
        if (lightingJson.contains("directionalLightColor")) {
            lighting.directionalLightColor = JsonToVec3(lightingJson.at("directionalLightColor"));
        }
        lighting.directionalLightIntensity = lightingJson.value("directionalLightIntensity", 1.0f);
        lighting.ambientStrength = lightingJson.value("ambientStrength", 0.15f);
        lighting.skyExposure = lightingJson.value("skyExposure", 1.15f);
        lighting.skySunIntensity = lightingJson.value("skySunIntensity", 10.0f);
        if (lightingJson.contains("clouds")) {
            const auto& clouds = lightingJson.at("clouds");
            lighting.cloudsEnabled = clouds.value("enabled", true);
            lighting.cloudLayerBottomKm = clouds.value("layerBottomKm", 1.5f);
            lighting.cloudLayerTopKm = clouds.value("layerTopKm", 4.0f);
            lighting.cloudCoverage = clouds.value("coverage", 0.22f);
            lighting.cloudDensity = clouds.value("density", 0.75f);
            lighting.cloudNoiseScale = clouds.value("noiseScale", 0.14f);
            lighting.cloudWindSpeed = clouds.value("windSpeed", 0.0015f);
            lighting.cloudWindDirectionDegrees = clouds.value("windDirectionDegrees", 0.0f);
            lighting.cloudPowderStrength = clouds.value("powderStrength", 0.8f);
            lighting.cloudAmbientStrength = clouds.value("ambientStrength", 0.25f);
            lighting.cloudPrimarySteps = clouds.value("primarySteps", 80);
            lighting.cloudShadowSteps = clouds.value("shadowSteps", 10);
        }
        scene.GetLightingSettings() = lighting;
    }

    // Version 1 scenes stored the primary sun only as environment numbers.
    // Promote that data to a real Directional Light entity on load so the
    // lighting and sky systems are independent without breaking old files.
    const auto directionalLights = scene.GetRegistry().view<const DirectionalLightComponent>();
    if (directionalLights.begin() == directionalLights.end()) {
        const SceneLightingSettings& lighting = scene.GetLightingSettings();
        const entt::entity lightEntity = scene.CreateEntity();
        scene.GetRegistry().emplace<TagComponent>(lightEntity, "Directional Light");
        TransformComponent transform;
        transform.rotationEulerDegrees = glm::vec3(
            -lighting.sunElevationDegrees, 90.0f - lighting.sunAzimuthDegrees, 0.0f);
        scene.GetRegistry().emplace<TransformComponent>(lightEntity, transform);
        DirectionalLightComponent light;
        light.color = lighting.directionalLightColor;
        light.intensity = lighting.directionalLightIntensity;
        scene.GetRegistry().emplace<DirectionalLightComponent>(lightEntity, light);
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
