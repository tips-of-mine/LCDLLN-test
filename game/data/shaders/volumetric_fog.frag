#version 450

// M45.2 — Brouillard volumique + god rays (rayons crépusculaires), version v1.
//
// Passe GRAPHIQUE plein écran (fullscreen triangle, comme lighting.frag). Pour
// CHAQUE pixel on ray-marche depuis la caméra vers la position monde du pixel,
// en échantillonnant UNE cascade de shadow map (cascade 0) pour décider si chaque
// point du rayon est éclairé par le soleil → god rays. Pas de volume froxel 3D,
// pas de compute (v1 conservatrice).
//
// Entrées (descriptor set 0) :
//   binding 0 = scene color HDR post-water (sampler linéaire clamp)
//   binding 1 = depth scene (D32_SFLOAT, sampler nearest clamp, lu en .r)
//   binding 2 = shadow map cascade 0 (image DEPTH, sampler nearest clamp, lue en .r)
//
// Sortie :
//   outColor (location 0) — scene color HDR brouillardée.
//
// Push constants (192 octets, stage fragment) — cf. FogParams (VolumetricFogPass.h) :
//   mat4 invVP        — inverse view-projection (reconstruction world pos depuis depth)
//   mat4 sunViewProj  — lightViewProj de la cascade 0 (transforme world -> clip ombre)
//   vec4 cameraPos    — xyz = position caméra ; w = densité fog (<= 0 désactive)
//   vec4 sunDir       — xyz = direction VERS le soleil (normalisée) ; w = anisotropie HG g
//   vec4 sunColor     — xyz = couleur soleil ; w = intensité inscattering
//   vec4 fogParams    — x = nb de steps, y = distance max de marche (m),
//                       z = hauteur de référence (m), w = atténuation en hauteur

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// ---- Samplers ---------------------------------------------------------------
layout(set = 0, binding = 0) uniform sampler2D sceneColorTex; // HDR post-water
layout(set = 0, binding = 1) uniform sampler2D depthTex;      // depth scene, .r = [0,1]
layout(set = 0, binding = 2) uniform sampler2D shadowTex;     // shadow cascade 0, .r = depth

// ---- Push constants ---------------------------------------------------------
layout(push_constant) uniform PC
{
    mat4 invVP;        // inverse view-projection
    mat4 sunViewProj;  // lightViewProj cascade 0
    vec4 cameraPos;    // xyz = caméra ; w = densité fog
    vec4 sunDir;       // xyz = dir vers le soleil ; w = anisotropie HG g
    vec4 sunColor;     // xyz = couleur soleil ; w = intensité inscattering
    vec4 fogParams;    // x = steps, y = maxDist (m), z = height (m), w = heightFalloff
} pc;

const float PI = 3.14159265358979;

void main()
{
    // ---- (1) Couleur de scène en entrée --------------------------------
    vec3 sceneColor = texture(sceneColorTex, inUV).rgb;

    // ---- (2) Gating runtime : densité <= 0 -> passthrough --------------
    float density = pc.cameraPos.w;
    if (density <= 0.0)
    {
        outColor = vec4(sceneColor, 1.0);
        return;
    }

    // ---- (3) Reconstruction de la position monde P du pixel ------------
    // NDC xy in [-1,+1] ; depth in [0,1] (convention Vulkan).
    float depth = texture(depthTex, inUV).r;
    vec2  ndcXY = inUV * 2.0 - 1.0;
    vec4  posClip  = vec4(ndcXY, depth, 1.0);
    vec4  posWorld = pc.invVP * posClip;
    posWorld      /= posWorld.w;
    vec3  P        = posWorld.xyz;

    vec3  cam = pc.cameraPos.xyz;

    // Distance de marche : si le pixel est du ciel (depth >= 1.0), on marche
    // quand même jusqu'à fogParams.y (brume d'horizon). Sinon on s'arrête à la
    // géométrie, borné à la distance max.
    float maxDist = max(pc.fogParams.y, 0.0);
    float marchDist;
    if (depth >= 1.0)
        marchDist = maxDist;
    else
        marchDist = min(length(P - cam), maxDist);

    // Direction du rayon caméra -> pixel (pour la phase + la marche).
    // Si P ~ cam (cas dégénéré), on garde une direction sûre.
    vec3 toP = P - cam;
    float toPLen = length(toP);
    vec3 rayDir = (toPLen > 1e-4) ? (toP / toPLen) : vec3(0.0, 0.0, 1.0);

    // ---- (4) Ray-march -------------------------------------------------
    int   steps   = int(max(pc.fogParams.x, 1.0));
    float stepLen = marchDist / float(steps);

    float g     = clamp(pc.sunDir.w, -0.99, 0.99);
    vec3  sunL  = normalize(pc.sunDir.xyz);
    float cosA  = dot(rayDir, sunL);
    // Phase de Henyey-Greenstein (anisotropie de la diffusion vers le soleil).
    float denom = max(1.0 + g * g - 2.0 * g * cosA, 1e-4);
    float phase = (1.0 - g * g) / (4.0 * PI * pow(denom, 1.5));

    float heightRef     = pc.fogParams.z;
    float heightFalloff = pc.fogParams.w;
    const float bias    = 0.0015; // biais d'ombre (anti acné)

    float scatter = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        // Position du sample (milieu du segment pour réduire le biais).
        float t   = (float(i) + 0.5) * stepLen;
        vec3  sp  = cam + rayDir * t;

        // Atténuation en hauteur : fog plus dense en bas.
        float heightFactor = exp(-max(sp.y - heightRef, 0.0) * heightFalloff);

        // Test d'ombre soleil : projette sp dans l'espace de la shadow map.
        float lit = 1.0;
        vec4  shadowClip = pc.sunViewProj * vec4(sp, 1.0);
        if (shadowClip.w > 0.0)
        {
            vec3 shadowNdc = shadowClip.xyz / shadowClip.w;
            // xy de NDC [-1,1] vers UV [0,1] ; z (depth Vulkan) déjà dans [0,1].
            vec2  shadowUV    = shadowNdc.xy * 0.5 + 0.5;
            float sampleDepth = shadowNdc.z;
            if (shadowUV.x >= 0.0 && shadowUV.x <= 1.0 &&
                shadowUV.y >= 0.0 && shadowUV.y <= 1.0 &&
                sampleDepth >= 0.0 && sampleDepth <= 1.0)
            {
                float shadowDepth = texture(shadowTex, shadowUV).r;
                lit = (shadowDepth >= sampleDepth - bias) ? 1.0 : 0.0;
            }
            // Hors de la shadow map -> considéré éclairé (lit reste 1.0).
        }

        scatter += lit * heightFactor * phase * stepLen * density;
    }

    // ---- (5) Composition finale ----------------------------------------
    scatter = clamp(scatter * pc.sunColor.w, 0.0, 1.0);
    vec3 fogColor = pc.sunColor.rgb;
    outColor = vec4(mix(sceneColor, fogColor, scatter), 1.0);
}
