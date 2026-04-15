#version 450
// M38.1 — Sky gradient fragment shader.
// Implements a Rayleigh-scattering approximation for the sky dome.
// Outputs sky colour; rendered at far-plane depth so it only shows where no
// geometry was drawn.

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inViewRay;

layout(push_constant) uniform PushConstants
{
    mat4  invViewProj;
    vec4  cameraPos;
    vec4  sunDirection;   // xyz: normalised toward-sun.
    vec4  skyZenith;      // xyz: zenith sky colour.
    vec4  skyHorizon;     // xyz: horizon sky colour.
    float sunElevation;   // sin(sunAngle), −1..+1.
    float timeHours;      // 0..24 h.
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) out vec4 outColor;

// ── Rayleigh scattering approximation ──────────────────────────────────────
// Simplified single-scattering model:
// - At horizon (low elevation angles) scattering is stronger → warmer/hazier.
// - At zenith scattering is weak → pure sky colour.
float RayleighPhase(float cosTheta)
{
    return 0.75 * (1.0 + cosTheta * cosTheta);
}

// ── Sun disc ────────────────────────────────────────────────────────────────
vec3 SunDisc(vec3 rayDir, vec3 sunDir)
{
    float cosAngle = dot(rayDir, sunDir);
    float disc     = smoothstep(0.9996, 0.9999, cosAngle); // Tight angular disc
    // Sun disc is white-yellow at noon, orange at horizon
    float elevT = clamp(pc.sunElevation * 2.0 + 0.5, 0.0, 1.0);
    vec3  discColor = mix(vec3(1.0, 0.5, 0.1), vec3(1.0, 0.98, 0.80), elevT);
    return discColor * disc * 3.0; // HDR brightness
}

// ── Moon disc (simplified, opposite to sun) ─────────────────────────────────
vec3 MoonDisc(vec3 rayDir, vec3 sunDir)
{
    vec3  moonDir  = -sunDir;
    float cosAngle = dot(rayDir, moonDir);
    float disc     = smoothstep(0.9990, 0.9995, cosAngle);
    return vec3(0.85, 0.88, 0.95) * disc * 0.8;
}

void main()
{
    vec3 rayDir = normalize(inViewRay);

    // ── Horizon blend factor (how close to the horizon the ray is) ──────────
    float horizonY  = clamp(rayDir.y, 0.0, 1.0);
    float zenithT   = pow(horizonY, 0.5); // nonlinear — more sky colour near zenith

    // ── Sky gradient (zenith → horizon) ─────────────────────────────────────
    vec3 skyColor = mix(pc.skyHorizon.xyz, pc.skyZenith.xyz, zenithT);

    // ── Rayleigh tint: scatter sun colour into horizon ───────────────────────
    vec3  sunDir     = normalize(pc.sunDirection.xyz);
    float cosSunUp   = dot(sunDir, vec3(0.0, 1.0, 0.0));  // how high sun is
    float cosRay     = dot(rayDir, sunDir);
    float rayleigh   = RayleighPhase(cosRay) * 0.15;

    // Atmospheric haze colour depends on time of day
    float elevT    = clamp(pc.sunElevation * 2.0 + 0.5, 0.0, 1.0);
    vec3  hazeColor = mix(vec3(0.95, 0.35, 0.05), vec3(0.70, 0.82, 0.95), elevT);

    // Apply Rayleigh near the horizon
    float horizonMix = clamp(1.0 - horizonY * 3.0, 0.0, 1.0);
    skyColor = mix(skyColor, hazeColor, rayleigh * horizonMix * clamp(cosSunUp + 0.4, 0.0, 1.0));

    // ── Sun and moon discs ───────────────────────────────────────────────────
    if (pc.sunElevation >= -0.1)
        skyColor += SunDisc(rayDir, sunDir);
    else
        skyColor += MoonDisc(rayDir, sunDir);

    // ── Stars at night (simple noise approximation) ─────────────────────────
    float nightFactor = clamp(-pc.sunElevation * 2.0, 0.0, 1.0);
    if (nightFactor > 0.0 && horizonY > 0.1)
    {
        // Hash-based star field
        vec3 starDir   = floor(rayDir * 200.0);
        float starHash = fract(sin(dot(starDir, vec3(12.9898, 78.233, 45.543))) * 43758.5453);
        float star     = step(0.998, starHash) * nightFactor * clamp(horizonY * 4.0, 0.0, 1.0);
        skyColor += vec3(star * 0.8);
    }

    outColor = vec4(skyColor, 1.0);
}
