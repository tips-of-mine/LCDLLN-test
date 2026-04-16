#version 450

// M37.3 — Underwater post-effect fragment shader.
//
// Applied as a fullscreen post-process when the camera is below the water plane.
// Implements (per spec):
//   1. Blue colour tint:       color *= vec3(0.5, 0.7, 1.0)
//   2. Exponential depth fog:  fogFactor = 1 - exp(-density * linearDepth)
//                              color = mix(color, fogColor, fogFactor * underwaterFactor)
//   3. Slight Gaussian blur vignette (simulates light diffusion underwater).
//
// The `underwaterFactor` push-constant controls overall effect intensity:
//   0.0 → pure pass-through (no effect at all)
//   1.0 → full underwater effects
// This allows smooth CPU-side transitions as the camera crosses the water surface.
//
// Descriptor set 0:
//   binding 0: sampler2D  sceneColorHDR  — R16G16B16A16_SFLOAT scene colour
//   binding 1: sampler2D  depthTex       — D32_SFLOAT depth (sampled as .r in [0,1])
//
// Push constants (16 bytes):
//   float underwaterFactor  — blend weight [0, 1]
//   float fogDensity        — exponential fog density (spec: 0.05)
//   float nearZ             — camera near clip (for depth linearisation)
//   float farZ              — camera far clip  (for depth linearisation)

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D sceneColorHDR;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform PC
{
    float underwaterFactor; ///< 0 = no effect, 1 = full underwater FX
    float fogDensity;       ///< exponential fog density (default 0.05)
    float nearZ;            ///< camera near clip plane (metres)
    float farZ;             ///< camera far clip plane  (metres)
} pc;

layout(location = 0) out vec4 outColor;

// ---- Linearise Vulkan depth buffer value ----------------------------------
// Vulkan uses [0, 1] depth where 0 = near and 1 = far (standard, non-reversed).
// Returns linear view-space depth in [nearZ, farZ].
float LineariseDepth(float depth)
{
    // Standard perspective linearisation.
    float near = pc.nearZ;
    float far  = pc.farZ;
    return (near * far) / (far - depth * (far - near));
}

void main()
{
    // ---- Pass-through when above water (underwaterFactor == 0) ---------------
    if (pc.underwaterFactor <= 0.0)
    {
        outColor = texture(sceneColorHDR, inUV);
        return;
    }

    // ---- Optional slight blur (simulates underwater light diffusion) ----------
    // 5-tap box blur with radius proportional to underwaterFactor.
    // radius = 1 texel at full underwater strength.
    vec2 texelSize = 1.0 / vec2(textureSize(sceneColorHDR, 0));
    float blurRadius = pc.underwaterFactor; // 1.0 texel max; very subtle

    vec3 color  = texture(sceneColorHDR, inUV).rgb;
    color      += texture(sceneColorHDR, inUV + vec2( blurRadius,  0.0) * texelSize).rgb;
    color      += texture(sceneColorHDR, vec2(inUV.x - blurRadius * texelSize.x, inUV.y)).rgb;
    color      += texture(sceneColorHDR, vec2(inUV.x, inUV.y + blurRadius * texelSize.y)).rgb;
    color      += texture(sceneColorHDR, vec2(inUV.x, inUV.y - blurRadius * texelSize.y)).rgb;
    color /= 5.0; // normalise box blur

    // ---- Blue colour tint (spec: color *= vec3(0.5, 0.7, 1.0)) --------------
    // Blended by underwaterFactor so the tint fades in gradually.
    const vec3 kUnderwaterTint = vec3(0.5, 0.7, 1.0);
    vec3 tinted = color * mix(vec3(1.0), kUnderwaterTint, pc.underwaterFactor);

    // ---- Exponential depth-based fog (spec: exp(-density * depth)) -----------
    // Reconstruct linear view-space depth from the depth buffer.
    float rawDepth    = texture(depthTex, inUV).r;
    float linearDepth = LineariseDepth(rawDepth);

    // fogFactor: 0 at the camera, approaches 1 as depth increases.
    float fogFactor = 1.0 - exp(-pc.fogDensity * linearDepth);
    fogFactor       = clamp(fogFactor * pc.underwaterFactor, 0.0, 1.0);

    // Underwater fog colour: dark deep blue matching the water colour.
    const vec3 kFogColor = vec3(0.0, 0.08, 0.18);
    vec3 foggedColor = mix(tinted, kFogColor, fogFactor);

    outColor = vec4(foggedColor, 1.0);
}
