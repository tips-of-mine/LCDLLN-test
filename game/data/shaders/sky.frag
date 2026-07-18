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
//   skyModel         (  4 bytes, float) — 0=dégradé legacy, 1=analytique (2026-07-17)
//   _pad3            (  4 bytes)

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
    float skyModel;          // 0 = dégradé legacy ; 1 = diffusion analytique (2026-07-17)
    float _pad3;
    vec4  cameraPos;         // xyz = position monde caméra (reconstruction viewDir)
} pc;

// -------------------------------------------------------------------------
// Ciel analytique (chantier ciel 2026-07-17) — diffusion simple Rayleigh +
// Mie : ray-march léger (8 échantillons vue × 4 vers le soleil) dans une
// atmosphère exponentielle. Constantes physiques standard (Terre). Donne un
// zénith bleu profond, un horizon blanchi, des levers/couchers orangés et
// une nuit qui tombe naturellement — là où le dégradé 2 couleurs restait
// plat. Sélectionné par pc.skyModel (config client.sky.analytic).
// -------------------------------------------------------------------------
const float kPi       = 3.14159265358979;
const float kPlanetR  = 6360.0e3;               // rayon planète (m)
const float kAtmoR    = 6420.0e3;               // sommet de l'atmosphère (m)
const vec3  kBetaR    = vec3(5.8e-6, 13.5e-6, 33.1e-6); // diffusion Rayleigh
const float kBetaM    = 21.0e-6;                // diffusion Mie
const float kHR       = 8000.0;                 // hauteur d'échelle Rayleigh (m)
const float kHM       = 1200.0;                 // hauteur d'échelle Mie (m)

// Distance de o (relatif au centre planète) à la sphère de rayon r le long
// de d ; négatif si pas d'intersection avant.
float RaySphereFar(vec3 o, vec3 d, float r)
{
    float b = dot(o, d);
    float c = dot(o, o) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}

vec3 AnalyticSky(vec3 viewDir, vec3 sunDir)
{
    // Caméra posée près du sol : l'altitude jeu (dizaines de mètres) est
    // négligeable devant les hauteurs d'échelle — on fige +200 m.
    vec3 o = vec3(0.0, kPlanetR + 200.0, 0.0);
    // Sous l'horizon on borne le rayon vers le haut : le sol du jeu couvre
    // ces pixels (le ciel n'y est visible qu'en bordure de monde).
    vec3 d = normalize(vec3(viewDir.x, max(viewDir.y, -0.03), viewDir.z));
    float tMax = RaySphereFar(o, d, kAtmoR);
    if (tMax <= 0.0) return vec3(0.0);

    float mu = dot(d, sunDir);
    float phaseR = 3.0 / (16.0 * kPi) * (1.0 + mu * mu);
    const float g = 0.76; // anisotropie Mie (halo autour du soleil)
    float phaseM = 3.0 / (8.0 * kPi) * ((1.0 - g * g) * (1.0 + mu * mu))
        / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

    const int kSteps      = 8;
    const int kLightSteps = 4;
    float dt = tMax / float(kSteps);
    float t  = 0.5 * dt;
    vec3  sumR = vec3(0.0);
    vec3  sumM = vec3(0.0);
    float odR = 0.0; // épaisseur optique cumulée le long de la vue
    float odM = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        vec3 p = o + d * t;
        float h = max(length(p) - kPlanetR, 0.0);
        float dR = exp(-h / kHR) * dt;
        float dM = exp(-h / kHM) * dt;
        odR += dR;
        odM += dM;

        // Épaisseur optique vers le soleil (4 échantillons) ; si le rayon
        // soleil traverse le sol, l'échantillon est dans l'ombre de la
        // planète (crépuscule) et ne contribue pas.
        float tSun = RaySphereFar(p, sunDir, kAtmoR);
        float sdt = tSun / float(kLightSteps);
        float st = 0.5 * sdt;
        float sOdR = 0.0;
        float sOdM = 0.0;
        bool inShadow = (tSun <= 0.0);
        for (int j = 0; j < kLightSteps && !inShadow; ++j)
        {
            vec3 sp = p + sunDir * st;
            float sh = length(sp) - kPlanetR;
            if (sh < 0.0) { inShadow = true; break; }
            sOdR += exp(-sh / kHR) * sdt;
            sOdM += exp(-sh / kHM) * sdt;
            st += sdt;
        }
        if (!inShadow)
        {
            vec3 tau = kBetaR * (odR + sOdR) + vec3(kBetaM * 1.1) * (odM + sOdM);
            vec3 att = exp(-tau);
            sumR += att * dR;
            sumM += att * dM;
        }
        t += dt;
    }

    const float kSunIntensity = 20.0;
    vec3 col = kSunIntensity * (sumR * kBetaR * phaseR + sumM * vec3(kBetaM) * phaseM);
    // Plancher nocturne : jamais le noir absolu (la lune et le halo restent
    // composés par-dessus, cf. RenderMoonDisk).
    return max(col, vec3(0.0015, 0.002, 0.0045));
}

