#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Per-frame-in-flight uniform data shared across every instance drawn this
// frame: camera view/projection only. The per-object model matrix lives in
// ModelPushConstant (below) instead — a single shared UBO slot can't
// represent more than one object's model matrix at a time, which matters
// once multiple simultaneously-visible instances need independent
// transforms.
//
// std140 layout: two consecutive mat4 fields each already have a 16-byte
// (vec4) base alignment and a size that's a multiple of 16 bytes, so no
// explicit padding/alignas is needed here.
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
};

// Per-object model matrix, pushed via vkCmdPushConstants immediately before
// each instance's draw call. 64 bytes — comfortably inside Vulkan's
// minimum-guaranteed 128-byte push constant budget.
struct ModelPushConstant {
    glm::mat4 model;
};

} // namespace polyizon
