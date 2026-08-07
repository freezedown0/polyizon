#pragma once

#include "polyizon/scene/scene.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace polyizon {

inline constexpr std::uint32_t kForwardPlusMaxLocalLights = 1024;

struct GpuLocalLight {
    glm::vec4 positionAndRange{};
    glm::vec4 colorAndIntensity{};
    glm::vec4 directionAndInnerCos{};
    glm::vec4 outerCosAndType{};
};

struct ForwardPlusLightBuffer {
    glm::ivec4 counts{}; // x total, y point, z spot, w dropped
    GpuLocalLight lights[kForwardPlusMaxLocalLights]{};
};

ForwardPlusLightBuffer BuildForwardPlusLightBuffer(const Scene& scene);

} // namespace polyizon
