#version 450

// Deferred lighting pass – PBR metallic/roughness (UE4-like).
//
// Reads:
//   GBufferA (binding 0) – albedo, R8G8B8A8_SRGB  (hardware linearises on sample)
//   GBufferB (binding 1) – world normal, A2B10G10R10_UNORM, encoded as N*0.5+0.5
//   GBufferC (binding 2) – ORM, R8G8B8A8_UNORM: R=AO, G=Roughness, B=Metallic
//   Depth    (binding 3) – scene depth, D32_SFLOAT, sampled as float in .r
//   (M05.4)  binding 4 – irradiance cubemap, 5 – prefiltered specular cubemap, 6 – BRDF LUT
//   (M06.4)  binding 7 – SSAO_Blur (R16F, .r = screen-space AO)
//   (M17.3)  binding 8 – DecalOverlay (RGBA, rgb = decal albedo, a = blend)
//   (M45.7)  binding 9 – DDGI irradiance atlas (RGBA16F). Lu UNIQUEMENT si
//                        useDdgi > 0.5 ; sinon binding lié à un fallback valide
//                        mais jamais échantillonné (rendu byte-identique).
//
// Outputs:
//   outSceneColorHDR (location 0) – SceneColor_HDR, R16G16B16A16_SFLOAT
//
// Push constants (224 bytes, fragment stage ; +80 o DDGI/pad ajoutés en M45.7):
//   mat4  invVP         – inverse view-projection matrix
//   vec4  cameraPos     – camera world-space position (xyz, w unused)
//   vec4  lightDir      – normalised direction *toward* the light (xyz, w unused)
//   vec4  lightColor    – RGB radiance (color * intensity, w unused)
//   vec4  ambientColor  – constant ambient RGB (fallback when useIBL == 0)
//   vec4  skyColor      – RGB couleur du ciel pour les pixels sans géométrie
//                         (depth == 1.0). Pilotée par DayNightCycle
//                         (skyHorizon) côté CPU. w = 1.0 si SkyPass est
//                         ready (alors on lit GBufferA pour la sky), 0.0
//                         si on utilise la flat skyColor.
//   float useIBL        – 1.0 = use IBL, 0.0 = constant ambient
//   float useDdgi       – M45.7 : 1.0 = ajoute l'irradiance DDGI dynamique, 0.0 = inchangé
//   vec3  pad0..2       – padding (alignement vec4)
//   vec4  ddgiOrigin    – xyz origine monde grille (m)
//   vec4  ddgiSpacing   – xyz espacement par axe (m)
//   vec4  ddgiCounts    – xyz nb sondes par axe ; w = irradianceTexels
//   vec4  ddgiAtlas     – x atlasCols, y atlasRows, z tileSize, w intensity
//
// M45.1 — Perspective aérienne (upgrade du brouillard de distance) :
//   lightColor.w = aerialDensity   – densité d'extinction (1/m). <= 0 désactive.
//   ambientColor.w = aerialInscatter – force du halo directionnel vers le soleil.
//   fogStart (cameraPos.w) = rayon clair ; fogEnd (lightDir.w) = distance de
//   fondu complet (garantit le masquage de la coupe de distance des props).

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
layout(set = 0, binding = 7) uniform sampler2D   ssaoTex;       // M06.4 SSAO_Blur (R16F, .r = AO)
layout(set = 0, binding = 8) uniform sampler2D   decalOverlayTex;
layout(set = 0, binding = 9) uniform sampler2D   ddgiIrradiance; // M45.7 atlas irradiance DDGI (RGBA16F)

// ---- CSM — Ombres cascades --------------------------------------------------
// binding 10 : 4 shadow maps (depth Vulkan [0,1], sampler nearest clamp, compare
//   manuel comme volumetric_fog.frag — PAS de sampler2DShadow).
// binding 11 : UBO cascades (std140). shadowParams : x=useShadows, y=texelSize
//   (1/résolution), z=biasConstant, w=biasSlopeMax.
layout(set = 0, binding = 10) uniform sampler2D uShadowMaps[4];
layout(set = 0, binding = 11) uniform ShadowUbo
{
    mat4 lightViewProj[4];
    vec4 shadowParams;
} uShadow;

