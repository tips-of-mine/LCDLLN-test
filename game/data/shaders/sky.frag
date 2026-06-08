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
    vec4  cameraPos;         // xyz = position monde caméra (reconstruction viewDir)
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
    const float kMoonRadius = 0.020; // un peu plus gros pour être repérable
    if (cosA <= cos(kMoonRadius * 4.0)) return baseSky; // marge pour le halo nocturne

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

    // Wave 4 polish — texture surface lunaire procedurale (no asset needed).
    // V1 simple : value-noise sparse pour simuler des cratere-spots, plus une
    // tache plus large simulant un Mare (ex. Mare Tranquillitatis). La
    // multiplication garde la tonalite generale et assombrit ponctuellement
    // pour donner du relief visuel. Une PR future pourra remplacer par une
    // vraie texture lunaire (cube ou sphere mapping). On applique uniquement
    // si on est dans la zone illuminee (shadowMask > epsilon) pour ne pas
    // crepiter la zone sombre.
    if (shadowMask > 0.01)
    {
        // Value-noise simple a partir des coords (u, v) du disque.
        float craterNoise = fract(sin(dot(vec2(u, v), vec2(12.9898, 78.233))) * 43758.5453);
        // smoothstep(0.85, 0.95) -> ~10% des points donnent un cratere visible.
        craterNoise = smoothstep(0.85, 0.95, craterNoise);
        moonSurface *= 1.0 - craterNoise * 0.25;  // assombrissement 25% ponctuel.

        // Cratere "Mare" plus grand a (u=0.2, v=0.1) — ombre douce ~15%.
        float distFromMare = length(vec2(u - 0.2, v - 0.1));
        float mareMask     = smoothstep(0.4, 0.3, distFromMare);
        moonSurface *= 1.0 - mareMask * 0.15;
    }

    // Earthshine sur les phases tres sombres (0..2 et 13..15) : bleute discret
    // sur la partie ombragee.
    if (pc.moonPhase < 3.0 || pc.moonPhase > 13.0)
    {
        moonSurface += vec3(0.05, 0.06, 0.10) * (1.0 - shadowMask);
    }

    // Disque + halo serré : transition smoothstep entre baseSky et moonSurface.
    float haloFalloff = smoothstep(cos(kMoonRadius * 1.5), cos(kMoonRadius), cosA);
    vec3 withDisk = mix(baseSky, moonSurface * pc.moonIntensity, haloFalloff);

    // Halo nocturne large bleuté autour de la lune -> repérable dans le ciel sombre.
    float glow = smoothstep(cos(kMoonRadius * 4.0), cos(kMoonRadius * 1.2), cosA) * 0.18;
    return withDisk + vec3(0.55, 0.60, 0.75) * glow * pc.moonIntensity;
}

void main()
{
    // ---- Reconstruct view direction from NDC --------------------------------
    // On échantillonne le PLAN LOINTAIN (z=1.0 en NDC Vulkan) puis on soustrait
    // la position caméra pour obtenir un VRAI rayon de vue monde. Échantillonner
    // le plan proche (z=0.0) sans soustraire la caméra donnait, dès que la caméra
    // n'était pas à l'origine, un viewDir quasi constant sur tout l'écran → ciel
    // uniforme et soleil/lune invisibles (les disques ne se localisaient nulle part).
    vec4 clipPos   = vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldPos4 = pc.invViewProj * clipPos;
    vec3 worldFar  = worldPos4.xyz / worldPos4.w;
    vec3 viewDir   = normalize(worldFar - pc.cameraPos.xyz);

    // ---- Sky gradient via view-zenith angle ---------------------------------
    // viewDir.y is the sine of the elevation angle.
    // Map [0..1] for above-horizon to [1..0] for below horizon (clamp at horizon).
    float t = clamp(viewDir.y, 0.0, 1.0);

    // Rayleigh approximation: non-linear blend (slightly exponential).
    // pow(t, 0.6) weights the gradient toward the horizon for a wider blue band.
    float blendFactor = pow(t, 0.6);
    vec3 skyColor = mix(pc.horizonColor, pc.zenithColor, blendFactor);

    // ---- Disque solaire NET + couronne + halo atmosphérique -----------------
    // Disque circulaire à bord net (au lieu d'un blob mou), couleur chaude qui
    // vire à l'orange quand le soleil est bas (lever/coucher) -> soleil reconnaissable.
    float sunDot = max(dot(viewDir, pc.lightDir), 0.0);
    float sunAng = acos(clamp(sunDot, -1.0, 1.0)); // angle au centre du soleil (rad)

    const float kSunRadius = 0.045;                       // ~2.5°, nettement visible
    float sunDisk  = smoothstep(kSunRadius, kSunRadius * 0.8, sunAng); // disque net
    float corona   = pow(sunDot, 90.0) * 0.9;             // couronne large
    float haloGlow = pow(sunDot, 7.0)  * 0.40;            // bloom atmosphérique

    // Élévation du soleil (lightDir.y) : bas -> orange, haut -> jaune-blanc chaud.
    float sunElev  = clamp(pc.lightDir.y, 0.0, 1.0);
    vec3  sunCore  = mix(vec3(1.0, 0.45, 0.15), vec3(1.0, 0.92, 0.72), sunElev);
    vec3  haloCol  = mix(vec3(1.0, 0.40, 0.15), pc.horizonColor, sunElev);
    // Disque sur-lumineux (HDR > 1) -> ressort du ciel et alimente le bloom.
    vec3  sunContrib = sunCore * (sunDisk * 2.5 + corona) + haloCol * haloGlow;

    // Pas de soleil quand il est sous l'horizon.
    if (pc.lightDir.y < -0.02)
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
