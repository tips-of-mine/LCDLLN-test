#version 450

layout(set=0, binding=0) uniform sampler2D u_sceneColor;
layout(set=0, binding=1) uniform sampler2D u_sceneDepth;
layout(set=0, binding=2) uniform sampler2D u_normalMap;
layout(set=0, binding=3) uniform samplerCube u_skybox;

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;
    float flowSpeed;
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec2 vFlowDir;

layout(location = 0) out vec4 fragColor;

vec3 unpackNormal(vec3 n) { return normalize(n * 2.0 - 1.0); }

// Linearise une profondeur [0,1] (Vulkan) en distance approx. le long de l'axe
// camera. Sert a estimer l'EPAISSEUR de colonne d'eau (fond - surface) pour
// l'opacite selon la profondeur. near/far de la projection du jeu
// (Mat4::PerspectiveVulkan) : near=0.1, far=1000 — deduits du viewProj observe
// (row2 = (.,.,-1.0001,-0.1)). d=0 -> near, d=1 -> far (monotone croissant).
float linearizeDepth(float d)
{
    const float NEAR_PLANE = 0.1;
    const float FAR_PLANE  = 1000.0;
    return (NEAR_PLANE * FAR_PLANE) / (FAR_PLANE - d * (FAR_PLANE - NEAR_PLANE));
}

// SSR mince : raymarch en screen-space, max 32 steps, fallback skybox.
vec3 ssrTrace(vec3 worldPos, vec3 reflectDir, vec3 fallback)
{
    const int   kMaxSteps  = 32;
    const float kStepSize  = 0.5;
    const float kDepthEpsilon = 0.05;  // ~5% NDC depth = thin surface tolerance

    vec3 rayPos = worldPos + reflectDir * 0.05;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        rayPos += reflectDir * kStepSize;
        vec4 clip = pc.viewProj * vec4(rayPos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        if (any(lessThan(ndc.xy, vec2(-1.0))) || any(greaterThan(ndc.xy, vec2(1.0))))
            break;
        vec2 screenUv = ndc.xy * 0.5 + 0.5;
        float sampledDepth = texture(u_sceneDepth, screenUv).r;
        float rayDepthNorm = ndc.z * 0.5 + 0.5;
        if (sampledDepth < rayDepthNorm - 1e-4)
        {
            float depthDiff = rayDepthNorm - sampledDepth;
            if (depthDiff < kDepthEpsilon)
                return texture(u_sceneColor, screenUv).rgb;
        }
    }
    return fallback;
}

// ── EAU.1 — bruit procédural (houle fine, écume) ─────────────────────────────
// Les textures d'eau (normalMap) sont encore des 1x1 muettes : la surface
// recevait des normales plates et l'eau semblait figée. On calcule donc des
// rides en espace MONDE (motif stable quand la caméra bouge), animées dans le
// temps et entraînées par le courant des rivières.
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Hauteur des rides (sans unité) au point monde p (m) : deux trains de rides
// croisés + bruit, entraînés par `flow` (m, déjà multiplié par le temps).
float rippleHeight(vec2 p, vec2 flow, float t)
{
    vec2 q = p - flow;
    float h = 0.0;
    h += 0.50 * valueNoise(q * 0.35 + vec2(t * 0.11, t * 0.07));
    h += 0.30 * valueNoise(q * 0.90 - vec2(t * 0.17, -t * 0.13));
    h += 0.20 * valueNoise(q * 2.10 + vec2(-t * 0.29, t * 0.23));
    return h;
}

// Normale des rides par différences finies. \param strength pente (0 = plat).
vec3 rippleNormal(vec2 p, vec2 flow, float t, float strength)
{
    const float e = 0.15;
    float h0 = rippleHeight(p, flow, t);
    float hx = rippleHeight(p + vec2(e, 0.0), flow, t);
    float hz = rippleHeight(p + vec2(0.0, e), flow, t);
    return normalize(vec3((h0 - hx) * strength, 1.0, (h0 - hz) * strength));
}

// ── EAU.2 — houle de la mer ──────────────────────────────────────────────────
// Somme de 4 trains de vagues directionnels (longueurs 21, 13, 8 et 5 m) autour
// de la direction du vent \p dir. Renvoie la pente (dérivées x, z) de la
// surface et, dans \p crest, un indicateur 0..1 de crête (sommet de vague) qui
// sert à placer l'écume. Pas de déplacement de géométrie (la nappe de la mer
// n'a que 4 sommets) : la houle se lit par la lumière, les reflets et l'écume.
vec2 swellSlope(vec2 p, vec2 dir, float t, out float crest)
{
    const float kLen[4]   = float[4](21.0, 13.0, 8.0, 5.0);
    const float kAmp[4]   = float[4](0.55, 0.30, 0.18, 0.10);
    const float kAngle[4] = float[4](0.0, 0.45, -0.60, 1.10);
    vec2 slope = vec2(0.0);
    float h = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        float a = kAngle[i];
        vec2 d = normalize(vec2(dir.x * cos(a) - dir.y * sin(a), dir.x * sin(a) + dir.y * cos(a)));
        float k = 6.2831853 / kLen[i];
        float c = sqrt(9.81 / k);                 // vitesse de phase (eau profonde)
        float ph = k * dot(d, p) - k * c * t;
        // Profil légèrement pointu (crêtes plus étroites que les creux).
        float s = sin(ph);
        h += kAmp[i] * (s * 0.5 + 0.5);
        slope += d * (kAmp[i] * k * cos(ph));
    }
    crest = clamp(h / 1.13, 0.0, 1.0);
    return slope;
}

