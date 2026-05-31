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

void main()
{
    vec2 screenUv = gl_FragCoord.xy / pc.screenSize;

    // ── Occlusion de profondeur ──────────────────────────────────────────────
    // La passe water dessine dans PostWater SANS depth-attachment. On teste donc
    // MANUELLEMENT la profondeur du fragment d'eau (gl_FragCoord.z, deja en [0,1]
    // sous Vulkan) contre la profondeur de la scene opaque (u_sceneDepth) :
    //  - si une surface opaque est DEVANT l'eau (profondeur plus PETITE = plus
    //    proche camera), on jette le fragment -> empeche l'eau de se peindre
    //    par-dessus le joueur / le terrain situes devant la nappe (bug observe :
    //    "l'eau coupe le joueur alors qu'il n'est pas dans l'eau").
    //  - le FOND du bassin est PLUS LOIN que la surface -> non rejete -> l'eau
    //    reste bien visible a l'interieur du bol creuse.
    // Bias VOLONTAIREMENT large (2.5e-3) : a distance la profondeur sature
    // (projection near 0.05 / far 1000) et le bruit de quantification du depth
    // buffer faisait clignoter le test -> l'eau disparaissait par plaques et le
    // bord du bassin "vibrait" (bug observe). On ne jette donc le fragment QUE si
    // l'occludeur est NETTEMENT devant (marge 2.5e-3) : le joueur (tres proche)
    // occulte toujours l'eau, mais le fond/bord du bassin (a profondeur quasi
    // egale) ne declenche plus de faux rejet -> nappe stable.
    float occluderDepth = texture(u_sceneDepth, screenUv).r;
    if (occluderDepth < gl_FragCoord.z - 2.5e-3)
        discard;

    // Flow effective : prend la direction per-vertex (rivière) ou le push constant (lac).
    vec2 flowEff = (length(vFlowDir) > 0.001) ? vFlowDir : pc.flowDirection;
    vec2 flow = flowEff * pc.flowSpeed * pc.timeSeconds;

    vec2 uv1 = vUv * 8.0  + flow;
    vec2 uv2 = vUv * 16.0 - flow * 0.5;
    vec3 n1 = unpackNormal(texture(u_normalMap, uv1).xyz);
    vec3 n2 = unpackNormal(texture(u_normalMap, uv2).xyz);
    vec3 n  = normalize(n1 + n2);

    vec2 refrUv   = clamp(screenUv + n.xz * pc.refractionAmount, vec2(0.0), vec2(1.0));
    vec3 refr     = texture(u_sceneColor, refrUv).rgb;
    refr          = mix(refr, pc.bottomColor, pc.turbidity);

    vec3 viewDir    = normalize(pc.cameraPos - vWorldPos);
    vec3 surfaceN   = normalize(vec3(n.x, 1.0, n.z));
    vec3 reflectDir = reflect(-viewDir, surfaceN);
    vec3 skyFallback = texture(u_skybox, reflectDir).rgb;
    vec3 refl = ssrTrace(vWorldPos, reflectDir, skyFallback);

    float NdotV = max(0.0, dot(surfaceN, viewDir));
    float f = pow(1.0 - NdotV, pc.fresnelPower);

    // ── Couleur d'eau GARANTIE visible ───────────────────────────────────────
    // Les textures (normalMap/skybox) sont muettes (1x1) et reflectionStrength
    // peut valoir 0 : on ne peut donc PAS compter sur le SSR/reflet pour rendre
    // l'eau visible (sinon la nappe est quasi transparente = invisible). On
    // impose une couleur d'eau bleu-vert nette, melangee a la refraction
    // (garde un peu de transparence/profondeur), avec un lisere de Fresnel clair
    // pour la brillance de surface. La nappe se lit ainsi toujours comme de l'eau.
    float fres = clamp(pow(1.0 - NdotV, 3.0), 0.0, 1.0);
    vec3 deepWater    = vec3(0.05, 0.35, 0.55);   // bleu-cyan profond (vu d'aplomb)
    vec3 shallowWater = vec3(0.12, 0.46, 0.62);   // bleu (angle rasant) — assombri
                                                  // pour NE PAS saturer en blanc.
    vec3 waterBody    = mix(deepWater, shallowWater, fres);

    // ── Opacite selon la profondeur (transparence du dessus) ─────────────────
    // Epaisseur de la colonne d'eau traversee par le rayon = distance lineaire
    // entre le FOND (u_sceneDepth, deja echantillonne plus haut) et la SURFACE
    // (gl_FragCoord.z). Loi de Beer-Lambert : fin -> transparent (on voit le
    // fond sableux via la refraction), epais -> bleu opaque (le fond disparait).
    // L'epaisseur augmente avec la profondeur ET avec l'angle rasant : un joueur
    // exterieur voit donc le fond pres du bord, mais plus du tout vers le centre.
    float surfaceDist = linearizeDepth(gl_FragCoord.z);
    float bottomDist  = linearizeDepth(occluderDepth);
    float waterThickness = max(0.0, bottomDist - surfaceDist);          // metres approx
    float beer    = 1.0 - exp(-waterThickness * 0.15);                  // 0 (fin) .. 1 (epais)
    float opacity = mix(0.12, 0.97, beer);                              // 12% mini (eau toujours
                                                                        // teintee) .. 97% maxi

    vec3 color = mix(refr, waterBody, opacity);                              // fond <-> eau selon profondeur
    color = mix(color, refl, clamp(f * pc.reflectionStrength, 0.0, 1.0));    // reflet si dispo
    color += vec3(0.06, 0.09, 0.12) * fres;                                  // sheen discret (etait
                                                                             // 0.20,0.26,0.32 -> blanc)
    fragColor = vec4(color, 1.0);
}
