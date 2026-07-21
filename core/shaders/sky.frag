#version 450

// Real-time raymarched atmosphere (Stage 1: Rayleigh + Mie single-scattering,
// dynamic sun) and, once Stage 2 lands, volumetric clouds composited on top
// — all in one fragment invocation per pixel, no precomputed LUTs, no
// compute shader (this engine has neither yet). See
// core/include/polyizon/vulkan/sky_uniform_buffer_object.hpp for the exact
// field layout this must match (std140, field order matters).

const float kPi = 3.14159265359;

layout(set = 0, binding = 0) uniform SkyUBO {
    mat4 invView;
    mat4 invProj;
    vec4 sunDirection;
    vec4 timeAndSun;
    vec4 atmosphereParams0;
    vec4 atmosphereParams1;
    vec4 cloudParams0; // x=layerBottom(km above planetR) y=layerTop z=coverage w=densityMultiplier
    vec4 cloudParams1; // x=windSpeed y=windDirection(rad) z=forwardG w=backG
    vec4 cloudParams2; // x=powderStrength y=ambientStrength z=noiseUvScale w=reserved
    ivec4 stepCounts;  // x=atmosphere primary y=atmosphere sun z=cloud primary w=cloud shadow
} sky;

layout(set = 0, binding = 1) uniform sampler3D cloudNoise;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

// Standard analytic sphere intersection: returns (tNear, tFar) along the ray,
// or a negative tFar if there's no intersection. ro is relative to the
// sphere's center.
vec2 RaySphereIntersect(vec3 ro, vec3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) {
        return vec2(-1.0);
    }
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

float RayleighPhase(float mu) {
    return 3.0 / (16.0 * kPi) * (1.0 + mu * mu);
}

float MiePhase(float mu, float g) {
    float g2 = g * g;
    return (3.0 * (1.0 - g2)) / (2.0 * (2.0 + g2)) * (1.0 + mu * mu) / pow(1.0 + g2 - 2.0 * g * mu, 1.5);
}

// Nested raymarch single-scattering atmosphere: a primary raymarch along the
// view ray accumulates Rayleigh/Mie optical depth; at each sample, a
// secondary raymarch toward the sun computes how much of the in-scattered
// light actually reaches that point (sun-side optical depth / self-shadow by
// the planet). Stylized constants (see UpdateSkyUniformBuffer), not literal
// Earth scale — chosen to look right at this engine's current world scale.
vec3 ComputeAtmosphere(vec3 ro, vec3 rd, vec3 sunDir, float planetR, float atmR,
                        float rayleighH, float mieH, vec3 betaR, float betaM, float mieG, float sunIntensity,
                        int primarySteps, int sunSteps) {
    vec2 atmHit = RaySphereIntersect(ro, rd, atmR);
    if (atmHit.y < 0.0) {
        return vec3(0.0);
    }
    float tMin = max(atmHit.x, 0.0);
    float tMax = atmHit.y;

    vec2 groundHit = RaySphereIntersect(ro, rd, planetR);
    if (groundHit.x > 0.0) {
        tMax = min(tMax, groundHit.x);
    }

    float segLen = (tMax - tMin) / float(primarySteps);
    float t = tMin;
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;
    vec3 totalR = vec3(0.0);
    vec3 totalM = vec3(0.0);

    float mu = dot(rd, sunDir);
    float phaseR = RayleighPhase(mu);
    float phaseM = MiePhase(mu, mieG);

    for (int i = 0; i < primarySteps; ++i) {
        vec3 p = ro + rd * (t + segLen * 0.5);
        float height = length(p) - planetR;
        float hr = exp(-height / rayleighH) * segLen;
        float hm = exp(-height / mieH) * segLen;
        opticalDepthR += hr;
        opticalDepthM += hm;

        vec2 sunHit = RaySphereIntersect(p, sunDir, atmR);
        float sunSegLen = sunHit.y / float(sunSteps);
        float sunOpticalDepthR = 0.0;
        float sunOpticalDepthM = 0.0;
        bool inShadow = false;
        for (int j = 0; j < sunSteps; ++j) {
            vec3 sp = p + sunDir * (sunSegLen * (float(j) + 0.5));
            float sh = length(sp) - planetR;
            if (sh < 0.0) {
                inShadow = true;
                break;
            }
            sunOpticalDepthR += exp(-sh / rayleighH) * sunSegLen;
            sunOpticalDepthM += exp(-sh / mieH) * sunSegLen;
        }

        if (!inShadow) {
            vec3 tau = betaR * (opticalDepthR + sunOpticalDepthR) + vec3(betaM * 1.1) * (opticalDepthM + sunOpticalDepthM);
            vec3 attenuation = exp(-tau);
            totalR += attenuation * hr;
            totalM += attenuation * hm;
        }
        t += segLen;
    }

    return (totalR * betaR * phaseR + totalM * vec3(betaM) * phaseM) * sunIntensity;
}