// ---- Lumières ponctuelles (point lights) ------------------------------------
// binding 12 : UBO std140. count.x = nb actives [0..64] ; lights[i].posRadius
// (xyz=position monde m, w=rayon m) ; lights[i].colorIntensity (rgb=couleur
// linéaire, a=intensité×intensity_scale). Pas d'ombre (v1).
struct PointLightStd140
{
    vec4 posRadius;
    vec4 colorIntensity;
};
layout(set = 0, binding = 12) uniform PointLightUbo
{
    uvec4 count;
    PointLightStd140 lights[64];
} uPoint;

// ---- Push constants ---------------------------------------------------------
layout(push_constant) uniform PC
{
    mat4  invVP;        // inverse view-projection (for world position reconstruction)
    vec4  cameraPos;    // xyz = camera world-space position ; w = aerial fogStart (m)
    vec4  lightDir;     // xyz = normalised direction toward the sun ; w = aerial fogEnd (m)
    vec4  lightColor;   // xyz = RGB radiance (color * intensity) ; w = aerialDensity (1/m)
    vec4  ambientColor; // xyz = constant ambient RGB (fallback) ; w = aerialInscatter
    vec4  skyColor;     // rgb = couleur du ciel flat (fallback) ; w = 1.0 si SkyPass ready
    float useIBL;       // 1.0 = use IBL, 0.0 = constant ambient
    // --- M45.7 — DDGI dynamique (ADDITIF). useDdgi=0 (défaut) => aucun changement.
    // useIBL+useDdgi+pad0+pad1 = 16 o pour aligner les vec4 DDGI sur 16 (std140). ---
    float useDdgi;      // 1.0 = ajoute l'irradiance DDGI dynamique, 0.0 = inchangé
    float aerialSkyModel; // 1.0 = teinte aerial = ciel ANALYTIQUE évalué par rayon (chantier 2026-07-20) ; 0.0 = skyColor legacy (ex-pad0)
    float pad1;         // padding (alignement vec4)
    vec4  ddgiOrigin;   // xyz = origine monde grille (mètres) ; w inutilisé
    vec4  ddgiSpacing;  // xyz = espacement par axe (mètres) ; w inutilisé
    vec4  ddgiCounts;   // xyz = nb sondes par axe ; w = irradianceTexels (côté hors bordure)
    vec4  ddgiAtlas;    // x = atlasCols, y = atlasRows, z = tileSize (texels+2), w = intensity
} pc;

// M45.7 — Encodage octaédrique direction -> (u,v) ∈ [0,1]². Inverse de
// OctaToDir (tools/impostor_builder/OctahedralMap.h) : MÊME convention que la
// passe d'écriture ddgi_update.comp, sinon l'échantillonnage serait décalé.
vec2 ddgiDirToOcta(vec3 dir)
{
    vec3 n = dir / max(abs(dir.x) + abs(dir.y) + abs(dir.z), 1e-8);
    vec2 uv;
    if (n.z >= 0.0)
    {
        uv = n.xy;
    }
    else
    {
        // Repli de l'hémisphère inférieur (symétrique d'OctaToDir).
        uv = vec2((1.0 - abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0),
                  (1.0 - abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0));
    }
    return uv * 0.5 + 0.5; // [-1,1] -> [0,1]
}

