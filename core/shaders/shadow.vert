#version 450

// Depth-only pass: writes gl_Position only, no fragment shader stage at all
// (see ShadowPipeline) — this pipeline exists purely to populate ShadowMap's
// depth image from the light's point of view.
layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMVP; // lightSpaceMatrix (see lit_uniform_buffer_object.hpp) * this entity's model matrix
} pc;

layout(location = 0) in vec3 inPosition;
// location 1 (normal) is present in Vertex3D's vertex input state but
// unused/undeclared here — a shader may ignore vertex attributes the
// pipeline provides, no need to declare every location.

void main() {
    gl_Position = pc.lightSpaceMVP * vec4(inPosition, 1.0);
}
