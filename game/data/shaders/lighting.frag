#version 450

// Deferred lighting pass – PBR metallic/roughness (UE4-like).
//
// Reads:
//   GBufferA (binding 0) – albedo, R8G8B8A8_SRGB  (hardware linearises on sample)
//   GBufferB (binding 1) – world normal, A2B10G10R10_UNORM, encoded as N*0.5+0.5
//   GBufferC (binding 2) – ORM, R8G8B8A8_UNORM: R=AO, G=Roughness, B=Metallic
//   Depth    (binding 3) – scene depth, D32_SFLOAT, sampled as float in .r
//   (M05.4)  binding 4 – irradiance cubemap, 5 – prefiltered specular cubemap, 6 – BRDF LUT
//
// Outputs:
//   outSceneColorHDR (location 0) – SceneColor_HDR, R16G16B16A16_SFLOAT
//
// Push constants (132 bytes, fragment stage):
//   mat4  invVP         – inverse view-projection matrix
//   vec4  cameraPos     – camera world-space position (xyz, w unused)
//   vec4  lightDir      – normalised direction *toward* the light (xyz, w unused)
//   vec4  lightColor    – RGB radiance (color * intensity, w unused)
//   vec4  ambientColor  – constant ambient RGB (fallback when useIBL == 0)
//   float useIBL        – 1.0 = use IBL, 0.0 = constant ambient

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outSceneColorHDR;

// ---- GBuffer samplers -------------------------------------------------------
layout(set = 0, binding = 0) uniform sampler2D gbufA;       // albedo (sRGB->linear)
layout(set = 0, binding = 1) uniform sampler2D gbufB;       // normal  [0,1] encoded
layout(set = 0, binding = 2) uniform sampler2D gbufC;       // ORM     [0,1]
layout(set = 0, binding = 3) uniform sampler2D depthTex;    // depth, .r = [0,1]
layout(set = 0, binding = 4) uniform samplerCube irradianceMap;  // M05.4 diffuse IBL
layout(set = 0, binding = 5) uniform samplerCube prefilterMap;  // M05.4 specular IBL
layout(set = 0, binding = 6) uniform sampler2D   brdfLut;       // M05.4 BRDF LUT (scale, bias)

// ---- Push constants ---------------------------------------------------------
layout(push_constant) uniform PC
{
    mat4  invVP;        // inverse view-projection (for world position reconstruction)
    vec4  cameraPos;    // camera world-space position (xyz, w unused)
    vec4  lightDir;     // normalised direction toward the directional light (xyz)
    vec4  lightColor;   // RGB radiance (xyz = color * intensity)
    vec4  ambientColor; // constant ambient RGB (fallback when useIBL == 0)
    float useIBL;       // 1.0 = use IBL, 0.0 = constant ambient
} pc;

// ---- Constants --------------------------------------------------------------
const float PI       = 3.14159265358979323846;
const float kMinRgh  = 0.04; // prevent mirror-flat specular artefacts
const float kDielF0  = 0.04; // base reflectance for non-metals

// ---- PBR BRDF functions (UE4-style) ----------------------------------------

/// GGX Normal Distribution Function.
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

/// Schlick-GGX geometry term for a single direction.
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-7);
}

/// Smith geometry function (view + light).
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

/// Fresnel-Schlick approximation.
vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

// ---- Main -------------------------------------------------------------------
void main()
{
    // ---- Sample GBuffer ------------------------------------------------
    vec3  albedo    = texture(gbufA, inUV).rgb;              // linear albedo
    vec3  gbufBsamp = texture(gbufB, inUV).rgb;
    vec3  normalW   = normalize(gbufBsamp * 2.0 - 1.0);     // decode [0,1]->[-1,1]
    vec4  orm       = texture(gbufC, inUV);
    float ao        = orm.r;
    float roughness = max(orm.g, kMinRgh);
    float metallic  = orm.b;
    float depth     = texture(depthTex, inUV).r;

    // ---- Skip sky / empty fragments (depth == 1.0 means no geometry) ---
    if (depth >= 1.0)
    {
        outSceneColorHDR = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ---- Reconstruct world-space position from depth -------------------
    // NDC xy in [-1,+1]; depth in [0,1] (Vulkan convention).
    vec2  ndcXY  = inUV * 2.0 - 1.0;
    vec4  posClip  = vec4(ndcXY, depth, 1.0);
    vec4  posWorld = pc.invVP * posClip;
    posWorld      /= posWorld.w;
    vec3  P        = posWorld.xyz;

    // ---- Shading vectors -----------------------------------------------
    vec3  V  = normalize(pc.cameraPos.xyz - P);   // view direction
    vec3  L  = normalize(pc.lightDir.xyz);         // toward light
    vec3  H  = normalize(V + L);                   // half vector

    float NdotL = max(dot(normalW, L), 0.0);
    float NdotV = max(dot(normalW, V), 0.0001);   // avoid division by zero
    float NdotH = max(dot(normalW, H), 0.0);
    float VdotH = max(dot(V,       H), 0.0);

    // ---- Cook-Torrance specular BRDF -----------------------------------
    // F0: interpolate between dielectric base (0.04) and metallic (albedo-tinted).
    vec3  F0 = mix(vec3(kDielF0), albedo, metallic);

    float D  = D_GGX(NdotH, roughness);
    float G  = G_Smith(NdotV, NdotL, roughness);
    vec3  F  = F_Schlick(VdotH, F0);

    // kD = (1 - kS) * (1 - metallic). M05.4 spec.
    vec3  kS = F;
    vec3  kD = (1.0 - kS) * (1.0 - metallic);

    // Specular lobe: DGF / (4 * NdotV * NdotL).
    vec3  specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-7);

    // Diffuse lobe: Lambertian.
    vec3  diffuse  = kD * albedo / PI;

    // ---- Direct lighting contribution ----------------------------------
    vec3  Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL;

    // ---- Ambient: IBL (split-sum diffuse + spec) or fallback constant -------
    vec3  ambient;
    if (pc.useIBL > 0.5)
    {
        // Diffuse IBL: irradiance along N.
        vec3  diffuseIBL = texture(irradianceMap, normalW).rgb * albedo * kD;

        // Specular IBL: prefiltered(R, lod) * (F * brdf.x + brdf.y).
        vec3  R = reflect(-V, normalW);
        float lod = roughness * float(textureQueryLevels(prefilterMap) - 1);
        vec3  prefiltered = textureLod(prefilterMap, R, lod).rgb;
        vec2  brdfSample  = texture(brdfLut, vec2(NdotV, roughness)).rg;
        vec3  specIBL     = prefiltered * (F * brdfSample.x + brdfSample.y);

        ambient = (diffuseIBL + specIBL) * ao;
    }
    else
    {
        ambient = pc.ambientColor.rgb * albedo * ao;
    }

    // ---- Combine & output HDR ------------------------------------------
    vec3  color = ambient + Lo;
    outSceneColorHDR = vec4(color, 1.0);
}
