#version 450
// M34.3: Terrain cliff fragment shader.
//
// Writes cliff geometry into the GBuffer (same layout as GeometryPass).
// Outputs 4 attachments:
//   location 0 (GBufferA)        : albedo RGBA8   (cliff rock placeholder)
//   location 1 (GBufferB)        : world normal   (N * 0.5 + 0.5)
//   location 2 (GBufferC)        : ORM            (R=AO, G=Roughness, B=Metallic)
//   location 3 (GBufferVelocity) : R16G16 velocity = currNDC - prevNDC
//
// blendWeight at 1.0 indicates a border vertex that matches the heightmap edge.
// No alpha discard is applied here; cliff meshes are fully opaque solid geometry.

layout(location = 0) in vec3  vWorldPos;
layout(location = 1) in vec3  vWorldNormal;
layout(location = 2) in vec2  vUV;
layout(location = 3) in float vBlendWeight;
layout(location = 4) in vec4  vPrevClip;
layout(location = 5) in vec4  vCurrClip;

// ── Descriptor bindings ───────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform CliffFrameUbo {
    mat4  viewProj;
    mat4  prevViewProj;
} ubo;

// binding 1: cliff albedo texture (placeholder solid RGBA8).
// If absent the shader falls back to the hard-coded rock colour below.
layout(set = 0, binding = 1) uniform sampler2D uCliffAlbedo;

// ── GBuffer outputs ───────────────────────────────────────────────────────────
layout(location = 0) out vec4 outAlbedo;   // GBufferA
layout(location = 1) out vec4 outNormal;   // GBufferB
layout(location = 2) out vec4 outORM;      // GBufferC
layout(location = 3) out vec4 outVelocity; // GBufferVelocity

void main()
{
    // ── Albedo: sample cliff texture, tiling on UV ────────────────────────────
    // UV tiling: 1 tile per 4 world units (cliffs typically 4–16 m tall).
    vec2  tiledUV = vUV * 4.0;
    vec3  albedo  = texture(uCliffAlbedo, tiledUV).rgb;

    // Blend towards a neutral grey at terrain-junction edges (blendWeight = 1.0).
    // This softens the seam where the cliff base meets the heightmap surface.
    const vec3 edgeColor = vec3(0.45, 0.42, 0.40); // grey-brown blend
    albedo = mix(albedo, edgeColor, vBlendWeight * 0.5);

    outAlbedo = vec4(albedo, 1.0);

    // ── Normal: encode world-space normal ────────────────────────────────────
    vec3 n = normalize(vWorldNormal);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);

    // ── ORM: rock-like default (AO=1, roughness=0.85, metallic=0) ────────────
    outORM = vec4(1.0, 0.85, 0.0, 1.0);

    // ── Velocity for TAA reprojection ─────────────────────────────────────────
    vec2 prevNDC = vPrevClip.xy / max(vPrevClip.w, 1e-6);
    vec2 currNDC = vCurrClip.xy / max(vCurrClip.w, 1e-6);
    outVelocity  = vec4(currNDC - prevNDC, 0.0, 1.0);
}
