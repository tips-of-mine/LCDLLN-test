#version 450
// M37.1 — Water surface fragment shader.
// M37.2 — Added foam (depth-based edge), caustics (animated texture projection).
//
// Implements:
//   1. Projective screen-space UVs from clip position (reflection + refraction sampling).
//   2. Normal map distortion (two scrolling layers blended) for ripple appearance.
//   3. Fresnel blend:  F = F0 + (1 - F0) * (1 - dot(N, V))^5
//      Blends reflected colour (high angle) with refracted colour (low angle).
//   4. Depth fade: water becomes transparent near shores (shallow) and opaque in deep zones.
//   5. Deep-water colour tint blended in based on depth.
//   6. M37.2 — Foam:     if (depth < foamThreshold) blend foam texture.
//   7. M37.2 — Caustics: animated texture projected from above, modulates scene colour.
//
// Inputs from vertex shader:
//   location 0: vec3  vWorldPos     — world-space position on the water plane (displaced by waves)
//   location 1: vec4  vClipPos      — clip-space position (for projective UV)
//   location 2: vec2  vNormalUV1    — first scrolling normal-map UV
//   location 3: vec2  vNormalUV2    — second scrolling normal-map UV
//
// Descriptor set 0:
//   binding 0: sampler2D  reflectionTex  — reflection RT (half-res, above-water scene)
//   binding 1: sampler2D  refractionTex  — refraction RT (full-res, below-water scene)
//   binding 2: sampler2D  depthTex       — scene depth (D32_SFLOAT, sampled as .r)
//   binding 3: sampler2D  normalMapTex   — water normal map (RGB tangent normals)
//   binding 4: sampler2D  foamTex        — M37.2 foam edge texture (RGBA, repeating)
//   binding 5: sampler2D  causticsTex    — M37.2 animated caustics texture (RGBA, repeating)
//
// Push constants (128 bytes):
//   mat4  viewProj, vec4 cameraPos, float waterLevel, float time, float normalTiling,
//   float normalScrollX, float normalScrollZ, float fresnelF0, float fadeDepthMeters,
//   float waveAmplitude (M37.2), float waveFrequency (M37.2),
//   float foamThreshold (M37.2), float causticsTiling (M37.2), float causticsScroll (M37.2)
//
// Output:
//   location 0: vec4  outColor      — water colour in HDR (alpha = blend weight)

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec4 vClipPos;
layout(location = 2) in vec2 vNormalUV1;
layout(location = 3) in vec2 vNormalUV2;

layout(set = 0, binding = 0) uniform sampler2D reflectionTex;
layout(set = 0, binding = 1) uniform sampler2D refractionTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;
layout(set = 0, binding = 3) uniform sampler2D normalMapTex;
layout(set = 0, binding = 4) uniform sampler2D foamTex;      // M37.2
layout(set = 0, binding = 5) uniform sampler2D causticsTex;  // M37.2

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  cameraPos;
    float waterLevel;
    float time;
    float normalTiling;
    float normalScrollX;
    float normalScrollZ;
    float fresnelF0;
    float fadeDepthMeters;
    float waveAmplitude;   // M37.2
    float waveFrequency;   // M37.2
    float foamThreshold;   // M37.2 — metres below which foam appears (spec: 0.5 m)
    float causticsTiling;  // M37.2 — caustics UV tile multiplier
    float causticsScroll;  // M37.2 — caustics UV scroll speed
} pc;

layout(location = 0) out vec4 outColor;

// ---- Fresnel term (Schlick approximation) -----------------------------------
float FresnelSchlick(float cosTheta, float F0)
{
    float x = 1.0 - clamp(cosTheta, 0.0, 1.0);
    return F0 + (1.0 - F0) * (x * x * x * x * x);
}