// -------------------------------------------------------------------------
// Étoiles nocturnes procédurales (chantier ciel 2026-07-18, lot 3).
// Champ d'étoiles par cellules 3D sur la direction de vue : chaque cellule
// tire (hash) une étoile potentielle (~3 % des cellules), avec position,
// magnitude et teinte propres. Fondu piloté par l'élévation du soleil ;
// la lune est composée PAR-DESSUS (RenderMoonDisk appelé après).
// -------------------------------------------------------------------------
float StarHash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 Stars(vec3 viewDir, float nightFactor)
{
    if (nightFactor <= 0.001 || viewDir.y < -0.05) return vec3(0.0);
    // Échelle angulaire : ~220 cellules par unité de direction (étoiles
    // ponctuelles d'environ un quart de degré).
    vec3 sp = viewDir * 220.0;
    vec3 cell = floor(sp);
    float h = StarHash13(cell);
    if (h < 0.97) return vec3(0.0); // ~3 % des cellules sont étoilées

    // Position de l'étoile dans sa cellule (décalage stable par hash).
    vec3 starPos = cell + 0.5 + 0.6 * (vec3(
        StarHash13(cell + 1.7),
        StarHash13(cell + 4.2),
        StarHash13(cell + 9.1)) - 0.5);
    float d = length(sp - starPos);
    float star = smoothstep(0.7, 0.0, d);

    // Magnitude et teinte variables (blanc bleuté -> blanc chaud).
    float mag  = 0.35 + 0.65 * StarHash13(cell + 2.3);
    float tint = StarHash13(cell + 5.5);
    vec3 col = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.92, 0.80), tint);
    return col * star * mag * 0.6 * nightFactor;
}

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

    // ---- Fond de ciel : analytique OU dégradé legacy ------------------------
    // Chantier ciel 2026-07-17 : pc.skyModel sélectionne la diffusion
    // Rayleigh+Mie (défaut) ou l'ancien dégradé 2 couleurs (repli via
    // config client.sky.analytic=false, zéro rebuild).
    vec3 skyColor;
    if (pc.skyModel > 0.5)
    {
        skyColor = AnalyticSky(viewDir, normalize(pc.lightDir));
    }
    else
    {
        // Dégradé legacy via l'angle vue-zénith. viewDir.y = sinus de
        // l'élévation ; pow(t, 0.6) élargit la bande d'horizon.
        float t = clamp(viewDir.y, 0.0, 1.0);
        float blendFactor = pow(t, 0.6);
        skyColor = mix(pc.horizonColor, pc.zenithColor, blendFactor);
    }

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

    // ---- Étoiles nocturnes (lot 3) — sous la lune, fondu au crépuscule ------
    float nightFactor = smoothstep(0.0, -0.12, pc.lightDir.y);
    skyColor += Stars(viewDir, nightFactor);

    // ---- Phase 5 Lunar — disque lunaire procedural ---------------------------
    // Compose le disque lunaire (avec ombre dependant de la phase) sur la
    // couleur ciel courante. Si moonIntensity == 0 (jour), no-op.
    skyColor = RenderMoonDisk(viewDir, skyColor);

    // ---- Final colour --------------------------------------------------------
    outColor = vec4(skyColor + sunContrib, 1.0);
}