// M45.7 — Échantillonne l'irradiance DDGI d'UNE sonde, donnée par ses indices
// de grille entiers (gridIdx = ix,iy,iz en float), selon la normale N.
// Déplie en (col,row) selon le packing M45.6 (col = ix + iz*counts.x, row = iy),
// encode N en octaédrique -> texel intérieur dans la tuile, échantillonne l'atlas.
// (Logique factorisée depuis l'ancien ddgiSampleNearest — packing inchangé.)
vec3 ddgiSampleProbe(vec3 gridIdx, vec3 N)
{
    vec3 counts = max(pc.ddgiCounts.xyz, vec3(1.0));
    vec3 gi = clamp(gridIdx, vec3(0.0), counts - 1.0);

    float atlasCols = max(pc.ddgiAtlas.x, 1.0);
    float atlasRows = max(pc.ddgiAtlas.y, 1.0);
    float tileSize  = max(pc.ddgiAtlas.z, 3.0);
    float texels    = max(pc.ddgiCounts.w, 1.0); // côté octaédrique hors bordure

    // Déplie l'indice de grille en (col,row) selon le packing M45.6.
    float col = gi.x + gi.z * counts.x;
    float row = gi.y;

    // Octa (u,v) sur N -> texel intérieur [1, tileSize-2]. On borne dans
    // [1, texels] (la bordure 1px n'est pas mise à jour par ddgi_update.comp en v1).
    vec2 octa  = ddgiDirToOcta(normalize(N));
    vec2 local = clamp(vec2(1.0) + octa * texels, vec2(1.0), vec2(texels)); // décalage bordure 1px

    // Coord pixel (centre du texel) dans l'atlas -> UV [0,1].
    vec2 pixel = vec2(col * tileSize, row * tileSize) + local;
    vec2 uv = (pixel + 0.5) / vec2(atlasCols * tileSize, atlasRows * tileSize);

    return texture(ddgiIrradiance, uv).rgb;
}

// M45.7b — Échantillonne l'irradiance DDGI pour la position monde P selon la
// normale N avec INTERPOLATION TRILINÉAIRE sur les 8 sondes englobantes,
// pondérée par la normale (anti-leak doux, sans Chebyshev).
//
// DDGI APPROCHÉE : il n'y a PAS de terme de visibilité/profondeur par sonde
// (Chebyshev) car cela exigerait du ray-tracing ou un rendu de profondeur par
// sonde, non disponible ici. La pondération normale ci-dessous (favorise les
// sondes « devant » la surface) atténue le light-leak mais ne le supprime pas.
vec3 ddgiSampleTrilinear(vec3 P, vec3 N)
{
    vec3 counts = max(pc.ddgiCounts.xyz, vec3(1.0));
    // Coords continues dans la grille.
    vec3 gf   = (P - pc.ddgiOrigin.xyz) / max(pc.ddgiSpacing.xyz, vec3(1e-4));
    vec3 base = floor(gf);
    vec3 frac = clamp(gf - base, 0.0, 1.0);

    vec3  accumIrr = vec3(0.0);
    float accumW   = 0.0;

    // Parcours des 8 coins o ∈ {0,1}^3 du cube de sondes englobant.
    for (int i = 0; i < 8; ++i)
    {
        vec3 o = vec3(float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1));
        vec3 gi = clamp(base + o, vec3(0.0), counts - 1.0);

        // Poids trilinéaire : produit par axe de frac (si o=1) ou 1-frac (si o=0).
        vec3  tw  = mix(vec3(1.0) - frac, frac, o);
        float wTri = tw.x * tw.y * tw.z;

        // Pondération par la normale : favorise les sondes situées « devant » la
        // surface (anti-leak doux). Poids ∈ [0.05, 1.05].
        vec3  probePos   = pc.ddgiOrigin.xyz + gi * pc.ddgiSpacing.xyz;
        vec3  dirToProbe = normalize(probePos - P);
        float wN = max(dot(dirToProbe, N), 0.0);
        wN = wN * 0.5 + 0.5;
        wN = wN * wN + 0.05;

        float w = wTri * wN;
        accumIrr += ddgiSampleProbe(gi, N) * w;
        accumW   += w;
    }

    // Repli sur la sonde la plus proche si tous les poids sont négligeables.
    return (accumW > 1e-5)
        ? accumIrr / accumW
        : ddgiSampleProbe(clamp(floor(gf + 0.5), vec3(0.0), counts - 1.0), N);
}

// M45.1 — exposant d'anisotropie du halo directionnel d'inscattering vers le
// soleil. Plus élevé = halo plus concentré autour du disque solaire. Constante
// (pas de slot push-constant libre) ; ajuster ici si besoin de réglage visuel.
const float kAerialSunExponent = 8.0;