void main()
{
    // ---- Compute projective screen-space UV -----------------------------------
    // NDC xy in [-1,1]; convert to [0,1] for texture sampling.
    vec3 ndcPos   = vClipPos.xyz / vClipPos.w;
    vec2 screenUV = ndcPos.xy * 0.5 + 0.5;

    // ---- Sample and blend two normal-map layers for ripples ------------------
    vec3 n1 = texture(normalMapTex, vNormalUV1).rgb * 2.0 - 1.0;
    vec3 n2 = texture(normalMapTex, vNormalUV2).rgb * 2.0 - 1.0;
    // Average the two normal layers; re-normalise.
    vec3 waterNormal = normalize(n1 + n2);
    // Water plane normal is UP (world Y); distort by the ripple normal map.
    waterNormal = normalize(vec3(waterNormal.x * 0.15, 1.0, waterNormal.z * 0.15));

    // ---- Distort screen UVs for reflection / refraction ----------------------
    const float kDistortStrength = 0.02;
    vec2 distortOffset = waterNormal.xz * kDistortStrength;

    vec2 reflUV = clamp(screenUV + distortOffset,        vec2(0.001), vec2(0.999));
    vec2 refrUV = clamp(screenUV + distortOffset * 0.5,  vec2(0.001), vec2(0.999));

    // Reflection is sampled with a vertical flip (Y is inverted in screen space).
    reflUV.y = 1.0 - reflUV.y;

    vec3 reflColor = texture(reflectionTex, reflUV).rgb;
    vec3 refrColor = texture(refractionTex, refrUV).rgb;

    // ---- Fresnel blend -------------------------------------------------------
    vec3 viewDir  = normalize(pc.cameraPos.xyz - vWorldPos);
    float cosTheta = dot(waterNormal, viewDir);
    float fresnel  = FresnelSchlick(cosTheta, pc.fresnelF0);

    // High Fresnel (grazing angle) → more reflection; low Fresnel → more refraction.
    vec3 waterColor = mix(refrColor, reflColor, fresnel);

    // ---- Deep-water colour tint ----------------------------------------------
    const vec3 kDeepWaterColor = vec3(0.02, 0.15, 0.25);
    const float kDeepTint = 0.35;
    waterColor = mix(waterColor, waterColor + kDeepWaterColor * kDeepTint, 1.0 - fresnel);

    // ---- Depth fade (shallow = transparent, deep = opaque) -------------------
    // Sample scene depth to determine how deep the water is under this fragment.
    float sceneDepth = texture(depthTex, screenUV).r; // [0, 1] depth buffer value
    // Heuristic linear depth approximation (see M37.1 notes for full inverse-projection).
    float waterDepthLinear = clamp(sceneDepth / pc.fadeDepthMeters * 10.0, 0.0, 1.0);

    // Alpha: near-shore fragments are nearly transparent; deep fragments are opaque.
    float alpha = clamp(waterDepthLinear + fresnel * 0.6, 0.0, 1.0);

    // =========================================================================
    // M37.2 — Foam (depth-based edge blending)
    // =========================================================================
    // depth = sceneDepth - waterDepth.
    // When the terrain is within foamThreshold metres of the water surface,
    // blend in the foam texture to create a shore/edge effect.
    //
    // We use sceneDepth as a proxy: a low depth value means shallow water.
    // foamWeight is 1 at depth 0 and falls to 0 at foamThreshold.
    {
        // Convert depth buffer value to a linear approximation of depth below surface.
        // sceneDepth ~0 → very shallow; sceneDepth ~1 → deep.
        float linearDepthApprox = sceneDepth * pc.fadeDepthMeters * 10.0; // metres equivalent (heuristic)

        // Foam fade: full foam at depth 0, no foam beyond foamThreshold.
        float foamWeight = 1.0 - clamp(linearDepthApprox / pc.foamThreshold, 0.0, 1.0);

        if (foamWeight > 0.0)
        {
            // Tile the foam texture using the world XZ coordinates (avoids seams from UV mapping).
            const float kFoamTiling = 4.0;
            vec2 foamUV = vWorldPos.xz * kFoamTiling * 0.01
                        + vec2(pc.normalScrollX, pc.normalScrollZ) * pc.time * 0.5;
            vec3 foamColor = texture(foamTex, foamUV).rgb;

            // Blend foam additively into the water colour at shallow edges.
            waterColor = mix(waterColor, foamColor, foamWeight * 0.8);
            // Shore edges are also more opaque.
            alpha = max(alpha, foamWeight * 0.9);
        }
    }

    // =========================================================================
    // M37.2 — Caustics (animated texture projected from sun direction)
    // =========================================================================
    // Caustics are projected onto the terrain under the water.
    // We sample an animated caustics texture using world XZ coordinates,
    // then modulate the refraction colour to simulate light patterns.
    // Per spec: "limit to shallow water" (caustics fade with depth).
    {
        // Only apply caustics where water is shallow enough to be visible.
        float causticsDepthFade = 1.0 - clamp(sceneDepth / pc.fadeDepthMeters * 5.0, 0.0, 1.0);

        if (causticsDepthFade > 0.0)
        {
            // Primary caustics UV: world XZ scaled by causticsTiling, scrolled over time.
            vec2 causUV1 = vWorldPos.xz * pc.causticsTiling * 0.01
                         + vec2(pc.causticsScroll, pc.causticsScroll * 0.7) * pc.time;
            // Secondary caustics UV: slightly different scale + direction for interference pattern.
            vec2 causUV2 = vWorldPos.xz * pc.causticsTiling * 0.008
                         + vec2(-pc.causticsScroll * 0.8, pc.causticsScroll * 1.1) * pc.time + vec2(0.5, 0.3);

            // Sample two caustics layers and multiply them together for a more complex pattern.
            vec3 caus1 = texture(causticsTex, causUV1).rgb;
            vec3 caus2 = texture(causticsTex, causUV2).rgb;
            // Combining via min (Blinn caustics approximation) produces bright focused spots.
            vec3 caustics = min(caus1, caus2) * 2.0; // boost brightness

            // Modulate refracted colour with caustics (additive contribution to lighting).
            waterColor += caustics * causticsDepthFade * 0.3 * (1.0 - fresnel);
        }
    }

    outColor = vec4(waterColor, alpha);
}
