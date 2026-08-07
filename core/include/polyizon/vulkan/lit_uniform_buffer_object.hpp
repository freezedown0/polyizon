#pragma once

#include <glm/glm.hpp>

namespace polyizon {

struct LitUniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 sunDirectionAndAmbient;
    glm::vec4 directionalLightColorAndIntensity;
    // x = directional shadow enabled. Local lights are stored at binding 2.
    glm::ivec4 renderFlags;
};

} // namespace polyizon
