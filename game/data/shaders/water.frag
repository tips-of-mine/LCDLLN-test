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

    // Courant : direction per-vertex (rivière) ou push constant (lac / mer).
    vec2 flowEff = (length(vFlowDir) > 0.001) ? vFlowDir : pc.flowDirection;
    vec2 flow = flowEff * pc.flowSpeed * pc.timeSeconds;
    float t = pc.timeSeconds;

    // ── Normales : texture (si un jour fournie) + rides procédurales ─────────
    vec2 uv1 = vUv * 8.0  + flow;
    vec2 uv2 = vUv * 16.0 - flow * 0.5;
    vec3 n1 = unpackNormal(texture(u_normalMap, uv1).xyz);
    vec3 n2 = unpackNormal(texture(u_normalMap, uv2).xyz);
    vec3 nTex = normalize(n1 + n2);
    // Rides plus marquées là où l'eau court (rivière), plus douces sinon.
    // Atténuées avec la distance : au loin les rides deviennent plus petites
    // qu'un pixel et scintilleraient (crénelage) au lieu de se fondre.
    float distFade = 1.0 / (1.0 + surfaceDist * 0.025);
    float rippleStrength = (0.9 + 1.6 * clamp(pc.flowSpeed, 0.0, 1.0)) * distFade;
    vec3 nRip = rippleNormal(vWorldPos.xz, flow, t, rippleStrength);
    vec3 surfaceN = normalize(vec3(nRip.x + nTex.x * 0.5, 1.0, nRip.z + nTex.z * 0.5));

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

    // ── EAU.1 — écume des rivages ────────────────────────────────────────────
    // Bande blanche là où l'eau est mince (rive), découpée par du bruit et
    // parcourue de lignes qui « lèchent » la rive (ressac), plus des taches
    // d'écume entraînées par le courant. Disparaît au-delà d'~1,2 m d'épaisseur.
    float shore   = 1.0 - smoothstep(0.0, 1.2, waterThickness);
    float lap     = 0.5 + 0.5 * sin(waterThickness * 9.0 - t * 1.6 + valueNoise(vWorldPos.xz * 0.6) * 6.0);
    float breakup = valueNoise((vWorldPos.xz - flow) * 1.7 + vec2(t * 0.05, -t * 0.04));
    // Au loin, les lignes de ressac (fines) sont remplacées par leur moyenne.
    lap = mix(0.5, lap, distFade);
    float foam    = shore * smoothstep(0.35, 0.85, lap * 0.6 + breakup * 0.7);
    foam = max(foam, smoothstep(0.92, 1.0, shore) * 0.85); // liseré continu au ras du bord
    vec3 foamCol  = vec3(0.85, 0.90, 0.92);
    color = mix(color, foamCol, clamp(foam, 0.0, 1.0) * 0.85);

    // alpha : bord adouci (fondu vers le fond déjà présent dans PostWater).
    fragColor = vec4(color, edge);
}