// Cloud density at world-space point p (in the same km-scale space as the
// atmosphere raymarch). Samples the baked Perlin-Worley noise volume (see
// noise.hpp): R = base billowy shape, G/B/A = increasing-frequency Worley
// erosion detail, blended in with decreasing weight per Schneider's talk.
// heightFraction (0 at layer bottom, 1 at layer top) drives a gradient that
// fades density to zero at both edges of the cloud shell.
float RemapCloud(float density, float coverage, float heightFraction) {
    float gradient = smoothstep(0.0, 0.2, heightFraction) * smoothstep(1.0, 0.7, heightFraction);
    density *= gradient;
    return clamp((density - (1.0 - coverage)) / max(coverage, 0.0001), 0.0, 1.0);
}

float SampleCloudDensity(vec3 p, float planetR, float bottom, float top, float coverage,
                          float densityMult, vec3 windOffset, float noiseScale) {
    vec4 n = texture(cloudNoise, p * noiseScale + windOffset);
    float baseShape = n.r;
    float erosion = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    // Erode more aggressively than a flat subtraction: edges/thin regions
    // (low baseShape) get carved away almost entirely by low erosion values,
    // while dense cores survive — this is what turns a smooth haze into
    // distinct, separated puffs instead of one continuous translucent layer.
    float shaped = clamp(baseShape - (1.0 - erosion) * 0.55, 0.0, 1.0);
    // Extra contrast: push mid/low densities down further, keep strong cores
    // relatively intact, sharpening the visible silhouette.
    shaped = pow(shaped, 1.6);
    float heightFraction = clamp((length(p) - (planetR + bottom)) / max(top - bottom, 0.0001), 0.0, 1.0);
    return RemapCloud(shaped, coverage, heightFraction) * densityMult;
}

// ACES filmic tonemap (Narkowicz 2015 fit): compresses unbounded HDR
// scattering values into a displayable [0,1] range with a smooth shoulder
// instead of hard-clipping to white. This is what actually fixes the
// "too bright" sky — without it, any pixel whose raw scattering/sun-disk
// value exceeds 1.0 clips to flat white over however wide an area that
// condition holds, which reads as a giant blown-out glow instead of a
// small, well-defined bright sun.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float HenyeyGreenstein(float mu, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * mu, 1e-4), 1.5));
}

// Short raymarch from a cloud sample point toward the sun, accumulating
// density to approximate self-shadowing (Beer's-law-style transmittance).
float SunShadowRaymarch(vec3 p, vec3 sunDir, float planetR, float bottom, float top, float coverage,
                         float densityMult, vec3 windOffset, float noiseScale, int steps) {
    float stepSize = max(top - bottom, 0.0001) / float(max(steps, 1)) * 0.5;
    float accum = 0.0;
    for (int i = 0; i < steps; ++i) {
        p += sunDir * stepSize;
        accum += SampleCloudDensity(p, planetR, bottom, top, coverage, densityMult, windOffset, noiseScale) * stepSize;
    }
    return exp(-accum);
}

// Front-to-back raymarch through the cloud-layer shell. Returns
// (scatteredLight.rgb, remainingTransmittance) — composited over the
// atmosphere result in main() as color*transmittance + scatteredLight.
vec4 RaymarchClouds(vec3 ro, vec3 rd, vec3 sunDir, float planetR, float bottom, float top,
                     float coverage, float densityMult, vec3 windOffset, float noiseScale,
                     float forwardG, float backG, float powder, float ambient, int primarySteps, int shadowSteps) {
    vec2 innerHit = RaySphereIntersect(ro, rd, planetR + bottom);
    vec2 outerHit = RaySphereIntersect(ro, rd, planetR + top);
    if (outerHit.y < 0.0) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    float tStart = (innerHit.y > 0.0) ? max(innerHit.y, 0.0) : max(outerHit.x, 0.0);
    float tEnd = outerHit.y;

    // Clip against the ground, exactly like ComputeAtmosphere does: a ray
    // from inside these spheres pointing at the ground still mathematically
    // "exits" on the far side (through the planet interior) with no ground
    // mesh to naturally block it — without this, looking straight down would
    // raymarch clouds through the virtual planet surface.
    vec2 groundHit = RaySphereIntersect(ro, rd, planetR);
    if (groundHit.x > 0.0) {
        tEnd = min(tEnd, groundHit.x);
    }

    if (tEnd <= tStart || primarySteps <= 0) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float stepSize = (tEnd - tStart) / float(primarySteps);
    float mu = dot(rd, sunDir);
    float phase = mix(HenyeyGreenstein(mu, forwardG), HenyeyGreenstein(mu, backG), 0.5);

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    float t = tStart;
    for (int i = 0; i < primarySteps && transmittance > 0.01; ++i) {
        vec3 p = ro + rd * (t + stepSize * 0.5);
        float density = SampleCloudDensity(p, planetR, bottom, top, coverage, densityMult, windOffset, noiseScale);
        if (density > 0.001) {
            float sunT = SunShadowRaymarch(p, sunDir, planetR, bottom, top, coverage, densityMult, windOffset, noiseScale, shadowSteps);
            float powderTerm = 1.0 - exp(-density * 2.0 * powder);
            vec3 sunColor = vec3(1.0, 0.95, 0.85) * max(sunDir.y, 0.05) * 2.0;
            // Ambient is intentionally weak relative to direct sun*shadow
            // lighting: sunT alone should carve visible dark undersides vs
            // bright, sun-facing tops. A strong flat ambient term is what
            // flattened the earlier result into a uniform haze.
            vec3 luminance = sunColor * phase * sunT * powderTerm + vec3(0.35, 0.4, 0.55) * ambient * (0.3 + 0.7 * sunT);
            float stepTransmittance = exp(-density * stepSize * 4.0);
            scattered += transmittance * luminance * (1.0 - stepTransmittance);
            transmittance *= stepTransmittance;
        }
        t += stepSize;
    }
    return vec4(scattered, transmittance);
}

