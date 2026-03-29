#version 450
// M34.1: Terrain vertex shader — heightmap displacement + geomorphing.
//
// Vertex input:
//   location 0: vec2 inPatchLocal  — local XZ position in [0, kPatchQuads] (raw grid index)
//
// Descriptor set 0:
//   binding 0: sampler2D uHeightmap  (R16_UNORM)
//   binding 1: sampler2D uNormalMap  (RGBA8_UNORM, unused in vert)
//   binding 2: TerrainFrameUbo
//
// Push constants (16 bytes):
//   vec2  patchOriginXZ  — world XZ of patch corner
//   float morphFactor    — geomorphing blend [0=currentLOD, 1=parentLOD]
//   int   lodLevel       — current LOD index (0..4)

layout(location = 0) in vec2 inPatchLocal;

// Per-frame uniform buffer
layout(set = 0, binding = 0) uniform sampler2D uHeightmap;
layout(set = 0, binding = 1) uniform sampler2D uNormalMap; // declared for set completeness

layout(set = 0, binding = 2) uniform TerrainFrameUbo {
    mat4  viewProj;       // current frame view-projection
    mat4  prevViewProj;   // previous frame view-projection (for TAA velocity)
    vec4  cameraPos;      // world camera position (xyz, w=unused)
    vec4  terrainParams;  // x=terrainSize, y=heightScale, z=vertStepWorld, w=unused
    vec4  terrainOrigin;  // x=originX, y=originZ, z=unused, w=unused
} ubo;

layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
} pc;

// Outputs to fragment shader
layout(location = 0) out vec2 vUV;        // heightmap / normalmap UV in [0,1]
layout(location = 1) out vec3 vWorldPos;  // world position (for lighting)
layout(location = 2) out vec4 vPrevClip;  // previous clip position (for TAA)
layout(location = 3) out vec4 vCurrClip;  // current clip position (for TAA)

void main()
{
    // Step multiplier: LOD N uses 2^N world units per local grid unit
    float stepMult = float(1 << pc.lodLevel);

    // Unpack terrain parameters from vec4 fields
    float terrainSize    = ubo.terrainParams.x;
    float heightScale    = ubo.terrainParams.y;
    float vertStepWorld  = ubo.terrainParams.z;
    float terrainOriginX = ubo.terrainOrigin.x;
    float terrainOriginZ = ubo.terrainOrigin.y;

    // World XZ position of this vertex
    float worldX = pc.patchOriginX + inPatchLocal.x * stepMult * vertStepWorld;
    float worldZ = pc.patchOriginZ + inPatchLocal.y * stepMult * vertStepWorld;

    // Heightmap UV (clamp to [0,1])
    vec2 uv;
    uv.x = clamp((worldX - terrainOriginX) / terrainSize, 0.0, 1.0);
    uv.y = clamp((worldZ - terrainOriginZ) / terrainSize, 0.0, 1.0);

    // Sample height at current vertex position
    float h      = texture(uHeightmap, uv).r;
    float worldY = h * heightScale;

    // Geomorphing: smoothly snap to parent LOD grid when morphFactor > 0.
    // Parent LOD step = 2 × current step. Vertices not on the parent grid
    // morph towards their snapped parent position.
    if (pc.morphFactor > 0.0 && pc.lodLevel < 4)
    {
        // Snap local position to nearest parent LOD grid vertex
        float snappedLocalX = floor(inPatchLocal.x / 2.0 + 0.5) * 2.0;
        float snappedLocalZ = floor(inPatchLocal.y / 2.0 + 0.5) * 2.0;

        float snappedWorldX = pc.patchOriginX + snappedLocalX * stepMult * 2.0 * vertStepWorld;
        float snappedWorldZ = pc.patchOriginZ + snappedLocalZ * stepMult * 2.0 * vertStepWorld;

        vec2 snappedUV;
        snappedUV.x = clamp((snappedWorldX - terrainOriginX) / terrainSize, 0.0, 1.0);
        snappedUV.y = clamp((snappedWorldZ - terrainOriginZ) / terrainSize, 0.0, 1.0);

        float snappedH = texture(uHeightmap, snappedUV).r;
        float snappedY = snappedH * heightScale;

        // Blend current height towards snapped parent height
        worldY = mix(worldY, snappedY, pc.morphFactor);
    }

    vec3 worldPos = vec3(worldX, worldY, worldZ);

    vUV       = uv;
    vWorldPos = worldPos;
    vCurrClip = ubo.viewProj     * vec4(worldPos, 1.0);
    vPrevClip = ubo.prevViewProj * vec4(worldPos, 1.0);

    gl_Position = vCurrClip;
}
