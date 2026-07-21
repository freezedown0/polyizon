#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 fogColorAndDensity; // rgb = fog color, a = density
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in float vViewDistance;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 baseColor = texture(texSampler, vTexCoord) * vec4(vColor, 1.0);

    // Exponential-squared distance fog: gentle near the camera, thickening
    // with distance — the same kind of atmospheric attenuation the sky pass
    // models via raymarching, applied here as a cheap per-object analytic
    // approximation since these quads are the only scene geometry.
    float fogDensity = ubo.fogColorAndDensity.a;
    float fogFactor = exp(-pow(vViewDistance * fogDensity, 2.0));
    vec3 finalColor = mix(ubo.fogColorAndDensity.rgb, baseColor.rgb, clamp(fogFactor, 0.0, 1.0));

    outColor = vec4(finalColor, baseColor.a);
}
