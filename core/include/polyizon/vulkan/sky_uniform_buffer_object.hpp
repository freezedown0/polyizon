#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Per-frame-in-flight uniform data for the sky/cloud fragment shader
// (sky.frag). Unlike UniformBufferObject (used by the vertex shader for the
// quads), everything here is consumed fragment-side, so the whole struct is
// bound VK_SHADER_STAGE_FRAGMENT_BIT only (see SkyPipeline).
//
// std140 layout: every field is already vec4/mat4-sized/aligned (no bare
// float/int members), so no explicit padding/alignas is needed.
struct SkyUniformBufferObject {
    glm::mat4 invView; // only mat3(invView) (rotation) is used in-shader — the
                        // sky is an infinite background decoupled from the
                        // camera's world-space translation (see sky.frag).
    glm::mat4 invProj; // inverse of the same Y-flipped projection used for the quads.

    glm::vec4 sunDirection;      // xyz normalized, world space, pointing TOWARD the sun. w unused.
    glm::vec4 timeAndSun;        // x = time (s), y = sun angular radius (rad), z/w reserved.
    glm::vec4 atmosphereParams0; // x=planetRadius(km) y=atmosphereHeight(km) z=eyeHeight(km) w=rayleighScaleHeight(km)
    glm::vec4 atmosphereParams1; // x=mieScaleHeight(km) y=mieG z=sunIntensity w=reserved

    // Populated in Stage 2 (cloud raymarch); zero-initialized and unused by
    // Stage 1's atmosphere-only shader.
    glm::vec4 cloudParams0; // x=cloudLayerBottom(km above planetR) y=cloudLayerTop z=coverage w=densityMultiplier
    glm::vec4 cloudParams1; // x=windSpeed y=windDirection(rad) z=forwardG w=backG (dual-lobe Henyey-Greenstein)
    glm::vec4 cloudParams2; // x=powderStrength y=ambientStrength z=noiseUvScale w=reserved

    glm::ivec4 stepCounts; // x=atmosphere primary steps y=atmosphere sun steps z=cloud primary steps w=cloud shadow steps
};

} // namespace polyizon
