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
    vec4  terrainOrigin;
    vec4  layerTiling; // x=grass, y=dirt, z=rock, w=snow (metres per tile)
} ubo;
layout(set = 0, binding = 3) uniform sampler2D      uSplatMap;    // RGBA8: R=grass,G=dirt,B=rock,A=snow
layout(set = 0, binding = 4) uniform sampler2DArray uAlbedoArray; // 4 layers RGBA8
layout(set = 0, binding = 5) uniform sampler2DArray uNormalArray; // 4 layers RGBA8 tangent-space
layout(set = 0, binding = 6) uniform sampler2DArray uORMArray;    // 4 layers RGBA8 (R=AO,G=rough,B=metal)
// M34.3: hole mask (R8_UNORM). 0.0 = hole (fragment discarded), 1.0 = solid.
layout(set = 0, binding = 7) uniform sampler2D      uHoleMask;

// ── Push constants ────────────────────────────────────────────────────────────
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
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
void main()
{
    // ── M34.3: Hole mask discard ──────────────────────────────────────────────
    // Sample the R8 hole mask. Value 0 = hole (discard), 1 = solid (keep).
    // Uses NEAREST sampler so each texel maps exactly to one terrain quad.
    float holeSolid = texture(uHoleMask, vUV).r;
    if (holeSolid < 0.5)
        discard;

    // ── Decode terrain macro normal ───────────────────────────────────────────
    vec3 macroN = normalize(texture(uNormalMap, vUV).rgb * 2.0 - 1.0);

    // ── Read splat weights and normalise (Σ = 1) ─────────────────────────────
    vec4 splat     = texture(uSplatMap, vUV);
    float totalW   = splat.r + splat.g + splat.b + splat.a;
    vec4  w        = splat / max(totalW, 1e-6);

    // ── Per-layer tiling ──────────────────────────────────────────────────────
    float tilingGrass = ubo.layerTiling.x;
    float tilingDirt  = ubo.layerTiling.y;
    float tilingRock  = ubo.layerTiling.z;
    float tilingSnow  = ubo.layerTiling.w;

    // ── Sample and blend albedo (triplanar per layer) ─────────────────────────
    vec3 albedo = vec3(0.0);
    albedo += triplanarSample(uAlbedoArray, 0.0, vWorldPos, macroN, tilingGrass).rgb * w.r;
    albedo += triplanarSample(uAlbedoArray, 1.0, vWorldPos, macroN, tilingDirt ).rgb * w.g;
    albedo += triplanarSample(uAlbedoArray, 2.0, vWorldPos, macroN, tilingRock ).rgb * w.b;
    albedo += triplanarSample(uAlbedoArray, 3.0, vWorldPos, macroN, tilingSnow ).rgb * w.a;
    outAlbedo = vec4(albedo, 1.0);

    // ── Sample and blend detail normals (triplanar per layer) ─────────────────
    // Detail normals are stored as RGB tangent-space normals (N * 0.5 + 0.5).
    vec3 detailN = vec3(0.0);
    detailN += triplanarSample(uNormalArray, 0.0, vWorldPos, macroN, tilingGrass).rgb * w.r;
    detailN += triplanarSample(uNormalArray, 1.0, vWorldPos, macroN, tilingDirt ).rgb * w.g;
    detailN += triplanarSample(uNormalArray, 2.0, vWorldPos, macroN, tilingRock ).rgb * w.b;
    detailN += triplanarSample(uNormalArray, 3.0, vWorldPos, macroN, tilingSnow ).rgb * w.a;
    // Decode blended detail normal and combine with macro normal
    vec3 detailNWorld = normalize(detailN * 2.0 - 1.0);
    vec3 blendedN     = normalize(macroN + detailNWorld);
    outNormal = vec4(blendedN * 0.5 + 0.5, 1.0);

    // ── Sample and blend ORM (triplanar per layer) ────────────────────────────
    vec4 orm = vec4(0.0);
    orm += triplanarSample(uORMArray, 0.0, vWorldPos, macroN, tilingGrass) * w.r;
    orm += triplanarSample(uORMArray, 1.0, vWorldPos, macroN, tilingDirt ) * w.g;
    orm += triplanarSample(uORMArray, 2.0, vWorldPos, macroN, tilingRock ) * w.b;
    orm += triplanarSample(uORMArray, 3.0, vWorldPos, macroN, tilingSnow ) * w.a;
    outORM = orm;

    // ── Velocity for TAA reprojection ─────────────────────────────────────────
    vec2 prevNDC = vPrevClip.xy / max(vPrevClip.w, 1e-6);
    vec2 currNDC = vCurrClip.xy / max(vCurrClip.w, 1e-6);
    outVelocity  = vec4(currNDC - prevNDC, 0.0, 1.0);
}
