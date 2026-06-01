#version 450

// M45.3 — Profondeur de champ / bokeh, version v1.
//
// Passe GRAPHIQUE plein écran (fullscreen triangle, comme volumetric_fog.frag).
// S'exécute sur l'image HDR APRÈS bloom et AVANT tonemap (sur HDR pour des
// highlights bokeh propres). Pour CHAQUE pixel on reconstruit la position monde
// depuis le depth, on calcule un Circle of Confusion (CoC) depuis la distance au
// plan focal, puis on fait un flou en disque (16 samples fixes) pondéré par le
// CoC. Un sample n'est mélangé QUE si son propre CoC le place dans le flou, pour
// éviter le bleeding du flou à travers une arête nette de premier plan.
//
// Entrées (descriptor set 0) :
//   binding 0 = scene color HDR post-bloom (sampler LINÉAIRE clamp)
//   binding 1 = depth scene (D32_SFLOAT, sampler NEAREST clamp, lu en .r)
//
// Sortie :
//   outColor (location 0) — scene color HDR floutée.
//
// Push constants (112 octets, stage fragment) — cf. DofParams (DepthOfFieldPass.h) :
//   mat4 invVP      — inverse view-projection (reconstruction world pos depuis depth)
//   vec4 cameraPos  — xyz = position caméra ; w = distance focale (m)
//   vec4 dofParams  — x = plage de netteté (m, demi-largeur ; <=0 désactive = passthrough),
//                     y = rayon de flou max (px), z = échelle flou near, w = échelle flou far
//   vec4 texelSize  — x = 1/width, y = 1/height, z/w = padding

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// ---- Samplers ---------------------------------------------------------------
layout(set = 0, binding = 0) uniform sampler2D colorTex; // HDR post-bloom
layout(set = 0, binding = 1) uniform sampler2D depthTex; // depth scene, .r = [0,1]

// ---- Push constants ---------------------------------------------------------
layout(push_constant) uniform PC
{
    mat4 invVP;      // inverse view-projection
    vec4 cameraPos;  // xyz = caméra ; w = distance focale (m)
    vec4 dofParams;  // x = focusRange (m), y = maxBlurPx, z = nearScale, w = farScale
    vec4 texelSize;  // x = 1/w, y = 1/h, z/w = padding
} pc;

// 16 offsets fixes en disque (spirale de Fermat / anneaux), rayon normalisé [0,1].
// Hardcodés pour éviter toute boucle dépendante de données non bornée.
const vec2 kDiskOffsets[16] = vec2[16](
    vec2( 0.0000,  0.0000),
    vec2( 0.5400,  0.0000),
    vec2( 0.1670,  0.5130),
    vec2(-0.4360,  0.3170),
    vec2(-0.4360, -0.3170),
    vec2( 0.1670, -0.5130),
    vec2( 0.7600,  0.4400),
    vec2(-0.0000,  0.8800),
    vec2(-0.7600,  0.4400),
    vec2(-0.7600, -0.4400),
    vec2( 0.0000, -0.8800),
    vec2( 0.7600, -0.4400),
    vec2( 1.0000,  0.0000),
    vec2(-0.5000,  0.8660),
    vec2(-0.5000, -0.8660),
    vec2( 0.3090,  0.9510)
);

// Reconstruit la distance caméra->surface (en mètres) pour un UV donné.
// \param uv         Coordonnée d'échantillonnage de l'image.
// \param isSky      out : true si le pixel est le ciel (depth >= 1.0).
// \return Distance monde en mètres ; pour le ciel, renvoie une grande valeur
//         (flou far max, le ciel étant à l'infini).
float sampleDistance(vec2 uv, out bool isSky)
{
    float depth = texture(depthTex, uv).r;
    if (depth >= 1.0)
    {
        isSky = true;
        return 1.0e6; // très loin : CoC -> far max
    }
    isSky = false;
    vec2 ndcXY    = uv * 2.0 - 1.0;
    vec4 posClip  = vec4(ndcXY, depth, 1.0);
    vec4 posWorld = pc.invVP * posClip;
    posWorld     /= posWorld.w;
    return length(posWorld.xyz - pc.cameraPos.xyz);
}

// Calcule le rayon de flou (en pixels) à partir d'une distance monde.
// \param dist Distance caméra->surface (m).
// \return Rayon de flou en pixels, dans [0, maxBlurPx].
float blurRadiusFromDist(float dist)
{
    float focus      = pc.cameraPos.w;
    float focusRange = pc.dofParams.x;
    float maxBlurPx  = pc.dofParams.y;
    // CoC signé : négatif = near (devant le plan focal), positif = far.
    float coc = (dist - focus) / max(focusRange, 1e-4);
    coc = clamp(coc, -1.0, 1.0);
    float blurPx;
    if (coc < 0.0)
        blurPx = min(-coc * pc.dofParams.z, 1.0) * maxBlurPx;
    else
        blurPx = min( coc * pc.dofParams.w, 1.0) * maxBlurPx;
    return blurPx;
}

void main()
{
    // ---- (1) Couleur centrale ------------------------------------------
    vec3 centerColor = texture(colorTex, inUV).rgb;

    // ---- (2) Gating runtime : focusRange <= 0 -> passthrough -----------
    float focusRange = pc.dofParams.x;
    if (focusRange <= 0.0)
    {
        outColor = vec4(centerColor, 1.0);
        return;
    }

    // ---- (3)/(4) CoC du pixel central ----------------------------------
    bool centerSky;
    float centerDist = sampleDistance(inUV, centerSky);
    float blurPx = blurRadiusFromDist(centerDist);

    // Trop peu de flou -> passthrough (évite un coût inutile + scintillement).
    if (blurPx < 0.5)
    {
        outColor = vec4(centerColor, 1.0);
        return;
    }

    // ---- (5) Flou en disque pondéré par le CoC des samples -------------
    // On ne mélange un sample QUE si son PROPRE CoC le place dans le flou,
    // pour éviter le bleeding d'un fond flou par-dessus un avant-plan net.
    vec2 radiusUV = vec2(blurPx) * pc.texelSize.xy; // rayon en UV
    vec3  accumColor  = vec3(0.0);
    float accumWeight = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        vec2 sampleUV = inUV + kDiskOffsets[i] * radiusUV;
        bool sampleSky;
        float sampleDist = sampleDistance(sampleUV, sampleSky);
        float sampleBlur = blurRadiusFromDist(sampleDist);
        // Poids : 1 si le sample est lui-même flou au moins autant que la
        // distance qui le sépare du centre (en pixels), sinon 0. Cela écarte
        // les samples nets d'avant-plan qui ne devraient pas baver dans le flou.
        float sampleOffsetPx = length(kDiskOffsets[i]) * blurPx;
        float w = (sampleBlur >= sampleOffsetPx - 0.5) ? 1.0 : 0.0;
        accumColor  += texture(colorTex, sampleUV).rgb * w;
        accumWeight += w;
    }

    vec3 blurredColor = (accumWeight > 0.0) ? (accumColor / accumWeight) : centerColor;

    // ---- (6) Sortie ----------------------------------------------------
    outColor = vec4(blurredColor, 1.0);
}
