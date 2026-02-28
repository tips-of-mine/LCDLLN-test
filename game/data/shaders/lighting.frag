#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uAlbedo;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uORM;
layout(binding = 3) uniform sampler2D uDepth;

layout(binding = 4) uniform LightingUBO {
    mat4 invViewProj;
    vec3 cameraPos;
    float _pad0;
    vec3 lightDir;
    float _pad1;
    vec3 lightColor;
    float _pad2;
    vec3 ambient;
    float _pad3;
} ubo;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float Pow2(float x) { return x * x; }

float GGX_D(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return (denom > 0.0) ? (a2 / denom) : 0.0;
}

float Schlick_F(float VdotH, float F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float Smith_G1(float NdotV, float k) {
    return (NdotV > 0.0) ? (NdotV / (NdotV * (1.0 - k) + k)) : 0.0;
}

float Smith_G(float NdotL, float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return Smith_G1(NdotL, k) * Smith_G1(NdotV, k);
}

void main() {
    vec4 albedoRgb = texture(uAlbedo, vUV);
    vec3 albedo = albedoRgb.rgb;
    vec3 N = normalize(texture(uNormal, vUV).rgb * 2.0 - 1.0);
    vec3 orm = texture(uORM, vUV).rgb;
    float occlusion = orm.r;
    float roughness = max(orm.g, 0.04);
    float metallic = orm.b;
    float depth = texture(uDepth, vUV).r;

    vec4 ndc = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = ubo.invViewProj * ndc;
    vec3 pos = worldPos.xyz / worldPos.w;

    vec3 V = normalize(ubo.cameraPos - pos);
    vec3 L = normalize(-ubo.lightDir);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float F0 = mix(0.04, albedo.r, metallic);
    float D = GGX_D(NdotH, roughness);
    float F = Schlick_F(VdotH, F0);
    float G = Smith_G(NdotL, NdotV, roughness);

    vec3 specular = (D * F * G) / max(4.0 * NdotL * NdotV, 0.001) * ubo.lightColor * NdotL;
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI * ubo.lightColor * NdotL;

    vec3 Lo = (diffuse + specular) * occlusion + ubo.ambient * albedo * occlusion;
    outColor = vec4(Lo, 1.0);
}
