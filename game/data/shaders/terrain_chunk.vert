// game/data/shaders/terrain_chunk.vert (M100.9)
// Vertex shader du pipeline `terrain_chunk` (M100.9). Distinct du
// `terrain.vert` legacy (M34, single-zone splat 4-layer SLAP) qui sert
// `demo_plains`. Le pipeline terrain_chunk dessine un chunk monde 257²
// avec splat 8-layer SLAT.
//
// Vertex layout : TerrainVertex 32 octets (M100.5)
//   0..11  : float3 position chunk-local (m)
//   12..23 : float3 normale (gradient bilinéaire)
//   24..31 : float2 UV (0..1 sur le chunk)
//
// Sortie : worldPos (pour tiling individuel par layer dans le frag),
// normale (pour le blend du gbuffer normal), UV (pour sampling splatMap).
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} u_camera;

// Push constant : origine du chunk en monde (m). Aligné sur vec4 (16 octets).
layout(push_constant) uniform PushConstants {
    vec3 chunkOriginWorld;
    float pad0;
} u_chunk;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_worldPos;

void main()
{
    vec3 worldPos = u_chunk.chunkOriginWorld + inPosition;
    v_worldPos = worldPos;
    v_normal = inNormal;
    v_uv = inUV;
    gl_Position = u_camera.viewProj * vec4(worldPos, 1.0);
}