// Chantier perspective aérienne 2026-07-20 — mini-évaluation du ciel
// ANALYTIQUE (mêmes constantes physiques que sky.frag, marche réduite
// 4 échantillons vue × 2 vers le soleil au lieu de 8×4) pour la TEINTE de la
// perspective aérienne : le terrain lointain converge vers la couleur du ciel
// analytique dans SA direction (chaud côté soleil au couchant, froid à
// l'opposé, bleuté le jour) au lieu de la couleur d'horizon legacy uniforme —
// qui jurait avec le fond analytique à l'horizon. Coût : uniquement les
// pixels de géométrie dont le facteur de brume est significatif.
const float kAerialPi      = 3.14159265358979;       // PI global déclaré plus bas
const float kAerialPlanetR = 6360.0e3;               // rayon planète (m)
const float kAerialAtmoR   = 6420.0e3;               // sommet de l'atmosphère (m)
const vec3  kAerialBetaR   = vec3(5.8e-6, 13.5e-6, 33.1e-6); // diffusion Rayleigh
const float kAerialBetaM   = 21.0e-6;                // diffusion Mie
const float kAerialHR      = 8000.0;                 // hauteur d'échelle Rayleigh (m)
const float kAerialHM      = 1200.0;                 // hauteur d'échelle Mie (m)

// Distance de o (relatif au centre planète) à la sphère de rayon r le long
// de d ; négatif si pas d'intersection avant (copie de sky.frag).
float AerialRaySphereFar(vec3 o, vec3 d, float r)
{
    float b = dot(o, d);
    float c = dot(o, o) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}

// Couleur du ciel analytique juste au-dessus de l'horizon dans la direction
// azimutale de \p viewDir (composante Y clampée vers l'horizon : regarder le
// sol n'a pas de sens atmosphérique — la teinte de convergence du terrain
// lointain est celle du ciel derrière lui). Sortie en radiance HDR, même
// échelle que le ciel écrit par SkyPass (kSunIntensity identique).
vec3 AerialAnalyticHorizon(vec3 viewDir, vec3 sunDir)
{
    vec3 o = vec3(0.0, kAerialPlanetR + 200.0, 0.0);
    vec3 d = normalize(vec3(viewDir.x, clamp(viewDir.y, 0.015, 0.25), viewDir.z));
    float tMax = AerialRaySphereFar(o, d, kAerialAtmoR);
    if (tMax <= 0.0) return vec3(0.0015, 0.002, 0.0045);

    float mu = dot(d, sunDir);
    float phaseR = 3.0 / (16.0 * kAerialPi) * (1.0 + mu * mu);
    const float g = 0.76; // anisotropie Mie (halo autour du soleil)
    float phaseM = 3.0 / (8.0 * kAerialPi) * ((1.0 - g * g) * (1.0 + mu * mu))
        / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

    const int kSteps      = 4;
    const int kLightSteps = 2;
    float dt = tMax / float(kSteps);
    float t  = 0.5 * dt;
    vec3  sumR = vec3(0.0);
    vec3  sumM = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        vec3 p = o + d * t;
        float h = max(length(p) - kAerialPlanetR, 0.0);
        float dR = exp(-h / kAerialHR) * dt;
        float dM = exp(-h / kAerialHM) * dt;
        odR += dR;
        odM += dM;
        float tSun = AerialRaySphereFar(p, sunDir, kAerialAtmoR);
        float sdt = tSun / float(kLightSteps);
        float st = 0.5 * sdt;
        float sOdR = 0.0;
        float sOdM = 0.0;
        bool inShadow = (tSun <= 0.0);
        for (int j = 0; j < kLightSteps && !inShadow; ++j)
        {
            vec3 sp = p + sunDir * st;
            float sh = length(sp) - kAerialPlanetR;
            if (sh < 0.0) { inShadow = true; break; }
            sOdR += exp(-sh / kAerialHR) * sdt;
            sOdM += exp(-sh / kAerialHM) * sdt;
            st += sdt;
        }
        if (!inShadow)
        {
            vec3 tau = kAerialBetaR * (odR + sOdR) + vec3(kAerialBetaM * 1.1) * (odM + sOdM);
            vec3 att = exp(-tau);
            sumR += att * dR;
            sumM += att * dM;
        }
        t += dt;
    }
    const float kSunIntensity = 20.0;
    vec3 col = kSunIntensity * (sumR * kAerialBetaR * phaseR + sumM * vec3(kAerialBetaM) * phaseM);
    // Plancher nocturne aligné sur sky.frag (jamais le noir absolu).
    return max(col, vec3(0.0015, 0.002, 0.0045));
}

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

