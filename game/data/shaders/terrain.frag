#version 450
// M34.1: Terrain fragment shader — GBuffer output.
//
// Reads world-space normal from the pre-baked normal map (Sobel-filtered heightmap).
// Outputs into 4 GBuffer attachments compatible with GeometryPass layout:
//   location 0 (GBufferA)        : albedo RGBA8
//   location 1 (GBufferB)        : world normal encoded N*0.5+0.5
//   location 2 (GBufferC)        : ORM (R=AO, G=Roughness, B=Metallic)
//   location 3 (GBufferVelocity) : R16G16_SFLOAT velocity = currNDC - prevNDC

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec4 vPrevClip;
layout(location = 3) in vec4 vCurrClip;

layout(set = 0, binding = 1) uniform sampler2D uNormalMap;

// Push constant — lodLevel reused for debug tinting (optional)
layout(push_constant) uniform PC {
    float patchOriginX;
    float patchOriginZ;
    float morphFactor;
    int   lodLevel;
} pc;

layout(location = 0) out vec4 outAlbedo;    // GBufferA
layout(location = 1) out vec4 outNormal;    // GBufferB
layout(location = 2) out vec4 outORM;       // GBufferC
layout(location = 3) out vec4 outVelocity;  // GBufferVelocity

void main()
{
    // ── Albedo — simple height-based colour (dirt/grass) ──────────────────────
    // Normalised height derived from world Y; terrain height range is [0, heightScale].
    float heightNorm = clamp(vWorldPos.y * 0.005, 0.0, 1.0);
    vec3 grassColour = vec3(0.35, 0.55, 0.20);
    vec3 rockColour  = vec3(0.50, 0.42, 0.32);
    vec3 albedo      = mix(grassColour, rockColour, heightNorm);
    outAlbedo        = vec4(albedo, 1.0);

    // ── Normal — decode pre-baked Sobel normal map ────────────────────────────
    vec3 N = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;
    N = normalize(N);
    outNormal = vec4(N * 0.5 + 0.5, 1.0);

    // ── ORM — no pre-baked AO, moderate roughness, non-metallic ─────────────
    outORM = vec4(1.0,   // AO = 1 (no occlusion)
                  0.85,  // Roughness = 0.85 (rough terrain)
                  0.0,   // Metallic  = 0
                  1.0);

    // ── Velocity for TAA reprojection ─────────────────────────────────────────
    vec2 prevNDC = vPrevClip.xy / max(vPrevClip.w, 1e-6);
    vec2 currNDC = vCurrClip.xy / max(vCurrClip.w, 1e-6);
    outVelocity  = vec4(currNDC - prevNDC, 0.0, 1.0);
}
