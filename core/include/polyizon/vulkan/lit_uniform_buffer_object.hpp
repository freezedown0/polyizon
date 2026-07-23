#pragma once

#include <glm/glm.hpp>

namespace polyizon {

// Fixed simultaneous-light caps for the Phase 19 point/spot light arrays
// below — a scene with more than this many active PointLightComponent/
// SpotLightComponent entities just has the extras silently ignored (see
// EditorViewportRenderer::UpdateLitUniformBuffer), same precedent as
// kMaxFramesInFlight elsewhere. Must match kMaxPointLights/kMaxSpotLights in
// lit.frag and voxel_lit.frag exactly (no shared C++/GLSL header this
// engine's build supports yet — duplicated by hand like every other
// shader/C++ constant pairing here, e.g. voxel_lit.frag's kVoxelDepthSlices).
inline constexpr int kMaxPointLights = 4;
inline constexpr int kMaxSpotLights = 4;

// Per-frame-in-flight uniform data for the lit-scene pass (lit.vert/lit.frag,
// see LitPipeline). Shared across every entity drawn this frame; the
// per-entity model matrix and base color arrive via push constants instead
// (see LitPipeline's pipeline layout) — this scene has a handful of distinct
// meshes, not many identical instances, so there's no per-instance vertex
// buffer here unlike GraphicsPipeline/UniformBufferObject.
//
// std140 layout: every field is already mat4/vec4-sized/aligned (including
// the point/spot light arrays below, deliberately all vec4-based rather than
// vec3 to avoid std140's vec3-in-an-array padding footguns), so no explicit
// padding/alignas is needed.
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

    // Phase 19: point/spot lights (see PointLightComponent/SpotLightComponent
    // in polyizon/scene/components.hpp) — no shadows from these, just a
    // forward-additive Lambertian + distance-attenuated (and, for spot,
    // cone-attenuated) contribution computed in lit.frag/voxel_lit.frag.
    // Only the first lightCounts.x/y slots of each array are valid this
    // frame — unused trailing slots are left default-initialized and never
    // read by the shader's bounded loops.
    glm::vec4 pointLightPositionAndRange[kMaxPointLights]; // xyz position, w range
    glm::vec4 pointLightColorAndIntensity[kMaxPointLights]; // rgb color, a intensity

    glm::vec4 spotLightPositionAndRange[kMaxSpotLights]; // xyz position, w range
    glm::vec4 spotLightColorAndIntensity[kMaxSpotLights]; // rgb color, a intensity
    // xyz = normalized world-space direction the light points (Transform::
    // GetForward()), w = cos(innerConeDegrees) — the fully-lit cone edge.
    glm::vec4 spotLightDirectionAndInnerCos[kMaxSpotLights];
    // x = cos(outerConeDegrees) — the light fades to zero between inner and
    // outer. yzw unused (kept vec4-sized for std140 array-stride safety).
    glm::vec4 spotLightOuterCos[kMaxSpotLights];

    // x = active point light count this frame, y = active spot light count.
    // zw unused.
    glm::ivec4 lightCounts;
};

} // namespace polyizon
