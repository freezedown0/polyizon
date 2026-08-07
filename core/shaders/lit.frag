#version 450

#define PI 3.14159265359

layout(set = 0, binding = 0) uniform LitUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirectionAndAmbient;
    vec4 directionalLightColorAndIntensity;
    ivec4 renderFlags;
} ubo;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

struct LocalLight {
    vec4 positionAndRange;
    vec4 colorAndIntensity;
    vec4 directionAndInnerCos;
    vec4 outerCosAndType;
};

layout(std430, set = 0, binding = 2) readonly buffer ForwardPlusLights {
    ivec4 counts;
    LocalLight lights[];
} localLights;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColor;
    vec4 materialParams; // x metallic, y roughness
    vec4 emissiveColorAndIntensity;
} pc;

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec4 vLightSpacePos;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

float DistributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(normal, halfway), 0.0);
    float denominator = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
    return GeometrySchlickGGX(max(dot(normal, viewDir), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(normal, lightDir), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluatePbrLight(vec3 normal, vec3 viewDir, vec3 lightDir, vec3 radiance,
                      vec3 albedo, float metallic, float roughness) {
    float ndotl = max(dot(normal, lightDir), 0.0);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }

    vec3 halfway = normalize(viewDir + lightDir);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(max(dot(halfway, viewDir), 0.0), f0);
    float distribution = DistributionGGX(normal, halfway, roughness);
    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);
    vec3 specular = (distribution * geometry * fresnel) /
        max(4.0 * max(dot(normal, viewDir), 0.0) * ndotl, 0.0001);

    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return (diffuseWeight * albedo / PI + specular) * radiance * ndotl;
}

float SampleShadow(vec4 lightSpacePos, vec3 normal, vec3 lightDir) {
    vec3 ndc = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (ndc.z <= 0.0 || ndc.z >= 1.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 1.0;
    }
    float bias = max(0.0015 * (1.0 - dot(normal, lightDir)), 0.00025);
    return texture(shadowMap, vec3(uv, ndc.z - bias));
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 cameraPosition = vec3(inverse(ubo.view)[3]);
    vec3 viewDir = normalize(cameraPosition - vWorldPos);
    vec3 albedo = max(pc.baseColor.rgb, vec3(0.0));
    float metallic = clamp(pc.materialParams.x, 0.0, 1.0);
    float roughness = clamp(pc.materialParams.y, 0.045, 1.0);

    vec3 direct = vec3(0.0);
    vec3 sunDir = normalize(ubo.sunDirectionAndAmbient.xyz);
    vec3 sunRadiance = ubo.directionalLightColorAndIntensity.rgb *
        ubo.directionalLightColorAndIntensity.a;
    float shadow = ubo.renderFlags.x != 0 ? SampleShadow(vLightSpacePos, normal, sunDir) : 1.0;
    direct += EvaluatePbrLight(normal, viewDir, sunDir, sunRadiance,
        albedo, metallic, roughness) * shadow;

    for (int i = 0; i < localLights.counts.x; ++i) {
        LocalLight light = localLights.lights[i];
        vec3 toLight = light.positionAndRange.xyz - vWorldPos;
        float distanceToLight = length(toLight);
        float range = light.positionAndRange.w;
        if (distanceToLight >= range) continue;
        vec3 lightDir = toLight / max(distanceToLight, 0.0001);
        float cone = 1.0;
        if (light.outerCosAndType.y > 0.5) {
            float innerCos = light.directionAndInnerCos.w;
            float outerCos = light.outerCosAndType.x;
            cone = clamp((dot(-lightDir, light.directionAndInnerCos.xyz) - outerCos) /
                max(innerCos - outerCos, 0.0001), 0.0, 1.0);
        }
        float rangeFade = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        float attenuation = cone * rangeFade * rangeFade /
            (1.0 + 0.1 * distanceToLight * distanceToLight);
        vec3 radiance = light.colorAndIntensity.rgb * light.colorAndIntensity.a * attenuation;
        direct += EvaluatePbrLight(normal, viewDir, lightDir, radiance,
            albedo, metallic, roughness);
    }

    vec3 ambient = albedo * ubo.sunDirectionAndAmbient.w * (1.0 - metallic);
    vec3 emissive = pc.emissiveColorAndIntensity.rgb * pc.emissiveColorAndIntensity.a;
    outColor = vec4(max(ambient + direct + emissive, vec3(0.0)), pc.baseColor.a);
}
