#version 450
// M37.1 — Water plane vertex shader.
// M37.2 — Added shore wave vertex displacement (sine wave on Y axis).
//
// Transforms a flat water grid vertex to clip space and computes:
//   - Screen-space UV for projective reflection/refraction sampling.
//   - World-space position for fragment-side Fresnel and depth-fade.
//   - Normal-map UV (tiled + time-scrolled) for ripple distortion.
//   - M37.2: Shore wave Y displacement: Y += waveAmplitude * sin(time * waveFreq + posX * waveFreq)
//
// Vertex input (stride = 20 bytes):
//   location 0: vec3  inPosition  — world XZ position on the water plane (Y = waterLevel)
//   location 1: vec2  inUV        — normalised [0,1] UV (used for normal map tiling)
//
// Push constants (128 bytes):
//   mat4  viewProj          (0..63)
//   vec4  cameraPos         (64..79)  xyz = world camera position
//   float waterLevel        (80)
//   float time              (84)      seconds, for UV scroll + wave animation
//   float normalTiling      (88)      normal map tile repetitions across the plane
//   float normalScrollX     (92)      UV scroll speed X per second
//   float normalScrollZ     (96)      UV scroll speed Z per second
//   float fresnelF0         (100)     Fresnel base F0 (passed through to fragment)
//   float fadeDepthMeters   (104)     depth at which water becomes fully opaque
//   float waveAmplitude     (108)     M37.2 shore wave amplitude in metres (spec: 0.2 m)
//   float waveFrequency     (112)     M37.2 shore wave frequency in rad/m
//   float foamThreshold     (116)     M37.2 (fragment only, carried for uniform block size)
//   float causticsTiling    (120)     M37.2 (fragment only)
//   float causticsScroll    (124)     M37.2 (fragment only)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  cameraPos;
    float waterLevel;
    float time;
    float normalTiling;
    float normalScrollX;
    float normalScrollZ;
    float fresnelF0;
    float fadeDepthMeters;
    float waveAmplitude;   // M37.2
    float waveFrequency;   // M37.2
    float foamThreshold;   // M37.2
    float causticsTiling;  // M37.2
    float causticsScroll;  // M37.2
} pc;

// Fragment outputs
layout(location = 0) out vec3  vWorldPos;       // world position (for Fresnel + depth-fade)
layout(location = 1) out vec4  vClipPos;         // clip position (for projective screen-space UVs)
layout(location = 2) out vec2  vNormalUV1;       // primary normal-map UV (scrolls at speed 1)
layout(location = 3) out vec2  vNormalUV2;       // secondary normal-map UV (scrolls at speed 0.7, offset)

void main()
{
    // M37.2 — Shore wave displacement: Y += A * sin(freq * time + freq * posX)
    // The sine argument combines time (temporal oscillation) with world X (spatial phase offset)
    // so that different parts of the shore have different wave phases — organic appearance.
    float waveY = pc.waveAmplitude * sin(pc.waveFrequency * pc.time + pc.waveFrequency * inPosition.x);

    // Apply displacement to the water surface Y position.
    vec3 displacedPos = inPosition;
    displacedPos.y += waveY;

    vec4 worldPos4 = vec4(displacedPos, 1.0);
    vec4 clipPos   = pc.viewProj * worldPos4;

    gl_Position = clipPos;
    vWorldPos   = displacedPos;
    vClipPos    = clipPos;

    // Tiled + time-scrolled UV for the water normal map.
    // Two slightly different scroll directions create an organic ripple appearance.
    vec2 baseUV = inUV * pc.normalTiling;
    vNormalUV1 = baseUV + vec2(pc.normalScrollX, pc.normalScrollZ) * pc.time;
    vNormalUV2 = baseUV * 0.7 + vec2(-pc.normalScrollZ * 0.8, pc.normalScrollX * 0.6) * pc.time + vec2(0.37, 0.13);
}
