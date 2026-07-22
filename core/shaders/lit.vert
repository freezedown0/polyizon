#version 450

layout(set = 0, binding = 0) uniform LitUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirectionAndAmbient;
} ubo;

// Per-entity data (see lit_pipeline.hpp's LitPushConstants) — this scene has
// a handful of distinct meshes, not many identical instances, so each is
// drawn with its own model matrix/color via push constants rather than a
// per-instance vertex buffer (contrast triangle.vert's inModelCol0..3).
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColor; // unused in this stage, see lit.frag
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out vec4 vLightSpacePos;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    // mat3(model) (rather than a full inverse-transpose normal matrix) is
    // correct as long as model only ever applies uniform scale — true for
    // this phase's plane/cube test entities (see TransformComponent). Revisit
    // if a future scene needs non-uniform scaling.
    vWorldNormal = mat3(pc.model) * inNormal;
    vLightSpacePos = ubo.lightSpaceMatrix * worldPos;
}
