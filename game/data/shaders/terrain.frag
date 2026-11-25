#version 450
// M34.2/M34.3: Terrain fragment shader — texture splatting with triplanar projection
//              + hole mask discard (M34.3).
//
// Reads per-layer weights from the splat map (R=grass, G=dirt, B=rock, A=snow),
// samples albedo/normal/ORM from 2D texture arrays using triplanar projection
// (avoids UV stretching on steep slopes), and blends by normalised splat weights.
//
// M34.3 addition: samples hole mask (R8_UNORM, binding 7). Fragments where the
// mask value < 0.5 are discarded (holes / cave entrances).
// Ticket 010: grass / surface-detail mask (R8_UNORM, binding 8), UV = splat UV.
// v1.0.123: Enfers — quand ubo.terrainOrigin.z > 0.5, le canal A (neige) est la LAVE :
//   couleur procédurale (croûte sombre + lave en fusion qui coule lentement, temps =
//   ubo.terrainOrigin.w) et émission écrite dans GBufferC.a = 1 − émission (1 = rien,
//   valeur de toutes les autres géométries) ; lighting.frag l'ajoute après l'éclairage
//   (la lave n'est pas assombrie la nuit).
// v1.0.140: rocher de l'Oracle — quand ubo.terrainOrigin.z > 1.5, le canal A est rendu en
//   CRISTAL bleu (crystalColor, facettes procédurales) là où la roche (canal B) est aussi
//   présente ; la neige pure et la pierre blanche des escaliers restent inchangées.
//
// Outputs into 4 GBuffer attachments compatible with GeometryPass layout:
//   location 0 (GBufferA)        : albedo RGBA8
//   location 1 (GBufferB)        : world normal encoded N*0.5+0.5
//   location 2 (GBufferC)        : ORM (R=AO, G=Roughness, B=Metallic)
//   location 3 (GBufferVelocity) : R16G16_SFLOAT velocity = currNDC - prevNDC

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec4 vPrevClip;
layout(location = 3) in vec4 vCurrClip;

// ── Descriptor bindings ───────────────────────────────────────────────────────
layout(set = 0, binding = 1) uniform sampler2D   uNormalMap;    // terrain macro normal (Sobel)
layout(set = 0, binding = 2) uniform TerrainFrameUbo {
    mat4  viewProj;
    mat4  prevViewProj;
    vec4  cameraPos;
    vec4  terrainParams;
    vec4  terrainOrigin; // x=originX, y=originZ, z=1 si canal A = lave (v1.0.123), w=temps (s)
    vec4  layerTiling; // x=grass, y=dirt, z=rock, w=snow (metres per tile)
    vec4  texOffsetX;  // décalage X du pavage par couche (m) : pavage en coordonnées globales
    vec4  texOffsetZ;  // décalage Z du pavage par couche (m)
} ubo;
layout(set = 0, binding = 3) uniform sampler2D      uSplatMap;    // RGBA8: R=grass,G=dirt,B=rock,A=snow
layout(set = 0, binding = 4) uniform sampler2DArray uAlbedoArray; // 4 layers RGBA8
layout(set = 0, binding = 5) uniform sampler2DArray uNormalArray; // 4 layers RGBA8 tangent-space
layout(set = 0, binding = 6) uniform sampler2DArray uORMArray;    // 4 layers RGBA8 (R=AO,G=rough,B=metal)
// M34.3: hole mask (R8_UNORM). 0.0 = hole (fragment discarded), 1.0 = solid.
layout(set = 0, binding = 7) uniform sampler2D      uHoleMask;
// 010: detail mask (herbe). 0 = pas d’effet. ubo.terrainParams.w = intensité globale (0 = désactivé).
layout(set = 0, binding = 8) uniform sampler2D      uGrassMask;

// ── Push constants ────────────────────────────────────────────────────────────
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
    int   noUserTextures; // 0 = rendu normal, 1 = orange uni (World Editor sans texture utilisateur)
} pc;

