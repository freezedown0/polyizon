#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Per-frame-in-flight uniform data for the lit-scene pass (lit.vert/lit.frag,
// see LitPipeline). Shared across every entity drawn this frame; the
// per-entity model matrix and base color arrive via push constants instead
// (see LitPipeline's pipeline layout) — this scene has a handful of distinct
// meshes, not many identical instances, so there's no per-instance vertex
// buffer here unlike GraphicsPipeline/UniformBufferObject.
//
// std140 layout: every field is already mat4/vec4-sized/aligned, so no
// explicit padding/alignas is needed.
struct LitUniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    // The shadow-casting light's combined view*proj matrix (orthographic,
    // fit to the scene bounds) — transforms world-space positions into the
    // shadow map's [0,1] UV + depth space (see ShadowPipeline/ShadowMap).
    glm::mat4 lightSpaceMatrix;

    // xyz = normalized world-space direction TOWARD the sun (same convention
    // as SkyUniformBufferObject::sunDirection, so the lit scene's shading
    // stays consistent with the sky's sun position). w = ambient term added
    // to the N.L diffuse term so shadowed/back faces aren't pure black.
    glm::vec4 sunDirectionAndAmbient;
};

} // namespace polyizon
