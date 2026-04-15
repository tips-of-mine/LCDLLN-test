#version 450
// M37.1 — Water surface fragment shader: reflection, refraction, Fresnel blend.
// M37.2 — Foam (depth-based shore edges), caustics (animated procedural projection).

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inClipPos;

layout(set = 0, binding = 0) uniform sampler2D reflectionTex;
layout(set = 0, binding = 1) uniform sampler2D refractionTex;
// M37.2 — Scene depth texture for depth-based foam edge detection.
layout(set = 0, binding = 2) uniform sampler2D sceneDepthTex;

layout(push_constant) uniform PushConstants
{
    mat4  viewProj;
    vec4  cameraPos;
    float waterY;
    float time;
    float fresnelF0;
    float planeHalfSize;
    // M37.2:
    float waveAmplitude;       // (not used in fragment; mirrors WaterParams layout)
    float foamThreshold;       // Depth difference below which foam is shown (metres).
    float causticsScale;       // UV tiling scale for procedural caustics.
    float causticsIntensity;   // Caustics brightness multiplier.
    float nearZ;               // Camera near plane for depth linearisation.
    float farZ;                // Camera far plane for depth linearisation.
} pc;

layout(location = 0) out vec4 outColor;

// ── Animated procedural normal (two-layer scrolling sine waves) ─────────────
vec3 ComputeNormal()
{
    const float tiling = 8.0;
    vec2 uv1 = inUV * tiling + vec2( pc.time * 0.025,  pc.time * 0.018);
    vec2 uv2 = inUV * tiling * 1.6 + vec2(-pc.time * 0.018, pc.time * 0.030);

    float w1 = sin(uv1.x * 6.2832) * cos(uv1.y * 6.2832);
    float w2 = sin(uv2.x * 6.2832) * cos(uv2.y * 6.2832);

    vec2 d = vec2(w1, w2) * 0.12;
    return normalize(vec3(d.x, 1.0, d.y));
}

// ── Schlick Fresnel: F = F0 + (1 - F0) * (1 - dot(N,V))^5 ──────────────────
float Fresnel(vec3 N, vec3 V)
{
    float cosTheta = clamp(dot(N, V), 0.0, 1.0);
    return pc.fresnelF0 + (1.0 - pc.fresnelF0) * pow(1.0 - cosTheta, 5.0);
}

// ── M37.2 — Depth linearisation (Vulkan NDC depth [0, 1] → view-space metres) ─
float LineariseDepth(float ndcDepth)
{
    float n = pc.nearZ;
    float f = pc.farZ;
    // Inverse Vulkan perspective: d = n / (f - ndcDepth * (f - n))
    return n * f / (f - ndcDepth * (f - n));
}

// ── M37.2 — Procedural caustics: animated interference pattern ───────────────
// Produces a bright caustic effect in shallow water without a texture asset.
// Returns intensity in [0, 1] to modulate refracted colour.
float ComputeCaustics(vec2 worldXZ)
{
    if (pc.causticsIntensity <= 0.0)
        return 0.0;

    vec2 uv = worldXZ * pc.causticsScale;
    vec2 a  = uv + vec2( pc.time * 0.07,  pc.time * 0.05);
    vec2 b  = uv + vec2(-pc.time * 0.05,  pc.time * 0.08);
    vec2 c  = uv * 0.7 + vec2( pc.time * 0.04, -pc.time * 0.06);

    // Interference of three wave fronts.
    float v = abs(sin(a.x * 6.2832) * sin(a.y * 6.2832))
            + abs(sin(b.x * 6.2832) * sin(b.y * 6.2832))
            + abs(sin(c.x * 6.2832) * sin(c.y * 6.2832));
    v /= 3.0;
    // Power to sharpen the bright caustic lines.
    return pow(v, 3.0) * pc.causticsIntensity;
}

void main()
{
    // ── Screen-space UV ────────────────────────────────────────────────────
    vec2 ndcXY    = inClipPos.xy / inClipPos.w;
    vec2 screenUV = ndcXY * 0.5 + 0.5;

    // ── Normal-map distortion ──────────────────────────────────────────────
    vec3 N          = ComputeNormal();
    vec2 distortion = N.xz * 0.025;

    vec2 reflUV    = vec2(screenUV.x + distortion.x, 1.0 - screenUV.y + distortion.y);
    vec2 refractUV = screenUV + distortion;
    reflUV    = clamp(reflUV,    vec2(0.001), vec2(0.999));
    refractUV = clamp(refractUV, vec2(0.001), vec2(0.999));

    // ── Sample reflection and refraction ──────────────────────────────────
    vec3 reflectColor = texture(reflectionTex, reflUV).rgb;
    vec3 refractColor = texture(refractionTex, refractUV).rgb;

    // ── M37.2 — Caustics: modulate refracted colour (shallow underwater) ───
    float caustics = ComputeCaustics(inWorldPos.xz);
    refractColor += vec3(caustics * 0.35, caustics * 0.42, caustics * 0.20);

    // ── Fresnel blend ──────────────────────────────────────────────────────
    vec3  V       = normalize(pc.cameraPos.xyz - inWorldPos);
    float fresnel = Fresnel(N, V);
    vec3  waterColor = mix(refractColor, reflectColor, fresnel);

    // ── Deep-water tint ────────────────────────────────────────────────────
    const vec3 kDeepColor = vec3(0.02, 0.08, 0.18);
    waterColor = mix(waterColor, kDeepColor, 0.15);

    // ── M37.2 — Foam: depth-based shore edge blend ─────────────────────────
    // Compare linearised scene depth with water surface depth.
    // When the terrain is close to the water surface, add foam.
    float sceneNdcDepth  = texture(sceneDepthTex, screenUV).r;
    float sceneLinearZ   = LineariseDepth(sceneNdcDepth);
    float waterLinearZ   = LineariseDepth(gl_FragCoord.z);
    float depthDiff      = sceneLinearZ - waterLinearZ;

    // foamMask → 1 when very close to shore, 0 in deep water.
    float foamMask = 1.0 - clamp(depthDiff / max(pc.foamThreshold, 0.01), 0.0, 1.0);
    // Smooth the edge using a power curve.
    foamMask = pow(foamMask, 1.5);

    // Procedural foam pattern (avoid texture asset, use noise-like sine).
    const float kFoamTile = 32.0;
    float foamNoise = sin((inUV.x * kFoamTile + pc.time * 0.6) * 6.2832)
                    * sin((inUV.y * kFoamTile + pc.time * 0.4) * 6.2832);
    foamNoise = foamNoise * 0.5 + 0.5; // [0, 1]

    const vec3 kFoamColor = vec3(0.94, 0.97, 1.00);
    waterColor = mix(waterColor, kFoamColor, foamMask * foamNoise * 0.85);

    // ── Alpha ──────────────────────────────────────────────────────────────
    float alpha = mix(0.85, 1.0, fresnel);
    // Near shore: more opaque (foam is nearly fully opaque).
    alpha = max(alpha, foamMask * 0.95);

    outColor = vec4(waterColor, alpha);
}