// Atténuation UE4 « windowed inverse square » : ~1/d² près de la source, fond à 0
// à d=r (coupure nette, cohérente avec le skip dist>radius).
float PointAtten(float d, float r)
{
    float f = d / max(r, 1e-4);
    float w = clamp(1.0 - f * f * f * f, 0.0, 1.0); // 1 - (d/r)^4, saturé
    return (w * w) / (d * d + 1.0);
}

// CSM — lit la profondeur stockée dans la cascade i à l'UV donné. Index CONSTANT
// (switch) pour éviter l'indexation dynamique non-uniforme d'un sampler array.
float SampleShadowDepth(int i, vec2 uv)
{
    if (i == 0) return texture(uShadowMaps[0], uv).r;
    if (i == 1) return texture(uShadowMaps[1], uv).r;
    if (i == 2) return texture(uShadowMaps[2], uv).r;
    return texture(uShadowMaps[3], uv).r;
}

// CSM — visibilité du soleil. Sélectionne la première cascade i (0→3) où P se
// projette dans [0,1]² (UV) et [0,1] (profondeur) [containment]. Hors de toutes
// -> éclairé (1.0). PCF 3×3 (9 taps, pas = shadowParams.y) ; compare manuel sur
// profondeur Vulkan [0,1] (cf. volumetric_fog.frag) ; biais slope-scaled.
// \param P     Position monde du fragment.
// \param NdotL normale·soleil (>=0), pour le biais.
// \return [0,1] : 1 = éclairé, 0 = ombré.
float ShadowVisibility(vec3 P, float NdotL)
{
    float texel = uShadow.shadowParams.y;
    float bias  = max(uShadow.shadowParams.z,
                      uShadow.shadowParams.w * (1.0 - NdotL));

    for (int i = 0; i < 4; ++i)
    {
        vec4 clip = uShadow.lightViewProj[i] * vec4(P, 1.0);
        if (clip.w <= 0.0) continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv  = ndc.xy * 0.5 + 0.5;   // [-1,1] -> [0,1]
        float refDepth = ndc.z;          // profondeur Vulkan, déjà [0,1]

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
            refDepth < 0.0 || refDepth > 1.0)
            continue; // pas dans cette cascade -> essaie la suivante

        float vis = 0.0;
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec2  off = vec2(float(dx), float(dy)) * texel;
            float occ = SampleShadowDepth(i, uv + off);
            vis += (refDepth - bias <= occ) ? 1.0 : 0.0;
        }
        return vis / 9.0;
    }
    return 1.0; // hors de toutes les cascades -> éclairé
}