void main()
{
    vec2 screenUv = gl_FragCoord.xy / pc.screenSize;

    // ── Occlusion de profondeur (en espace LINEAIRE / metres) ────────────────
    // La passe water dessine dans PostWater SANS depth-attachment : on compare
    // MANUELLEMENT la profondeur du fragment d'eau à la scène opaque, en
    // distances linéaires (bias métrique stable, cf. historique).
    float occluderDepth  = texture(u_sceneDepth, screenUv).r;
    float surfaceDist    = linearizeDepth(gl_FragCoord.z);   // distance camera->surface eau
    float bottomDist     = linearizeDepth(occluderDepth);    // distance camera->fond opaque
    // Bord ADOUCI (anti-crawl TAA) sur ~0.20 m.
    float edge = smoothstep(surfaceDist - 0.20, surfaceDist - 0.02, bottomDist);
    if (edge <= 0.001)
        discard;   // entierement occulte (joueur/terrain devant) : rien a dessiner

    // ── EAU.2 — trois sortes d'eau ───────────────────────────────────────────
    // Rivière : direction de courant par sommet (vFlowDir).
    // Mer     : flowSpeed < 0 (convention WaterPass : |flowSpeed| = force de la
    //           houle, flowDirection = direction du vent).
    // Lac     : le reste — eau calme, rides légères.
    bool isRiver = length(vFlowDir) > 0.001;
    bool isSea   = !isRiver && pc.flowSpeed < -0.001;
    float t = pc.timeSeconds;
    vec2 flowEff = isRiver ? normalize(vFlowDir) : pc.flowDirection;
    // Rivière : le motif est entraîné ~2,5 fois plus vite que la vitesse
    // nominale pour que le courant se lise bien à l'écran.
    vec2 flow = isRiver ? flowEff * pc.flowSpeed * 2.5 * t : vec2(0.0);

    // Au loin, les détails plus petits qu'un pixel scintilleraient : atténués.
    float distFade = 1.0 / (1.0 + surfaceDist * 0.025);

    // ── Normales : texture (si un jour fournie) + rides procédurales ─────────
    vec2 uv1 = vUv * 8.0  + flow;
    vec2 uv2 = vUv * 16.0 - flow * 0.5;
    vec3 n1 = unpackNormal(texture(u_normalMap, uv1).xyz);
    vec3 n2 = unpackNormal(texture(u_normalMap, uv2).xyz);
    vec3 nTex = normalize(n1 + n2);

    vec2 ripP = vWorldPos.xz;
    float rippleStrength = 0.45;                  // lac : presque lisse
    if (isRiver)
    {
        // Rides étirées dans le sens du courant (filets d'eau).
        vec2 side = vec2(-flowEff.y, flowEff.x);
        ripP = vec2(dot(vWorldPos.xz, side) * 1.8, dot(vWorldPos.xz, flowEff) * 0.6);
        flow = vec2(0.0, pc.flowSpeed * 2.5 * t * 0.6);  // même entraînement, repère du courant
        rippleStrength = 1.6;
    }
    else if (isSea)
        rippleStrength = 0.8;
    vec3 nRip = rippleNormal(ripP, flow, t, rippleStrength * distFade);
    vec2 slope = vec2(nRip.x, nRip.z) + nTex.xz * 0.5;

    float crest = 0.0;
    if (isSea)
    {
        // Houle : direction du vent, force = |flowSpeed|.
        vec2 windDir = length(pc.flowDirection) > 0.001 ? normalize(pc.flowDirection) : vec2(1.0, 0.0);
        float swellAmp = clamp(-pc.flowSpeed, 0.0, 3.0);
        vec2 sw = swellSlope(vWorldPos.xz, windDir, t, crest);
        slope -= sw * swellAmp * mix(0.35, 1.0, distFade);
    }
    vec3 surfaceN = normalize(vec3(slope.x, 1.0, slope.y));

    vec3 viewDir = normalize(pc.cameraPos - vWorldPos);
    float NdotV  = max(0.0, dot(surfaceN, viewDir));

    // ── Épaisseur de la colonne d'eau traversée (m) ──────────────────────────
    float waterThickness = max(0.0, bottomDist - surfaceDist);

    // ── Réfraction : le fond vu à travers l'eau, déformé par les rides ───────
    // Déformation atténuée près du bord (sinon le sable de la rive « nage »).
    float refrScale = pc.refractionAmount * clamp(waterThickness * 0.8, 0.0, 1.0);
    vec2 refrUv = clamp(screenUv + surfaceN.xz * refrScale, vec2(0.0), vec2(1.0));
    vec3 refr   = texture(u_sceneColor, refrUv).rgb;
    refr        = mix(refr, pc.bottomColor, pc.turbidity * 0.5);

    // ── EAU.1 — couleur selon la profondeur (aspect « Valheim ») ─────────────
    // Absorption de Beer-Lambert PAR CANAL : le rouge disparaît en premier,
    // puis le vert ; l'eau peu profonde laisse voir le fond teinté turquoise,
    // l'eau profonde tourne au bleu sombre. Valeurs LINÉAIRES (tonemap après).
    vec3 absorb      = vec3(0.45, 0.10, 0.07);            // par mètre
    vec3 transmitted = refr * exp(-absorb * waterThickness);
    vec3 shallowTint = vec3(0.04, 0.32, 0.34);            // turquoise (lagon)
    vec3 deepTint    = vec3(0.010, 0.060, 0.140);         // bleu profond
    float depthMix   = 1.0 - exp(-waterThickness * 0.18); // 0 bord .. 1 large
    vec3 scatterCol  = mix(shallowTint, deepTint, depthMix);
    // Part de diffusion : quasi nulle au bord (eau claire), forte au large.
    float scatterAmt = clamp(1.0 - exp(-waterThickness * 0.55), 0.0, 1.0);
    vec3 color = mix(transmitted, scatterCol, scatterAmt);

    // ── Reflets (ciel / SSR), Fresnel ────────────────────────────────────────
    vec3 reflectDir  = reflect(-viewDir, surfaceN);
    vec3 skyFallback = texture(u_skybox, reflectDir).rgb;
    vec3 refl = ssrTrace(vWorldPos, reflectDir, skyFallback);
    float f = pow(1.0 - NdotV, pc.fresnelPower);
    color = mix(color, refl, clamp(f * pc.reflectionStrength, 0.0, 1.0));
    float fres = clamp(pow(1.0 - NdotV, 3.0), 0.0, 1.0);
    color += vec3(0.05, 0.08, 0.10) * fres;               // lisere clair rasant

    // ── Écume (EAU.1, affinée EAU.2) ─────────────────────────────────────────
    // Rives : bande blanche là où l'eau est mince, découpée par du bruit et
    // parcourue de lignes de ressac. EAU.2 : bande resserrée (0,7 m au lieu de
    // 1,2 m) — autour d'un objet plongé dans l'eau (jambes, troncs) l'épaisseur
    // vue est faible aussi, et l'ancienne bande les peignait en blanc.
    float shore   = 1.0 - smoothstep(0.0, 0.7, waterThickness);
    float lap     = 0.5 + 0.5 * sin(waterThickness * 12.0 - t * 1.6 + valueNoise(vWorldPos.xz * 0.6) * 6.0);
    lap = mix(0.5, lap, distFade);
    float breakup = valueNoise((vWorldPos.xz - flowEff * (isRiver ? pc.flowSpeed * 2.5 * t : 0.0)) * 1.7 + vec2(t * 0.05, -t * 0.04));
    float foam    = shore * smoothstep(0.45, 0.9, lap * 0.6 + breakup * 0.7);
    foam = max(foam, smoothstep(0.85, 1.0, shore) * 0.7); // liseré au ras du bord (~10 cm)
    if (isSea)
    {
        // Moutons : écume sur les crêtes de la houle, en taches irrégulières.
        float caps = smoothstep(0.78, 0.95, crest) * smoothstep(0.55, 0.85, valueNoise(vWorldPos.xz * 0.25 + t * 0.1));
        foam = max(foam, caps * 0.75 * clamp(-pc.flowSpeed, 0.0, 1.5) * distFade);
    }
    else if (isRiver)
    {
        // Rivière : traînées d'écume légères qui filent avec le courant.
        float streak = smoothstep(0.72, 0.92, valueNoise((ripP - vec2(0.0, pc.flowSpeed * 2.5 * t * 0.6)) * vec2(0.5, 0.12)));
        foam = max(foam, streak * 0.35 * distFade);
    }
    vec3 foamCol  = vec3(0.85, 0.90, 0.92);
    color = mix(color, foamCol, clamp(foam, 0.0, 1.0) * 0.85);

    // alpha : bord adouci (fondu vers le fond déjà présent dans PostWater).
    fragColor = vec4(color, edge);
}
