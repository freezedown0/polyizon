#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Per-frame-in-flight uniform data shared across every instance drawn this
// frame: camera view/projection only. The per-object model matrix arrives
// via a per-instance vertex attribute instead (pipeline binding 1, see
// GraphicsPipeline) — a single shared UBO slot can't represent more than one
// object's model matrix at a time, which matters once multiple
// simultaneously-visible instances need independent transforms.
//
// std140 layout: two consecutive mat4 fields each already have a 16-byte
// (vec4) base alignment and a size that's a multiple of 16 bytes, so no
// explicit padding/alignas is needed here.
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    // Distance/volumetric fog for the quads (see triangle.vert/.frag):
    // rgb = fog color (kept close to the sky's own haze tone), a = density
    // for the exponential falloff exp(-(dist*density)^2). Read fragment-side
    // only, but lives in this same per-frame UBO rather than a second one —
    // the binding's stageFlags now cover both VERTEX and FRAGMENT (see
    // GraphicsPipeline::CreateDescriptorSetLayout()).
    glm::vec4 fogColorAndDensity;
};

} // namespace polyizon
