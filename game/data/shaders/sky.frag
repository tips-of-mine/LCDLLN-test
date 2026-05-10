#version 450
// M38.1 + Phase 5 Lunar — Sky gradient fragment shader avec disque lunaire procedural.
//
// Implements a Rayleigh-scattering approximation for the sky dome:
//   - Vertical gradient from zenith colour to horizon colour.
//   - Mie-scatter-like glow around the sun disk.
//   - Colours driven by the DayNightCycle CPU system via push constants.
//   - Procedural moon disk with phase-driven shadow (Phase 5 Lunar).
//
// Push constants (144 bytes total):
//   invViewProj      ( 64 bytes, mat4) — inverse view-projection for view direction recovery
//   lightDir         ( 12 bytes, vec3) — normalised direction toward the sun
//   _pad0            (  4 bytes)
//   zenithColor      ( 12 bytes, vec3) — sky colour at the zenith
//   _pad1            (  4 bytes)
//   horizonColor     ( 12 bytes, vec3) — sky colour at the horizon
//   _pad2            (  4 bytes)
//   moonDir          ( 12 bytes, vec3) — direction toward the moon (normalized)
//   moonIntensity    (  4 bytes, float) — 0..1, fade jour/nuit
//   moonPhase        (  4 bytes, float) — 0..15
//   moonIllumination (  4 bytes, float) — 0..1
//   _pad3            (  8 bytes)

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform SkyPC
{
    mat4  invViewProj;       // 64 bytes
    vec3  lightDir;          // direction toward the sun/moon (normalized)
    float _pad0;
    vec3  zenithColor;       // colour at the top of the sky dome
    float _pad1;
    vec3  horizonColor;      // colour at the horizon
    float _pad2;
    vec3  moonDir;           // direction toward the moon (normalized)
    float moonIntensity;     // 0..1, fade jour/nuit
    float moonPhase;         // 0..15
    float moonIllumination;  // 0..1
    vec2  _pad3;
} pc;

// -------------------------------------------------------------------------
// RenderMoonDisk : composite procedurale d'un disque lunaire avec ombre
// dependant de la phase. Renvoie la couleur ciel modifiee (mix entre baseSky
// et la surface lunaire ponderee par halo + intensity).
//
// Algorithme :
//   1. Si moonIntensity ~= 0 (jour), retourne baseSky inchange.
//   2. Calcule cosA = dot(viewDir, moonDir). Si en dehors du cone de la lune
//      (cosA <= cos(kMoonRadius)), retourne baseSky.
//   3. Construit un repere local (right, up) sur le disque, projete viewDir
//      pour obtenir (u, v) dans le disque.
//   4. Calcule un masque d'ombre via la distance a un cercle decalle
//      d'apres l'illumination (intersection 2 cercles, classique pour
//      simuler les phases).
//   5. Pour les phases tres sombres (0..2 et 13..15), ajoute un peu
//      d'earthshine bleute sur la partie ombragee.
//   6. Mix avec un halo doux (smoothstep) entre baseSky et moonSurface.
// -------------------------------------------------------------------------
vec3 RenderMoonDisk(vec3 viewDir, vec3 baseSky)
{
    if (pc.moonIntensity < 0.001) return baseSky;
    float cosA = dot(viewDir, pc.moonDir);
    const float kMoonRadius = 0.012;
    if (cosA <= cos(kMoonRadius * 1.5)) return baseSky;

    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), pc.moonDir));
    vec3 up    = cross(pc.moonDir, right);
    float u    = dot(viewDir, right) / sin(kMoonRadius);
    float v    = dot(viewDir, up)    / sin(kMoonRadius);

    // Decalage du cercle d'ombre selon l'illumination. Le signe depend de la
    // phase : avant FullMoon (phase < 7.5) la lune croit a droite, apres elle
    // decroit a gauche.
    float shadowOffset = 1.0 - pc.moonIllumination * 2.0;
    if (pc.moonPhase < 7.5) shadowOffset = -shadowOffset;

    float distFromShadow = length(vec2(u - shadowOffset, v));
    float shadowMask     = smoothstep(1.0, 0.95, distFromShadow);

    vec3 moonSurface = vec3(0.95, 0.92, 0.85) * shadowMask;

    // Earthshine sur les phases tres sombres (0..2 et 13..15) : bleute discret
    // sur la partie ombragee.
    if (pc.moonPhase < 3.0 || pc.moonPhase > 13.0)
    {
        moonSurface += vec3(0.05, 0.06, 0.10) * (1.0 - shadowMask);
    }

    // Halo doux : transition smoothstep entre baseSky et moonSurface.
    float haloFalloff = smoothstep(cos(kMoonRadius * 1.5), cos(kMoonRadius), cosA);
    return mix(baseSky, moonSurface * pc.moonIntensity, haloFalloff);
}

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

    // ---- Phase 5 Lunar — disque lunaire procedural ---------------------------
    // Compose le disque lunaire (avec ombre dependant de la phase) sur la
    // couleur ciel courante. Si moonIntensity == 0 (jour), no-op.
    skyColor = RenderMoonDisk(viewDir, skyColor);

    // ---- Final colour --------------------------------------------------------
    outColor = vec4(skyColor + sunContrib, 1.0);
}
