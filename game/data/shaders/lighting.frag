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
    mat4 view;
    mat4 lightViewProj[4];
    vec4 splitDepths;  // splitDepths[0..3] = cascade far planes (view-space)
    float shadowBiasConst;
    float shadowBiasSlope;
    float shadowEnabled;
    float shadowMapSize;  // 1/texel for PCF offset
    float prefilteredMipCount;  // M05.3: mip count for roughness -> lod
} ubo;

layout(binding = 5) uniform sampler2DShadow uShadowMap0;
layout(binding = 6) uniform sampler2DShadow uShadowMap1;
layout(binding = 7) uniform sampler2DShadow uShadowMap2;
layout(binding = 8) uniform sampler2DShadow uShadowMap3;

layout(binding = 9) uniform sampler2D uBrdfLut;  // M05.1: split-sum GGX LUT (NdotV, roughness) -> (scale, bias)
layout(binding = 10) uniform samplerCube uIrradianceMap;  // M05.2: diffuse irradiance cubemap (sample with N)
layout(binding = 11) uniform samplerCube uPrefilteredEnv;  // M05.3: specular prefiltered (sample with R, lod = roughness*(mipCount-1))
layout(binding = 12) uniform sampler2D uSsaoBlur;  // M06.4: SSAO_Blur (R), combine with AO ORM for ambient

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

// M04.3: cascade selection, project to light, bias, PCF 3x3. UV outside [0,1] -> shadow 1 (no occlusion).
float SampleShadow(int cascade, vec2 uv, float depthCompare) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    if (cascade == 0) return texture(uShadowMap0, vec3(uv, depthCompare));
    if (cascade == 1) return texture(uShadowMap1, vec3(uv, depthCompare));
    if (cascade == 2) return texture(uShadowMap2, vec3(uv, depthCompare));
    return texture(uShadowMap3, vec3(uv, depthCompare));
}

float PCF3x3(int cascade, vec2 uv, float depthCompare, vec2 texelSize) {
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            sum += SampleShadow(cascade, uv + vec2(float(x), float(y)) * texelSize, depthCompare);
        }
    }
    return sum / 9.0;
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

    float shadow = 1.0;
    if (ubo.shadowEnabled > 0.5) {
        vec4 viewPos = ubo.view * vec4(pos, 1.0);
        float viewSpaceDepth = -viewPos.z;
        int cascade = 0;
        if (viewSpaceDepth <= ubo.splitDepths.x) cascade = 0;
        else if (viewSpaceDepth <= ubo.splitDepths.y) cascade = 1;
        else if (viewSpaceDepth <= ubo.splitDepths.z) cascade = 2;
        else cascade = 3;
        vec4 lightClip = ubo.lightViewProj[cascade] * vec4(pos, 1.0);
        lightClip /= lightClip.w;
        vec2 shadowUV = lightClip.xy * 0.5 + 0.5;
        float depthInLight = lightClip.z;
        float slope = max(abs(dFdx(depthInLight)), abs(dFdy(depthInLight)));
        float depthCompare = depthInLight - ubo.shadowBiasConst - ubo.shadowBiasSlope * slope;
        vec2 texelSize = vec2(1.0 / ubo.shadowMapSize);
        shadow = PCF3x3(cascade, shadowUV, depthCompare, texelSize);
    }

    // M06.4: AO n'affecte pas la lumière directe
    vec3 directLight = (diffuse + specular) * shadow;

    // M06.4: AO_final = AO_ssao * AO_tex; ambient *= AO_final
    float ao_ssao = texture(uSsaoBlur, vUV).r;
    float AO_final = ao_ssao * occlusion;

    // M05.4 — IBL split-sum: kD = (1 - kS) * (1 - metallic), specIBL = prefiltered * (F*brdf.x + brdf.y)
    float F_ibl = Schlick_F(NdotV, F0);  // Fresnel at viewing angle for IBL
    float kS = F_ibl;
    float kD = (1.0 - kS) * (1.0 - metallic);
    vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = kD * irradiance * albedo * AO_final;
    vec3 R = reflect(-V, N);
    float lod = roughness * max(ubo.prefilteredMipCount - 1.0, 0.0);
    vec3 prefilteredColor = textureLod(uPrefilteredEnv, R, lod).rgb;
    vec3 specIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y) * AO_final;

    vec3 Lo = directLight + diffuseIBL + specIBL;
    outColor = vec4(Lo, 1.0);
}
