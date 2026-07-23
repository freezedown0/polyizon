#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Per-frame-in-flight uniform data for the Voxel lighting mode's flat/
// gradient sky (classic_sky.frag) — a much smaller sibling of
// SkyUniformBufferObject (see sky_uniform_buffer_object.hpp): no atmosphere/
// cloud raymarch params at all, since this shader has neither. Fragment-only,
// same reasoning as SkyUniformBufferObject.
//
// std140 layout: every field is vec4/mat4-sized, no explicit padding needed.
struct ClassicSkyUniformBufferObject {
    glm::mat4 invView; // only mat3(invView) (rotation) used — same
                        // camera-translation-decoupled convention as
                        // SkyUniformBufferObject.
    glm::mat4 invProj;

    glm::vec4 sunDirection; // xyz normalized, world space, pointing TOWARD the
                            // sun (same convention as SkyUniformBufferObject).
                            // w = sun angular radius (radians).
    glm::vec4 dayHorizonColor;
    glm::vec4 dayZenithColor;
    glm::vec4 nightHorizonColor;
    glm::vec4 nightZenithColor;
};

} // namespace polyizon
