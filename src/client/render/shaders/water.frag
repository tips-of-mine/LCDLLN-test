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
    // NOTE depth : la passe water dessine dans SceneColor_HDR_PostWater SANS
    // depth-attachment. On NE fait PAS de depth-test ecran ici : la projection
    // (near 0.05 / far 1000) sature la precision de profondeur des ~50 m — or
    // l'eau de test est a ~50 m — donc toute comparaison NDC ecran y est non
    // fiable (les profondeurs se confondent) et rejetait l'eau A TORT -> nappe
    // invisible (bug observe). L'eau etant confinee dans un BOL creuse du
    // heightmap et le ping-pong PostWater etant TOUJOURS ecrit (gating dans
    // Engine.cpp), l'absence de depth-test ne reintroduit pas le plein-ecran
    // blanc : la nappe reste une etendue LOCALE. Occlusion fine du rivage par le
    // terrain : a refaire plus tard en world-space (inverse projection) si besoin.
    vec2 screenUv = gl_FragCoord.xy / pc.screenSize;

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
    vec3 deepWater    = vec3(0.05, 0.35, 0.55);   // bleu vif profond (vu d'aplomb)
    vec3 shallowWater = vec3(0.20, 0.58, 0.74);   // bleu clair (angle rasant)
    vec3 waterBody    = mix(deepWater, shallowWater, fres);

    vec3 color = mix(refr, waterBody, 0.85);                                 // 85% eau, 15% fond
    color = mix(color, refl, clamp(f * pc.reflectionStrength, 0.0, 1.0));    // reflet si dispo
    color += vec3(0.20, 0.26, 0.32) * fres;                                  // sheen de surface
    fragColor = vec4(color, 1.0);
}
