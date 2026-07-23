#version 450

// Voxel lighting mode's lit shading: identical Lambertian N.L + ambient
// model as lit.frag, but the shadow term comes from a deliberately coarse
// (e.g. 4x4) shadow volume sampled+compared manually rather than lit.frag's
// smooth hardware-PCF sampler2DShadow tap — see
// core/include/polyizon/vulkan/shadow_map.hpp's ShadowSamplerMode.

layout(set = 0, binding = 0) uniform LitUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirectionAndAmbient;
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

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 sunDir = ubo.sunDirectionAndAmbient.xyz;
    float ambient = ubo.sunDirectionAndAmbient.w;

    float diffuse = max(dot(normal, sunDir), 0.0);
    float shadow = SampleVoxelShadow(vLightSpacePos);

    vec3 lit = pc.baseColor.rgb * (ambient + diffuse * shadow);
    outColor = vec4(lit, pc.baseColor.a);
}
