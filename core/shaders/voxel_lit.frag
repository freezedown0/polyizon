#version 450

// Voxel lighting mode's lit shading: identical Lambertian N.L + ambient
// model as lit.frag, but the shadow term comes from a deliberately coarse
// (e.g. 4x4) shadow volume sampled+compared manually rather than lit.frag's
// smooth hardware-PCF sampler2DShadow tap — see
// core/include/polyizon/vulkan/shadow_map.hpp's ShadowSamplerMode.

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

// Plain (non-comparison) sampler, NEAREST-filtered — reading the raw stored
// depth back so this shader can quantize+compare it manually instead of
// relying on hardware PCF (which would smoothly blend across the shadow
// map's texels, hiding the low resolution's intended blockiness).
layout(set = 0, binding = 1) uniform sampler2D voxelShadowMap;

layout(push_constant) uniform PushConstants {
    mat4 model; // unused in this stage, see lit.vert (reused unchanged)
    vec4 baseColor;
} pc;

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec4 vLightSpacePos;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

// Discretizes the light-space depth range into this many buckets before
// comparing — combined with the shadow map's own low texel resolution (see
// EditorViewportRenderer's m_VoxelShadowMap), this is what gives the "4x4x4"
// blocky look: 4 texels across each axis of the frustum, 4 depth slices
// along it.
const float kVoxelDepthSlices = 4.0;

float SampleVoxelShadow(vec4 lightSpacePos) {
    vec3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        // Outside the light's orthographic frustum: fully lit, matching
        // ShadowMap's CLAMP_TO_BORDER/opaque-white convention used by the
        // hardware-PCF path.
        return 1.0;
    }

    float occluderDepth = texture(voxelShadowMap, uv).r;
    float fragDepth = ndc.z;

    // A fragment is lit if nothing closer to the light was recorded in its
    // depth bucket (or an earlier one) — quantizing both sides first is what
    // turns the usual continuous compare into a stepped one.
    float occluderSlice = floor(occluderDepth * kVoxelDepthSlices);
    float fragSlice = floor(fragDepth * kVoxelDepthSlices);
    return fragSlice <= occluderSlice ? 1.0 : 0.0;
}

// Identical to lit.frag's function of the same name — see there for the
// falloff/formula rationale. No voxel-specific quantization here: only the
// sun's shadow is deliberately blocky, point/spot lights have no shadows in
// either mode.
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
    float shadow = SampleVoxelShadow(vLightSpacePos);

    vec3 lit = pc.baseColor.rgb * (ambient + diffuse * shadow);
    lit += pc.baseColor.rgb * (ComputePointLightContribution(vWorldPos, normal) + ComputeSpotLightContribution(vWorldPos, normal));
    outColor = vec4(lit, pc.baseColor.a);
}
