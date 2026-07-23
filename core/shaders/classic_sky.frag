#version 450

// Voxel lighting mode's sky: a flat horizon->zenith gradient plus a simple
// sun disc, no atmospheric scattering raymarch and no volumetric clouds —
// see core/include/polyizon/vulkan/classic_sky_uniform_buffer_object.hpp for
// the exact field layout this must match (std140, field order matters).
// Reuses sky.vert unchanged (see ClassicSkyPipeline) — the fullscreen
// triangle trick has no mode-specific vertex-side logic.

layout(set = 0, binding = 0) uniform ClassicSkyUBO {
    mat4 invView;
    mat4 invProj;
    vec4 sunDirection; // xyz normalized toward sun, w = sun angular radius (rad)
    vec4 dayHorizonColor;
    vec4 dayZenithColor;
    vec4 nightHorizonColor;
    vec4 nightZenithColor;
} sky;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 viewPos = sky.invProj * vec4(vNdc, 1.0, 1.0);
    viewPos /= viewPos.w;
    // Only the rotation part of invView is used — same camera-translation-
    // decoupled convention as sky.frag.
    vec3 rayDir = normalize(mat3(sky.invView) * viewPos.xyz);
    vec3 sunDir = normalize(sky.sunDirection.xyz);

    // Day/night blend driven purely by sun elevation, smoothed across the
    // horizon rather than a hard cut.
    float dayFactor = smoothstep(-0.15, 0.15, sunDir.y);
    vec3 horizonColor = mix(sky.nightHorizonColor.rgb, sky.dayHorizonColor.rgb, dayFactor);
    vec3 zenithColor = mix(sky.nightZenithColor.rgb, sky.dayZenithColor.rgb, dayFactor);

    // The whole "classic sky" look: one lerp by view-ray elevation, no
    // scattering integral.
    float skyT = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 color = mix(horizonColor, zenithColor, skyT);

    // Same soft-edged sun-disc shape as sky.frag's sun disk term, without an
    // exposure/tonemap pass afterward — these colors are already display-range.
    float sunAngularRadius = sky.sunDirection.w;
    float cosTheta = dot(rayDir, sunDir);
    float sunDisk = smoothstep(cos(sunAngularRadius * 1.05), cos(sunAngularRadius * 0.85), cosTheta);
    color += sunDisk * vec3(1.0, 0.95, 0.85) * clamp(dayFactor, 0.2, 1.0);

    outColor = vec4(color, 1.0);
}
