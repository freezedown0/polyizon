#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 3) in vec4 inModelCol0;
layout(location = 4) in vec4 inModelCol1;
layout(location = 5) in vec4 inModelCol2;
layout(location = 6) in vec4 inModelCol3;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out float vViewDistance;

void main() {
    mat4 model = mat4(inModelCol0, inModelCol1, inModelCol2, inModelCol3);
    vec4 viewPos = ubo.view * model * vec4(inPosition, 0.0, 1.0);
    gl_Position = ubo.proj * viewPos;
    vColor = inColor;
    vTexCoord = inTexCoord;
    // Camera-space distance, interpolated to the fragment shader for the
    // exponential distance fog (see triangle.frag) — cheaper and just as
    // correct here as reconstructing it from depth, since this is the only
    // geometry in the scene.
    vViewDistance = length(viewPos.xyz);
}
