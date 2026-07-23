#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Flat, unlit solid color - the gizmo is a UI overlay, not scene
    // geometry (see GizmoPipeline: depth test/write are both disabled).
    outColor = pc.color;
}