// ---- Main -------------------------------------------------------------------
void main()
{
    // ---- Sample GBuffer ------------------------------------------------
    vec4  baseAlbedo = texture(gbufA, inUV);
    vec4  decalOverlay = texture(decalOverlayTex, inUV);
    vec3  albedo    = mix(baseAlbedo.rgb, decalOverlay.rgb, decalOverlay.a);
    vec3  gbufBsamp = texture(gbufB, inUV).rgb;
    vec3  normalW   = normalize(gbufBsamp * 2.0 - 1.0);     // decode [0,1]->[-1,1]
    vec4  orm       = texture(gbufC, inUV);
    float ao_tex    = orm.r;   // AO from ORM texture
    float ao_ssao   = texture(ssaoTex, inUV).r;  // M06.4: screen-space AO
    float ao_final  = ao_ssao * ao_tex;          // M06.4: combine for ambient
    float roughness = max(orm.g, kMinRgh);
    float metallic  = orm.b;
    float depth     = texture(depthTex, inUV).r;

    // ---- Sky / empty fragments (depth == 1.0 means no geometry) --------
    // Phase 5 Lunar (PR #561 fix Concern 3) : si SkyPass est ready
    // (signale par pc.skyColor.w >= 0.5), il a dessine le ciel
    // procedural + disque lunaire dans GBufferA dans le render pass
    // loadOp=LOAD du GeometryPass, uniquement la ou depth==1.0
    // (depthTest=LESS_OR_EQUAL avec gl_Position.z=1.0). On passe alors
    // GBufferA tel quel au scene color HDR sans re-eclairage. Sinon
    // (SkyPass.Init a echoue ou shaders absents au boot) on garde le
    // fallback flat skyColor pilote par DayNightCycle pour ne pas
    // casser le rendu jour/nuit.
    if (depth >= 1.0)
    {
        vec3 skyOut = (pc.skyColor.w >= 0.5) ? baseAlbedo.rgb : pc.skyColor.rgb;
        outSceneColorHDR = vec4(skyOut, 1.0);
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
    // CSM — atténue le SEUL soleil direct par les ombres cascades. Ambient/IBL/DDGI
    // non ombrés. Gating : shadowParams.x <= 0.5 (shadows off / maps invalides) -> 1.
    float vis = (uShadow.shadowParams.x > 0.5) ? ShadowVisibility(P, NdotL) : 1.0;
    vec3  Lo = (diffuse + specular) * pc.lightColor.rgb * NdotL * vis;

    // ---- Point lights (additif, gated count>0, sans ombre) ------------------
    // Même BRDF Cook-Torrance que le soleil (D/G/F réutilisés). Skip si hors du
    // rayon (dist>radius). Atténuation UE4 windowed. count==0 (jour) => sauté.
    if (uPoint.count.x > 0u)
    {
        uint n = min(uPoint.count.x, 64u);
        for (uint i = 0u; i < n; ++i)
        {
            vec3  Lpos   = uPoint.lights[i].posRadius.xyz;
            float radius = uPoint.lights[i].posRadius.w;
            vec3  Lp     = Lpos - P;
            float dist   = length(Lp);
            if (dist > radius) continue;

            vec3  Lp_n = Lp / max(dist, 1e-4);
            vec3  Hp   = normalize(V + Lp_n);
            float NdLp = max(dot(normalW, Lp_n), 0.0);
            if (NdLp <= 0.0) continue;
            float NdHp = max(dot(normalW, Hp), 0.0);
            float VdHp = max(dot(V, Hp), 0.0);

            float Dp = D_GGX(NdHp, roughness);
            float Gp = G_Smith(NdotV, NdLp, roughness);
            vec3  Fp = F_Schlick(VdHp, F0);
            vec3  kSp = Fp;
            vec3  kDp = (1.0 - kSp) * (1.0 - metallic);
            vec3  specp = (Dp * Gp * Fp) / max(4.0 * NdotV * NdLp, 1e-7);
            vec3  diffp = kDp * albedo / PI;

            float atten = PointAtten(dist, radius);
            vec3  radiance = uPoint.lights[i].colorIntensity.rgb
                           * uPoint.lights[i].colorIntensity.a * atten;
            Lo += (diffp + specp) * radiance * NdLp;
        }
    }

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

        ambient = (diffuseIBL + specIBL) * ao_final;
    }
    else
    {
        ambient = pc.ambientColor.rgb * albedo * ao_final;
    }

    // ---- M45.7 — Terme indirect DDGI dynamique (ADDITIF, gated) --------
    // Quand useDdgi <= 0.5 (DÉFAUT), ce bloc est entièrement sauté : le rendu
    // est STRICTEMENT identique au chemin probes statiques (byte-identique).
    // M45.7b : INTERPOLATION TRILINÉAIRE (8 sondes) + pondération par la normale,
    // DDGI APPROCHÉE sans Chebyshev (pas de ray-tracing/profondeur par sonde). On
    // échantillonne l'irradiance dynamique le long de la normale, modulée par
    // albedo, AO et l'intensité (ddgiAtlas.w).
    if (pc.useDdgi > 0.5)
    {
        vec3 ddgiIrr = ddgiSampleTrilinear(P, normalW);
        ddgiIrr = clamp(ddgiIrr, vec3(0.0), vec3(64.0));
        ambient += ddgiIrr * albedo * ao_final * pc.ddgiAtlas.w;
    }

    // ---- Combine & output HDR ------------------------------------------
    vec3  color = ambient + Lo;

    // ---- M45.1 — Perspective aerienne (extinction + inscattering) ------
    // Upgrade du brouillard de distance : zone CLAIRE autour du joueur
    // (< fogStart), puis EXTINCTION EXPONENTIELLE vers une teinte
    // atmospherique. Le joueur n'est PAS noye ; ca ajoute de la profondeur
    // au loin, suit le cycle jour/nuit (base = skyHorizon) et masque la
    // coupe de distance des props (regler fogEnd ~ world.props.cull_distance_m).
    // Teinte = horizon, rechauffee vers le soleil quand on le regarde
    // (inscattering directionnel, sans ray-march — c'est M45.2 qui marchera).
    // Slots : fogStart=cameraPos.w, fogEnd=lightDir.w, density=lightColor.w,
    // inscatter=ambientColor.w. Desactive si fogEnd<=fogStart ou density<=0.
    // Applique UNIQUEMENT aux pixels avec geometrie (le ciel a deja ete ecrit
    // via l'early-return depth>=1.0).
    float fogStart  = pc.cameraPos.w;
    float fogEnd    = pc.lightDir.w;
    float density   = pc.lightColor.w;
    float inscatter = pc.ambientColor.w;
    if (fogEnd > fogStart && density > 0.0)
    {
        float dist = length(pc.cameraPos.xyz - P);
        float d    = max(dist - fogStart, 0.0);
        // Extinction exponentielle (transmittance = exp(-d*density)).
        float fog  = 1.0 - exp(-d * density);
        // Filet de securite : garantit l'opacite complete a fogEnd (masquage du
        // cull des props), meme si la densite seule n'y suffit pas. smoothstep
        // reste ~0 pres de fogStart pour laisser l'exponentielle s'exprimer.
        fog = max(fog, smoothstep(fogStart, fogEnd, dist));
        // Inscattering directionnel : halo chaud vers le soleil (couleur du
        // soleil = lightColor.rgb). rayDir = direction camera->pixel.
        // Base de teinte (chantier perspective aérienne 2026-07-20) :
        //  - mode analytique (aerialSkyModel=1, config client.sky.analytic) :
        //    ciel analytique évalué DANS LA DIRECTION DU RAYON — le terrain
        //    lointain se fond dans le vrai fond de ciel (couchant orangé côté
        //    soleil, bleu froid à l'opposé). Le halo Mie étant déjà dans la
        //    base, le renfort directionnel legacy est réduit (x0.3) pour ne
        //    pas doubler le halo.
        //  - mode legacy : couleur d'horizon uniforme skyColor.rgb (inchangé).
        vec3  rayDir = normalize(P - pc.cameraPos.xyz);
        float sunAmt = pow(max(dot(rayDir, normalize(pc.lightDir.xyz)), 0.0), kAerialSunExponent) * inscatter;
        vec3  tintBase;
        if (pc.aerialSkyModel >= 0.5)
        {
            tintBase = AerialAnalyticHorizon(rayDir, normalize(pc.lightDir.xyz));
            sunAmt *= 0.3;
        }
        else
        {
            tintBase = pc.skyColor.rgb;
        }
        vec3  tint   = mix(tintBase, pc.lightColor.rgb, clamp(sunAmt, 0.0, 1.0));
        color = mix(color, tint, fog);
    }

    outSceneColorHDR = vec4(color, 1.0);
}