// ── GBuffer outputs ───────────────────────────────────────────────────────────
layout(location = 0) out vec4 outAlbedo;    // GBufferA
layout(location = 1) out vec4 outNormal;    // GBufferB
layout(location = 2) out vec4 outORM;       // GBufferC
layout(location = 3) out vec4 outVelocity;  // GBufferVelocity

// ─────────────────────────────────────────────────────────────────────────────
// Triplanar sampling helper
//
// Samples a texture array at the given layer index from three world-space planes
// (YZ, XZ, XY) and blends the results by the absolute value of the surface normal.
// The blend sharpness is controlled by the power applied to the blend weights.
// The UV coordinates are scaled by (1.0 / tilingMetres) so that one tile spans
// `tilingMetres` world units.
// ─────────────────────────────────────────────────────────────────────────────
vec4 triplanarSample(sampler2DArray arr, float layer,
                     vec3 worldPos, vec3 normal, float tilingMetres)
{
    float invTiling = 1.0 / max(tilingMetres, 0.1);

    // UVs for each projection plane
    vec2 uvYZ = worldPos.yz * invTiling; // sampled along X axis
    vec2 uvXZ = worldPos.xz * invTiling; // sampled along Y axis (top-down)
    vec2 uvXY = worldPos.xy * invTiling; // sampled along Z axis

    // Blend weights: absolute normal components, bias for sharper transitions
    vec3 blend = abs(normal);
    blend = max(blend - 0.2, 0.0);
    float blendSum = blend.x + blend.y + blend.z;
    blend /= max(blendSum, 1e-6);

    vec4 xSample = texture(arr, vec3(uvYZ, layer));
    vec4 ySample = texture(arr, vec3(uvXZ, layer));
    vec4 zSample = texture(arr, vec3(uvXY, layer));

    return xSample * blend.x + ySample * blend.y + zSample * blend.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// v1.0.123 — lave procédurale (Enfers)
// ─────────────────────────────────────────────────────────────────────────────
float lavaHash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float lavaNoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(lavaHash(i), lavaHash(i + vec2(1.0, 0.0)), u.x),
               mix(lavaHash(i + vec2(0.0, 1.0)), lavaHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float lavaFbm(vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int k = 0; k < 4; ++k) { s += a * lavaNoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}

// v1.0.140 — cristal du rocher de l'Oracle (T2-P044, T3-P005) : grandes facettes de
// tons bleu-gris, ardoise et violet (cellules de bruit en 3D, pas de bleu saturé ni de
// monochrome), arêtes claires. Couleur linéaire, ~ texture T_Decor_Crystal_Blue.
vec3 crystalColor(vec3 p)
{
    vec3 q = p * 0.06;
    vec3 c = floor(q);
    float f = lavaHash(c.xz + c.y * 17.31);
    float g = lavaHash(c.zy + c.x * 7.13);
    vec3 base = mix(vec3(0.075, 0.115, 0.215), vec3(0.135, 0.17, 0.29), f);
    base = mix(base, vec3(0.13, 0.12, 0.25), step(0.78, g));      // facettes violettes
    base = mix(base, vec3(0.08, 0.15, 0.19), step(0.86, f) * 0.8); // facettes sarcelle
    vec3 e = abs(fract(q) - 0.5);
    float edge = smoothstep(0.43, 0.49, max(e.x, max(e.y, e.z)));
    float n = lavaNoise(p.xz * 0.25 + p.y * 0.11);
    return mix(base * (0.85 + 0.3 * n), vec3(0.42, 0.5, 0.6), edge * 0.6);
}

// Couleur de la lave au point (m, repère de rendu) et part en fusion (0..1, émissive).
// La croûte noire se fend et dérive lentement ; la lave en fusion va du rouge au jaune.
vec3 lavaColor(vec2 xz, float t, out float molten)
{
    vec2 flow = vec2(t * 0.35, t * 0.22);
    float warp = lavaFbm(xz * 0.015 + vec2(t * 0.01));
    float n = lavaFbm(xz * 0.06 + flow * 0.06 + warp * 2.0);
    molten = smoothstep(0.42, 0.62, n);
    float heat = lavaFbm(xz * 0.2 - flow * 0.1);
    vec3 hot = mix(vec3(0.95, 0.25, 0.03), vec3(1.0, 0.72, 0.18), heat);
    vec3 crust = vec3(0.07, 0.045, 0.035);
    return mix(crust, hot, molten);
}

// ─────────────────────────────────────────────────────────────────────────────
void main()
{
    // ── M34.3: Hole mask discard ──────────────────────────────────────────────
    // Sample the R8 hole mask. Value 0 = hole (discard), 1 = solid (keep).
    // Uses NEAREST sampler so each texel maps exactly to one terrain quad.
    float holeSolid = texture(uHoleMask, vUV).r;
    if (holeSolid < 0.5)
        discard;

    // ── Decode terrain macro normal ───────────────────────────────────────────
    // Normal map générée aux SOMMETS de la heightmap : centre de texel (cf. terrain.vert).
    vec2 nmSize = vec2(textureSize(uNormalMap, 0));
    vec3 macroN = normalize(texture(uNormalMap, (vUV * (nmSize - 1.0) + 0.5) / nmSize).rgb * 2.0 - 1.0);

    // ── Read splat weights and normalise (Σ = 1) ─────────────────────────────
    vec4 splat     = texture(uSplatMap, vUV);
    float totalW   = splat.r + splat.g + splat.b + splat.a;
    vec4  w        = splat / max(totalW, 1e-6);

    // ── Per-layer tiling ──────────────────────────────────────────────────────
    float tilingGrass = ubo.layerTiling.x;
    float tilingDirt  = ubo.layerTiling.y;
    float tilingRock  = ubo.layerTiling.z;
    float tilingSnow  = ubo.layerTiling.w;
    // v1.0.123 — canal A = lave (Enfers) : couleur procédurale au lieu de la neige.
    bool  lavaMode = ubo.terrainOrigin.z > 0.5 && ubo.terrainOrigin.z < 1.5;
    // v1.0.140 — canal A = cristal bleu là où la roche est aussi présente (peinture du
    // rocher de l'Oracle : roche 0,45 + A 0,55) ; la pierre blanche des escaliers (A 0,95,
    // roche 0,05) et la neige pure restent blanches.
    bool  crystalMode = ubo.terrainOrigin.z > 1.5;
    float crystalK = crystalMode ? clamp((w.b - 0.1) * 4.0, 0.0, 1.0) : 0.0;
    vec3  crystalAlb = crystalK > 0.0 ? crystalColor(vWorldPos) : vec3(0.0);
    float molten   = 0.0;
    vec3  lavaAlb  = lavaMode ? lavaColor(vWorldPos.xz, ubo.terrainOrigin.w, molten) : vec3(0.0);

    // ── Sample and blend albedo (triplanar per layer) ─────────────────────────
    vec3 albedo = vec3(0.0);
    albedo += triplanarSample(uAlbedoArray, 0.0, vWorldPos + vec3(ubo.texOffsetX.x, 0.0, ubo.texOffsetZ.x), macroN, tilingGrass).rgb * w.r;
    albedo += triplanarSample(uAlbedoArray, 1.0, vWorldPos + vec3(ubo.texOffsetX.y, 0.0, ubo.texOffsetZ.y), macroN, tilingDirt ).rgb * w.g;
    albedo += triplanarSample(uAlbedoArray, 2.0, vWorldPos + vec3(ubo.texOffsetX.z, 0.0, ubo.texOffsetZ.z), macroN, tilingRock ).rgb * w.b;
    albedo += (lavaMode ? lavaAlb
                        : mix(triplanarSample(uAlbedoArray, 3.0, vWorldPos + vec3(ubo.texOffsetX.w, 0.0, ubo.texOffsetZ.w), macroN, tilingSnow).rgb,
                              crystalAlb, crystalK)) * w.a;
    float grassAmt = texture(uGrassMask, vUV).r * ubo.terrainParams.w;
    vec3 grassHue = vec3(0.88, 1.04, 0.82);
    albedo = mix(albedo, albedo * grassHue, clamp(grassAmt, 0.0, 1.0));
    // Fallback World Editor : orange vif quand aucune couche splat n'a de texture utilisateur.
    // Les sorties normal / ORM / velocity restent inchangees -> shading lambert
    // sur l'orange => relief lisible (orange clair cote soleil, sombre face opposee).
    if (pc.noUserTextures != 0) {
        outAlbedo = vec4(1.0, 0.55, 0.1, 1.0);
    } else {
        outAlbedo = vec4(albedo, 1.0);
    }

    // ── Sample and blend detail normals (triplanar per layer) ─────────────────
    // Detail normals are stored as RGB tangent-space normals (N * 0.5 + 0.5).
    vec3 detailN = vec3(0.0);
    detailN += triplanarSample(uNormalArray, 0.0, vWorldPos + vec3(ubo.texOffsetX.x, 0.0, ubo.texOffsetZ.x), macroN, tilingGrass).rgb * w.r;
    detailN += triplanarSample(uNormalArray, 1.0, vWorldPos + vec3(ubo.texOffsetX.y, 0.0, ubo.texOffsetZ.y), macroN, tilingDirt ).rgb * w.g;
    detailN += triplanarSample(uNormalArray, 2.0, vWorldPos + vec3(ubo.texOffsetX.z, 0.0, ubo.texOffsetZ.z), macroN, tilingRock ).rgb * w.b;
    detailN += (lavaMode ? macroN * 0.5 + 0.5
                         : triplanarSample(uNormalArray, 3.0, vWorldPos + vec3(ubo.texOffsetX.w, 0.0, ubo.texOffsetZ.w), macroN, tilingSnow).rgb) * w.a;
    // Decode blended detail normal and combine with macro normal
    vec3 detailNWorld = normalize(detailN * 2.0 - 1.0);
    vec3 blendedN     = normalize(macroN + detailNWorld);
    outNormal = vec4(blendedN * 0.5 + 0.5, 1.0);

    // ── Sample and blend ORM (triplanar per layer) ────────────────────────────
    vec4 orm = vec4(0.0);
    orm += triplanarSample(uORMArray, 0.0, vWorldPos + vec3(ubo.texOffsetX.x, 0.0, ubo.texOffsetZ.x), macroN, tilingGrass) * w.r;
    orm += triplanarSample(uORMArray, 1.0, vWorldPos + vec3(ubo.texOffsetX.y, 0.0, ubo.texOffsetZ.y), macroN, tilingDirt ) * w.g;
    orm += triplanarSample(uORMArray, 2.0, vWorldPos + vec3(ubo.texOffsetX.z, 0.0, ubo.texOffsetZ.z), macroN, tilingRock ) * w.b;
    orm += (lavaMode ? vec4(1.0, mix(0.85, 0.45, molten), 0.0, 1.0)
                     : mix(triplanarSample(uORMArray, 3.0, vWorldPos + vec3(ubo.texOffsetX.w, 0.0, ubo.texOffsetZ.w), macroN, tilingSnow),
                           vec4(0.9, 0.32, 0.0, 1.0), crystalK)) * w.a;
    // GBufferC.a = 1 − émission (1 partout sauf la lave en fusion, v1.0.123), avec une
    // lente pulsation de la lueur.
    float pulse    = 0.85 + 0.15 * sin(ubo.terrainOrigin.w * 1.7 + molten * 6.2831);
    float emissive = lavaMode ? clamp(w.a * molten * pulse, 0.0, 1.0) : 0.0;
    outORM = vec4(orm.rgb, 1.0 - emissive);

    // ── Velocity for TAA reprojection ─────────────────────────────────────────
    vec2 prevNDC = vPrevClip.xy / max(vPrevClip.w, 1e-6);
    vec2 currNDC = vCurrClip.xy / max(vCurrClip.w, 1e-6);
    outVelocity  = vec4(currNDC - prevNDC, 0.0, 1.0);
}
