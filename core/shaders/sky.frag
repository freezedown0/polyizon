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
float SampleCloudDensity(vec3 p, float planetR, float bottom, float top, float coverage,
                          float densityMult, vec3 windOffset, float noiseScale) {
    float heightFraction = clamp((length(p) - (planetR + bottom)) / max(top - bottom, 0.0001), 0.0, 1.0);

    // Traverse most of the 3D texture vertically across the cloud layer.
    // The previous p*noiseScale mapping covered only about 0.05 texture
    // units from base to top, so it behaved like one extruded 2D slice.
    const vec2 noiseOrigin = vec2(0.10, 0.40);
    vec3 shapeUv = vec3(
        p.x * noiseScale + windOffset.x,
        heightFraction * 0.82 + 0.11,
        p.z * noiseScale + windOffset.z);
    shapeUv.xz += noiseOrigin;
    vec4 n = texture(cloudNoise, shapeUv);

    // A low-frequency base-noise sample is the weather map. It creates
    // broad clear regions and clustered cloud systems before local 3D shape
    // and erosion are applied.
    float weatherScale = noiseScale * 0.18;
    vec3 weatherUv = vec3(
        p.x * weatherScale + windOffset.x * 0.18,
        0.37,
        p.z * weatherScale + windOffset.z * 0.18);
    weatherUv.xz += noiseOrigin * 0.18;
    float weather = texture(cloudNoise, weatherUv).r;
    float weatherThreshold = mix(0.62, 0.34, clamp(coverage, 0.0, 1.0));
    float cloudField = smoothstep(weatherThreshold - 0.06, weatherThreshold + 0.08, weather);

    // Weak weather cells form low clouds; strong cells grow tall, rounded
    // cauliflower tops. The sharp lower ramp preserves a condensation-level
    // base while the upper ramp stays soft and irregular.
    float cloudType = smoothstep(weatherThreshold - 0.02, min(weatherThreshold + 0.10, 0.98), weather);
    float topHeight = mix(0.48, 1.0, cloudType);
    float baseProfile = smoothstep(0.0, 0.07, heightFraction);
    float topProfile = 1.0 - smoothstep(max(topHeight - 0.2, 0.08), topHeight, heightFraction);
    float heightProfile = baseProfile * topProfile;

    // R is the Perlin-Worley body; G/B/A are progressively finer Worley
    // octaves. Erode the boundary while leaving dense interiors intact.
    float erosion = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float localThreshold = mix(0.56, 0.34, clamp(coverage, 0.0, 1.0));
    float baseDensity = smoothstep(localThreshold, localThreshold + 0.12, n.r);
    float edgeErosion = (1.0 - erosion) * 0.32 * (1.0 - baseDensity * 0.65);
    float shapedDensity = clamp(baseDensity - edgeErosion, 0.0, 1.0);
    return shapedDensity * cloudField * heightProfile * densityMult;
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

// Raymarch all the way from a cloud sample to the sun-facing edge of the
// outer cloud shell. Using a fixed fraction of the layer thickness here is
// incorrect for oblique sunlight: at low sun elevations that short ray ends
// while it is still inside the cloud and misses most of the optical depth.
float SunOpticalDepth(vec3 p, vec3 sunDir, float planetR, float bottom, float top, float coverage,
                       float densityMult, vec3 windOffset, float noiseScale, int steps) {
    vec2 shellHit = RaySphereIntersect(p, sunDir, planetR + top);
    if (shellHit.y <= 0.0 || steps <= 0) {
        return 0.0;
    }

    // Local self-shadowing is visually dominant. At grazing angles the
    // spherical exit can be hundreds of kilometres away; integrating every
    // distant cloud bank would make the whole sky uniformly gray and is too
    // undersampled for this small secondary-ray budget.
    float rayLength = min(shellHit.y, 18.0);
    float opticalDepth = 0.0;
    float previousDistance = 0.0;
    for (int i = 0; i < steps; ++i) {
        // Quadratic placement spends more of the small shadow budget near
        // the shaded point while still reaching the shell edge at grazing
        // sun angles.
        float u = (float(i) + 1.0) / float(steps);
        float distance = rayLength * u * u;
        float segmentLength = distance - previousDistance;
        vec3 samplePoint = p + sunDir * (previousDistance + segmentLength * 0.5);
        opticalDepth += SampleCloudDensity(samplePoint, planetR, bottom, top, coverage,
            densityMult, windOffset, noiseScale) * segmentLength;
        previousDistance = distance;
    }
    return opticalDepth;
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

    // Very shallow rays can remain in the spherical layer for hundreds of
    // kilometres. A finite cloud horizon avoids undersampled stripe bands
    // and lets distant formations dissolve naturally into atmospheric haze.
    const float cloudFadeStart = 55.0;
    const float cloudRenderDistance = 80.0;
    tEnd = min(tEnd, cloudRenderDistance);
    if (tEnd <= tStart) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float stepSize = (tEnd - tStart) / float(primarySteps);
    float mu = dot(rd, sunDir);
    // The forward lobe carries most of the energy and produces the bright
    // rim around the sun. The weaker back lobe keeps the opposite side from
    // looking completely flat. Multiplying by 4pi converts the normalized
    // phase function into a convenient unit-average lighting response.
    float phase = mix(HenyeyGreenstein(mu, backG), HenyeyGreenstein(mu, forwardG), 0.8) * (4.0 * kPi);

    float daylight = smoothstep(-0.08, 0.04, sunDir.y);
    float sunWarmth = smoothstep(-0.02, 0.35, sunDir.y);
    vec3 sunColor = mix(vec3(1.0, 0.28, 0.08), vec3(1.0, 0.95, 0.82), sunWarmth);
    vec3 zenithAmbient = mix(vec3(0.015, 0.02, 0.04), vec3(0.24, 0.38, 0.62), daylight);

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    // Stable screen-space jitter hides equally spaced raymarch bands without
    // introducing time-dependent shimmer.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float t = tStart + stepSize * (0.5 + (jitter - 0.5) * 0.3);
    for (int i = 0; i < primarySteps && transmittance > 0.01; ++i) {
        vec3 p = ro + rd * t;
        float density = SampleCloudDensity(p, planetR, bottom, top, coverage, densityMult, windOffset, noiseScale);
        density *= 1.0 - smoothstep(cloudFadeStart, cloudRenderDistance, t);
        if (density > 0.001) {
            float sunDepth = SunOpticalDepth(p, sunDir, planetR, bottom, top, coverage,
                densityMult, windOffset, noiseScale, shadowSteps);
            float directT = exp(-sunDepth * 1.5);

            // A cheap second scattering order: light penetrating dense cloud
            // is softer and less directional, preventing pitch-black cores
            // without washing out the primary self-shadowing.
            float bouncedT = exp(-sunDepth * 0.35);
            float powderTerm = 1.0 + powder * (1.0 - exp(-density * 2.0));
            float heightFraction = clamp((length(p) - (planetR + bottom)) / max(top - bottom, 0.0001), 0.0, 1.0);
            float ambientHeight = mix(0.35, 1.0, smoothstep(0.0, 0.7, heightFraction));

            vec3 directLight = sunColor * daylight * phase * directT * powderTerm;
            vec3 bouncedLight = sunColor * daylight * bouncedT * 0.16;
            vec3 ambientLight = zenithAmbient * ambient * ambientHeight;
            vec3 luminance = directLight + bouncedLight + ambientLight;
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
    // Rotation reconstructs the view ray. Translation is used separately
    // below for cloud parallax while the atmosphere remains infinite.
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

    // Fixed virtual eye point for atmospheric scattering.
    vec3 atmosphereOrigin = vec3(0.0, planetR + eyeHeight, 0.0);
    // Engine world units are metres; feed camera translation into the cloud
    // volume in kilometres for real parallax while keeping the atmosphere
    // itself an effectively infinite background.
    vec3 cameraPositionKm = sky.invView[3].xyz * 0.001;
    vec3 cloudOrigin = atmosphereOrigin + cameraPositionKm;

    const vec3 betaR = vec3(0.0058, 0.0135, 0.0331); // per km, standard Rayleigh scattering coefficients
    const float betaM = 0.021;                        // per km, Mie extinction coefficient

    vec3 color = ComputeAtmosphere(atmosphereOrigin, rayDir, sunDir, planetR, planetR + atmH,
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

    vec4 clouds = RaymarchClouds(cloudOrigin, rayDir, sunDir, planetR,
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
