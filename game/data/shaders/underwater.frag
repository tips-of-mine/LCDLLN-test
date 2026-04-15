#version 450
// M37.3 — Underwater post-effects fragment shader.
//
// Composited via alpha blending over SceneColor_HDR (no HDR sampler needed;
// the fragment outputs an overlay colour that the blend equation mixes with
// the existing framebuffer contents).
//
// Effects:
//   1. Depth-based exponential fog:  fogFactor = exp(-density * linearDepth)
//   2. Blue tint:  tintedFog = fogColor * tint (vec3(0.5, 0.7, 1.0) per spec)
//   3. Vignette:   edge darkening, optional (vignetteStrength = 0 → disabled)

layout(location = 0) in vec2 inUV;

// Binding 0: scene depth buffer (NEAREST sampler, for fog computation).
layout(set = 0, binding = 0) uniform sampler2D sceneDepthTex;

layout(push_constant) uniform PushConstants
{
    float fogColorR;        // Fog/tint colour R  (default 0.02)
    float fogColorG;        // Fog/tint colour G  (default 0.08)
    float fogColorB;        // Fog/tint colour B  (default 0.25)
    float fogDensity;       // k in exp(-k * depth), default 0.05
    float nearZ;            // Camera near plane (world units)
    float farZ;             // Camera far plane  (world units)
    float vignetteStrength; // Vignette edge darkening [0, 1]; 0 = disabled
    float _pad;
} pc;

layout(location = 0) out vec4 outColor;

// ── Depth linearisation — Vulkan NDC [0, 1] → view-space metres ─────────────
float LineariseDepth(float ndcDepth)
{
    float n = pc.nearZ;
    float f = pc.farZ;
    return n * f / (f - ndcDepth * (f - n));
}

void main()
{
    // ── Sample and linearise scene depth ─────────────────────────────────────
    float ndcDepth   = texture(sceneDepthTex, inUV).r;
    float linearDist = LineariseDepth(ndcDepth);

    // ── Exponential depth fog:  fogBlend = 1 - exp(-density * depth) ─────────
    // fogBlend is 0 at eye and increases toward 1 in deep water.
    float fogBlend = 1.0 - exp(-pc.fogDensity * linearDist);
    fogBlend = clamp(fogBlend, 0.0, 1.0);

    // ── Underwater tint (spec: color *= vec3(0.5, 0.7, 1.0)) ─────────────────
    // Applied by blending a blue-tinted fog overlay.
    // The base tint shifts existing colour toward the underwater palette.
    const vec3 kTint = vec3(0.5, 0.7, 1.0);
    vec3 fogColor    = vec3(pc.fogColorR, pc.fogColorG, pc.fogColorB) * kTint;

    // ── Optional vignette — darken edges to simulate scattering ──────────────
    float vignette = 1.0;
    if (pc.vignetteStrength > 0.0)
    {
        // Distance from screen centre [0, ~0.7 at corners].
        vec2  centred = inUV - 0.5;
        float dist    = length(centred) * 2.0; // 0 at centre, 1 at edge corners
        vignette = 1.0 - pc.vignetteStrength * dist * dist;
        vignette = clamp(vignette, 0.0, 1.0);
    }

    // ── Compose output — alpha-blended overlay ────────────────────────────────
    // Alpha = fogBlend: deep water → high alpha (fog colour dominates);
    //                   near eye   → low alpha  (original colour shows through).
    // Colour = fogColor * vignette.
    float alpha = fogBlend * vignette;
    outColor = vec4(fogColor * vignette, alpha);
}
