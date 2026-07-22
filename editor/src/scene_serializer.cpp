#include "scene_serializer.hpp"

#include "mesh_import.hpp"

#include "polyizon/scene/components.hpp"
#include "polyizon/vulkan/context.hpp"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <stdexcept>
#include <unordered_map>

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

} // namespace

void SaveScene(const polyizon::Scene& scene, const std::filesystem::path& path) {
    const entt::registry& registry = scene.GetRegistry();

    json entitiesJson = json::array();

    auto view = registry.view<const polyizon::TransformComponent>();
    for (const entt::entity entity : view) {
        json entityJson;

        const auto& transform = view.get<const polyizon::TransformComponent>(entity);
        entityJson["transform"] = {
            { "position", Vec3ToJson(transform.position) },
            { "rotationEulerDegrees", Vec3ToJson(transform.rotationEulerDegrees) },
            { "scale", Vec3ToJson(transform.scale) },
        };

        if (const auto* mesh = registry.try_get<const polyizon::MeshComponent>(entity)) {
            entityJson["mesh"] = mesh->sourcePath;
        }
        if (const auto* material = registry.try_get<const polyizon::MaterialComponent>(entity)) {
            entityJson["material"] = { { "baseColor", Vec3ToJson(material->baseColor) } };
        }
        if (const auto* script = registry.try_get<const polyizon::ScriptComponent>(entity)) {
            entityJson["script"] = script->scriptPath;
        }

        entitiesJson.push_back(std::move(entityJson));
    }

    json root;
    root["entities"] = std::move(entitiesJson);

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open scene file for writing: " + path.string());
    }
    file << root.dump(2);
}

polyizon::Scene LoadScene(polyizon::VulkanContext& context, const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open scene file: " + path.string());
    }

    json root;
    file >> root;

    polyizon::Scene scene;
    // Multiple entities referencing the same mesh file (e.g. several copies
    // of the same prop) shouldn't each re-run Assimp import - keyed by the
    // same sourcePath string MeshComponent/the JSON both use.
    std::unordered_map<std::string, std::shared_ptr<polyizon::Mesh>> meshCache;

    for (const auto& entityJson : root.at("entities")) {
        const entt::entity entity = scene.CreateEntity();

        polyizon::TransformComponent transform;
        const auto& transformJson = entityJson.at("transform");
        transform.position = JsonToVec3(transformJson.at("position"));
        transform.rotationEulerDegrees = JsonToVec3(transformJson.at("rotationEulerDegrees"));
        transform.scale = JsonToVec3(transformJson.at("scale"));
        scene.GetRegistry().emplace<polyizon::TransformComponent>(entity, transform);

        if (entityJson.contains("mesh")) {
            const std::string meshPath = entityJson.at("mesh").get<std::string>();

            std::shared_ptr<polyizon::Mesh> mesh;
            const auto it = meshCache.find(meshPath);
            if (it != meshCache.end()) {
                mesh = it->second;
            } else {
                mesh = LoadMesh(context, GetExecutableDirectory() / meshPath);
                meshCache.emplace(meshPath, mesh);
            }

            scene.GetRegistry().emplace<polyizon::MeshComponent>(entity, mesh, meshPath);
        }

        if (entityJson.contains("material")) {
            polyizon::MaterialComponent material;
            material.baseColor = JsonToVec3(entityJson.at("material").at("baseColor"));
            scene.GetRegistry().emplace<polyizon::MaterialComponent>(entity, material);
        }

        if (entityJson.contains("script")) {
            scene.GetRegistry().emplace<polyizon::ScriptComponent>(entity, entityJson.at("script").get<std::string>());
        }
    }

    return scene;
}
