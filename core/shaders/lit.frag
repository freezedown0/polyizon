#version 450

// Must match kMaxPointLights/kMaxSpotLights in
// core/include/polyizon/vulkan/lit_uniform_buffer_object.hpp exactly (see
// that header's comment on why there's no shared C++/GLSL constant here).
#define kMaxPointLights 4
#define kMaxSpotLights 4

layout(set = 0, binding = 0) uniform LitUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirectionAndAmbient;

    vec4 pointLightPositionAndRange[kMaxPointLights];
    vec4 pointLightColorAndIntensity[kMaxPointLights];

    vec4 spotLightPositionAndRange[kMaxSpotLights];
    vec4 spotLightColorAndIntensity[kMaxSpotLights];
    vec4 spotLightDirectionAndInnerCos[kMaxSpotLights];
    vec4 spotLightOuterCos[kMaxSpotLights];

    ivec4 lightCounts; // x = active point count, y = active spot count
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
layout(location = 2) in vec3 vWorldPos;

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

// No shadows for point/spot lights (only the sun casts one, see
// ShadowMap/ShadowPipeline) — just Lambertian N.L with a smooth
// distance-squared falloff to `range`.
vec3 ComputePointLightContribution(vec3 worldPos, vec3 normal) {
    vec3 total = vec3(0.0);
    for (int i = 0; i < ubo.lightCounts.x; ++i) {
        vec3 lightPos = ubo.pointLightPositionAndRange[i].xyz;
        float range = ubo.pointLightPositionAndRange[i].w;
        vec3 toLight = lightPos - worldPos;
        float dist = length(toLight);
        if (dist >= range) {
            continue;
        }
        vec3 lightDir = toLight / max(dist, 0.0001);
        float ndotl = max(dot(normal, lightDir), 0.0);
        float attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
        attenuation *= attenuation;
        vec3 color = ubo.pointLightColorAndIntensity[i].rgb;
        float intensity = ubo.pointLightColorAndIntensity[i].a;
        total += color * intensity * ndotl * attenuation;
    }
    return total;
}

// Same falloff as ComputePointLightContribution, additionally narrowed to a
// cone around spotLightDirectionAndInnerCos via a smooth inner/outer-cosine
// edge (standard spotlight cone-attenuation formula).
vec3 ComputeSpotLightContribution(vec3 worldPos, vec3 normal) {
    vec3 total = vec3(0.0);
    for (int i = 0; i < ubo.lightCounts.y; ++i) {
        vec3 lightPos = ubo.spotLightPositionAndRange[i].xyz;
        float range = ubo.spotLightPositionAndRange[i].w;
        vec3 toLight = lightPos - worldPos;
        float dist = length(toLight);
        if (dist >= range) {
            continue;
        }
        vec3 lightDir = toLight / max(dist, 0.0001);
        float ndotl = max(dot(normal, lightDir), 0.0);

        vec3 spotDir = ubo.spotLightDirectionAndInnerCos[i].xyz;
        float innerCos = ubo.spotLightDirectionAndInnerCos[i].w;
        float outerCos = ubo.spotLightOuterCos[i].x;
        float cosAngle = dot(-lightDir, spotDir);
        float coneFactor = clamp((cosAngle - outerCos) / max(innerCos - outerCos, 0.0001), 0.0, 1.0);

        float attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
        attenuation *= attenuation;

        vec3 color = ubo.spotLightColorAndIntensity[i].rgb;
        float intensity = ubo.spotLightColorAndIntensity[i].a;
        total += color * intensity * ndotl * attenuation * coneFactor;
    }
    return total;
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 sunDir = ubo.sunDirectionAndAmbient.xyz;
    float ambient = ubo.sunDirectionAndAmbient.w;

    float diffuse = max(dot(normal, sunDir), 0.0);
    float shadow = SampleShadow(vLightSpacePos);

    vec3 lit = pc.baseColor.rgb * (ambient + diffuse * shadow);
    lit += pc.baseColor.rgb * (ComputePointLightContribution(vWorldPos, normal) + ComputeSpotLightContribution(vWorldPos, normal));
    outColor = vec4(lit, pc.baseColor.a);
}
