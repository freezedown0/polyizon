#include "polyizon/scene/components.hpp"
#include "polyizon/scene/entity_uuid.hpp"
#include "polyizon/scene/scene.hpp"
#include "polyizon/scene/scene_serializer.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <unordered_set>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    bool passed = true;
    std::unordered_set<std::string> generatedIds;
    for (int i = 0; i < 256; ++i) {
        const std::string id = polyizon::GenerateEntityUuid();
        passed &= Check(polyizon::IsValidEntityUuid(id), "generated UUID must be valid");
        passed &= Check(generatedIds.insert(id).second, "generated UUID must be unique in the sample");
    }
    passed &= Check(!polyizon::IsValidEntityUuid("not-a-uuid"), "malformed UUID must be rejected");

    polyizon::Scene scene;
    const entt::entity entity = scene.CreateEntity();
    auto& registry = scene.GetRegistry();
    registry.emplace<polyizon::TagComponent>(entity, "Sun");
    registry.emplace<polyizon::TransformComponent>(entity);
    registry.emplace<polyizon::DirectionalLightComponent>(entity);

    const auto& identity = registry.get<const polyizon::IdentityComponent>(entity);
    auto& metadata = registry.get<polyizon::EntityMetadataComponent>(entity);
    metadata.enabled = false;
    metadata.staticForLighting = true;

    const nlohmann::json root = polyizon::SerializeSceneToJson(scene);
    passed &= Check(root.at("schemaVersion") == 2, "scene schema must remain version 2");
    passed &= Check(root.at("entities").size() == 1, "scene must serialize one entity");

    const auto& serialized = root.at("entities").at(0);
    passed &= Check(serialized.at("id") == identity.uuid, "stable entity ID must be serialized");
    passed &= Check(!serialized.at("metadata").at("enabled").get<bool>(), "inactive state must be serialized");
    passed &= Check(serialized.at("metadata").at("staticForLighting").get<bool>(),
        "baked-lighting contribution must be serialized");
    passed &= Check(serialized.contains("directionalLight"), "Directional Light component must be serialized");

    if (passed) {
        std::cout << "Polyizon scene authoring tests passed\n";
        return 0;
    }
    return 1;
}
