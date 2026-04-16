#version 450
// M38.1 — Sky gradient fragment shader.
//
// Implements a Rayleigh-scattering approximation for the sky dome:
//   - Vertical gradient from zenith colour to horizon colour.
//   - Mie-scatter-like glow around the sun disk.
//   - Colours driven by the DayNightCycle CPU system via push constants.
//
// Push constants (80 bytes):
//   invViewProj  (64 bytes, mat4) — inverse view-projection for view direction recovery
//   lightDir     (12 bytes, vec3) — normalised direction toward the sun/moon
//   _pad0        ( 4 bytes)
//   zenithColor  (12 bytes, vec3) — sky colour at the zenith
//   _pad1        ( 4 bytes)
//   horizonColor (12 bytes, vec3) — sky colour at the horizon
//   _pad2        ( 4 bytes)

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform SkyPC
{
    mat4  invViewProj;    // 64 bytes
    vec3  lightDir;       // direction toward the sun/moon (normalized)
    float _pad0;
    vec3  zenithColor;    // colour at the top of the sky dome
    float _pad1;
    vec3  horizonColor;   // colour at the horizon
    float _pad2;
} pc;

void main()
{
    // ---- Reconstruct view direction from NDC --------------------------------
    // NDC is in [-1, +1]; we sample at z = 0 (near plane).
    vec4 clipPos = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
    vec4 worldPos4 = pc.invViewProj * clipPos;
    vec3 viewDir = normalize(worldPos4.xyz / worldPos4.w);

    // ---- Sky gradient via view-zenith angle ---------------------------------
    // viewDir.y is the sine of the elevation angle.
    // Map [0..1] for above-horizon to [1..0] for below horizon (clamp at horizon).
    float t = clamp(viewDir.y, 0.0, 1.0);

    // Rayleigh approximation: non-linear blend (slightly exponential).
    // pow(t, 0.6) weights the gradient toward the horizon for a wider blue band.
    float blendFactor = pow(t, 0.6);
    vec3 skyColor = mix(pc.horizonColor, pc.zenithColor, blendFactor);

    // ---- Mie-scatter-like sun glow ------------------------------------------
    // Soft glow around the sun disk; modelled as (dot^n) which approximates the
    // Henyey-Greenstein phase function used in Rayleigh/Mie scattering.
    float sunDot = max(dot(viewDir, pc.lightDir), 0.0);

    // Sun disk: tight halo.
    float sunGlow = pow(sunDot, 256.0);

    // Atmospheric halo: wide bloom around sun.
    float haloGlow = pow(sunDot, 8.0) * 0.35;

    // Sun/moon colour: white tint (actual colour is on the directional light, not the sky).
    vec3 sunContrib = vec3(1.0, 0.95, 0.85) * sunGlow
                    + pc.horizonColor       * haloGlow;

    // Only add sun glow above the horizon.
    if (viewDir.y < 0.02)
    {
        sunContrib = vec3(0.0);
    }

    // ---- Below-horizon transition -------------------------------------------
    // Below the horizon we keep a thin ground-reflection band (darkened horizon colour).
    if (viewDir.y < 0.0)
    {
        float belowT  = clamp(-viewDir.y / 0.1, 0.0, 1.0);
        skyColor = mix(skyColor, pc.horizonColor * 0.3, belowT);
    }

    // ---- Final colour --------------------------------------------------------
    outColor = vec4(skyColor + sunContrib, 1.0);
}
