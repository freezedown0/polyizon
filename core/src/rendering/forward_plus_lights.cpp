#include "polyizon/rendering/forward_plus_lights.hpp"

#include "polyizon/scene/components.hpp"

#include <glm/trigonometric.hpp>

#include <cmath>

namespace polyizon {
namespace {
bool IsEntityEnabled(const entt::registry& registry, entt::entity entity) {
    const auto* metadata = registry.try_get<const EntityMetadataComponent>(entity);
    return metadata == nullptr || metadata->enabled;
}
} // namespace

ForwardPlusLightBuffer BuildForwardPlusLightBuffer(const Scene& scene) {
    ForwardPlusLightBuffer output{};
    const entt::registry& registry = scene.GetRegistry();
    std::uint32_t total = 0;
    std::uint32_t dropped = 0;

    const auto pointLights = registry.view<const TransformComponent, const PointLightComponent>();
    for (const entt::entity entity : pointLights) {
        const auto& light = pointLights.get<const PointLightComponent>(entity);
        if (!IsEntityEnabled(registry, entity) || !light.enabled || light.mobility == LightMobility::Baked) continue;
        if (total >= kForwardPlusMaxLocalLights) { ++dropped; continue; }
        const auto& transform = pointLights.get<const TransformComponent>(entity);
        GpuLocalLight& gpu = output.lights[total++];
        gpu.positionAndRange = glm::vec4(transform.position, light.range);
        gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);
        gpu.outerCosAndType.y = 0.0f;
        ++output.counts.y;
    }

    const auto spotLights = registry.view<const TransformComponent, const SpotLightComponent>();
    for (const entt::entity entity : spotLights) {
        const auto& light = spotLights.get<const SpotLightComponent>(entity);
        if (!IsEntityEnabled(registry, entity) || !light.enabled || light.mobility == LightMobility::Baked) continue;
        if (total >= kForwardPlusMaxLocalLights) { ++dropped; continue; }
        const auto& transform = spotLights.get<const TransformComponent>(entity);
        GpuLocalLight& gpu = output.lights[total++];
        gpu.positionAndRange = glm::vec4(transform.position, light.range);
        gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);
        gpu.directionAndInnerCos = glm::vec4(
            transform.GetForward(), std::cos(glm::radians(light.innerConeDegrees)));
        gpu.outerCosAndType = glm::vec4(
            std::cos(glm::radians(light.outerConeDegrees)), 1.0f, 0.0f, 0.0f);
        ++output.counts.z;
    }
    output.counts.x = static_cast<int>(total);
    output.counts.w = static_cast<int>(dropped);
    return output;
}

} // namespace polyizon