void main() {
    vec4 viewPos = sky.invProj * vec4(vNdc, 1.0, 1.0);
    viewPos /= viewPos.w;
    // Only the rotation part of invView is used: the sky is an infinite
    // background decoupled from the camera's world-space translation (see
    // sky_uniform_buffer_object.hpp).
    vec3 rayDir = normalize(mat3(sky.invView) * viewPos.xyz);
    vec3 sunDir = normalize(sky.sunDirection.xyz);

    float planetR = sky.atmosphereParams0.x;
    float atmH = sky.atmosphereParams0.y;
    float eyeHeight = sky.atmosphereParams0.z;
    float rayleighH = sky.atmosphereParams0.w;
    float mieH = sky.atmosphereParams1.x;
    float mieG = sky.atmosphereParams1.y;
    float sunIntensity = sky.atmosphereParams1.z;
    float exposure = sky.atmosphereParams1.w;

    // Fixed virtual eye point, not the literal (tiny-scale) camera position —
    // see sky_uniform_buffer_object.hpp's invView comment.
    vec3 rayOrigin = vec3(0.0, planetR + eyeHeight, 0.0);

    const vec3 betaR = vec3(0.0058, 0.0135, 0.0331); // per km, standard Rayleigh scattering coefficients
    const float betaM = 0.021;                        // per km, Mie extinction coefficient

    vec3 color = ComputeAtmosphere(rayOrigin, rayDir, sunDir, planetR, planetR + atmH,
        rayleighH, mieH, betaR, betaM, mieG, sunIntensity, sky.stepCounts.x, sky.stepCounts.y);

    // Sun disk: a soft-edged core (smoothstep across a couple degrees, not a
    // hard cutoff) around the exact sun direction, faded by the sun's own
    // elevation so it doesn't punch through the horizon glow at
    // sunrise/sunset in a physically silly way. Kept moderate in raw
    // intensity — ACESFilm below gives it a natural bright-but-bounded look
    // instead of relying on a huge multiplier that would've just clipped to
    // white without tonemapping.
    float sunAngularRadius = sky.timeAndSun.y;
    float cosTheta = dot(rayDir, sunDir);
    float sunDisk = smoothstep(cos(sunAngularRadius * 1.05), cos(sunAngularRadius * 0.85), cosTheta);
    color += sunDisk * vec3(1.0, 0.95, 0.85) * max(sunDir.y, 0.05) * 1.5;

    // Dark-navy night floor so a fully-set sun doesn't read as pure black.
    color = max(color, vec3(0.0005, 0.0007, 0.0012));

    float windSpeed = sky.cloudParams1.x;
    float windDirection = sky.cloudParams1.y;
    vec3 windOffset = vec3(
        sky.timeAndSun.x * windSpeed * cos(windDirection),
        0.0,
        sky.timeAndSun.x * windSpeed * sin(windDirection));

    vec4 clouds = RaymarchClouds(rayOrigin, rayDir, sunDir, planetR,
        sky.cloudParams0.x, sky.cloudParams0.y, sky.cloudParams0.z, sky.cloudParams0.w,
        windOffset, sky.cloudParams2.z, sky.cloudParams1.z, sky.cloudParams1.w,
        sky.cloudParams2.x, sky.cloudParams2.y, sky.stepCounts.z, sky.stepCounts.w);

    color = color * clouds.a + clouds.rgb;

    // Tonemap once, at the very end, over the fully composited HDR result
    // (atmosphere + sun disk + clouds) — applying it earlier/per-term would
    // double-compress the parts that get added afterward.
    color = ACESFilm(color * exposure);

    outColor = vec4(color, 1.0);
}
