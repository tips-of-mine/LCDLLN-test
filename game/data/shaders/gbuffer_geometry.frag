#version 450
// GBuffer geometry fragment shader — M03.3, M07.3 motion vectors
//
// Outputs:
//   location 0 (GBufferA)   : albedo
//   location 1 (GBufferB)   : world-space normal encoded N*0.5+0.5
//   location 2 (GBufferC)   : ORM (R=AO, G=Roughness, B=Metallic)
//   location 3 (GBufferVelocity, R16G16F) : velocity = currNDC - prevNDC (M07.3)

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vPrevClip;
layout(location = 3) in vec4 vCurrClip;

// Per-material textures (set = 0, bound by MaterialDescriptorCache).
layout(set = 0, binding = 0) uniform sampler2D uBaseColor; // sRGB, linear-filter, repeat
layout(set = 0, binding = 1) uniform sampler2D uNormal;    // linear, tangent-space normal map
layout(set = 0, binding = 2) uniform sampler2D uORM;       // linear, R=AO G=Roughness B=Metallic

layout(location = 0) out vec4 outAlbedo;   // GBufferA
layout(location = 1) out vec4 outNormal;  // GBufferB
layout(location = 2) out vec4 outORM;     // GBufferC
layout(location = 3) out vec4 outVelocity; // GBufferVelocity (M07.3, .rg = currNDC - prevNDC)

void main()
{
    // ---- BaseColor --------------------------------------------------------
    // Sampled from sRGB texture; GPU hardware linearises on read. Written to
    // sRGB attachment so the hardware re-encodes for storage. The lighting
    // pass will read it as linear (sRGB attachment auto-linearises on sample).
    outAlbedo = texture(uBaseColor, vUv);

    // ---- Normal -----------------------------------------------------------
    // Decode the tangent-space normal map from [0,1] → [-1,1].
    // Without per-vertex tangent data (MVP) we perturb the interpolated
    // vertex normal using the XY deviation from the flat default (0,0,1).
    vec3 normalSample = texture(uNormal, vUv).xyz * 2.0 - 1.0; // [-1,1]
    vec3 N = normalize(vNormal);
    // Blend: add the tangent-space XY offset projected onto the hemisphere.
    // This is a simplified approximation valid for meshes with low curvature.
    N = normalize(N + vec3(normalSample.xy, 0.0) * 0.5);
    outNormal = vec4(N * 0.5 + 0.5, 1.0);

    // ---- ORM --------------------------------------------------------------
    outORM = vec4(texture(uORM, vUv).rgb, 1.0);

    // ---- Velocity (M07.3) --------------------------------------------------
    // currNDC - prevNDC for TAA reprojection.
    vec2 prevNDC = vPrevClip.xy / max(vPrevClip.w, 1e-6);
    vec2 currNDC = vCurrClip.xy / max(vCurrClip.w, 1e-6);
    outVelocity = vec4(currNDC - prevNDC, 0.0, 1.0);
}
