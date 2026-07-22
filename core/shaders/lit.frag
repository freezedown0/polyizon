#version 450

layout(set = 0, binding = 0) uniform LitUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirectionAndAmbient;
} ubo;

// ShadowMap's comparison sampler (see shadow_map.hpp): a single texture()
// tap here does hardware percentage-closer filtering and returns a [0,1]
// visibility factor directly (1 = fully lit, 0 = fully shadowed) — no manual
// PCF loop needed this phase.
layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(push_constant) uniform PushConstants {
    mat4 model; // unused in this stage, see lit.vert
    vec4 baseColor;
} pc;

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec4 vLightSpacePos;

layout(location = 0) out vec4 outColor;

float SampleShadow(vec4 lightSpacePos) {
    vec3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    // NDC xy is [-1,1] -> shadow map UV needs [0,1]. NDC z is already in
    // [0,1] here (GLM_FORCE_DEPTH_ZERO_TO_ONE, see core/CMakeLists.txt),
    // matching this compare-sampler's expected reference-depth range
    // directly - no extra remap needed for z.
    vec2 uv = ndc.xy * 0.5 + 0.5;
    return texture(shadowMap, vec3(uv, ndc.z));
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 sunDir = ubo.sunDirectionAndAmbient.xyz;
    float ambient = ubo.sunDirectionAndAmbient.w;

    float diffuse = max(dot(normal, sunDir), 0.0);
    float shadow = SampleShadow(vLightSpacePos);

    vec3 lit = pc.baseColor.rgb * (ambient + diffuse * shadow);
    outColor = vec4(lit, pc.baseColor.a);
}
