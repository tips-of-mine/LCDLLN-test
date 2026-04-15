#version 450
// M37.1 — Water surface vertex shader.
// M37.2 — Shore wave vertex animation: Y += waveAmplitude * sin(time + posX).
// Input: vec2 inXZ in [0, 1]^2 (local plane coordinates).
// The push constant planeHalfSize scales the plane to world space;
// waterY places it at the correct world-space height.

layout(location = 0) in vec2 inXZ;

layout(push_constant) uniform PushConstants
{
    mat4  viewProj;       // Camera view-projection matrix.
    vec4  cameraPos;      // Camera world-space position (xyz used, w unused).
    float waterY;         // Base world-space Y of the water plane.
    float time;           // Elapsed seconds — used for UV animation.
    float fresnelF0;      // Fresnel reflectance at 0° incidence (≈0.02 for water).
    float planeHalfSize;  // Half-size in world units; plane spans ±planeHalfSize XZ.
    // M37.2:
    float waveAmplitude;      // Shore wave peak displacement in metres (e.g. 0.2).
    float foamThreshold;      // Unused in vertex shader; must mirror WaterParams layout.
    float causticsScale;      // Unused in vertex shader; must mirror WaterParams layout.
    float causticsIntensity;  // Unused in vertex shader; must mirror WaterParams layout.
    float nearZ;              // Unused in vertex shader; must mirror WaterParams layout.
    float farZ;               // Unused in vertex shader; must mirror WaterParams layout.
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec4 outClipPos;

void main()
{
    // Map [0, 1] → [−planeHalfSize, +planeHalfSize] in X and Z.
    float worldX = inXZ.x * pc.planeHalfSize * 2.0 - pc.planeHalfSize;
    float worldZ = inXZ.y * pc.planeHalfSize * 2.0 - pc.planeHalfSize;

    // M37.2 — Shore wave: Y displacement using overlapping sine waves.
    // Higher frequency near the edges (shore), lower in the center (deep water).
    const float kWaveFreqX = 0.80;  // cycles per world unit
    const float kWaveFreqZ = 0.60;
    float waveY = pc.waveAmplitude
        * (sin(pc.time * 1.4 + worldX * kWaveFreqX) * 0.6
        +  sin(pc.time * 0.9 + worldZ * kWaveFreqZ + worldX * 0.3) * 0.4);

    vec3 worldPos = vec3(worldX, pc.waterY + waveY, worldZ);

    outWorldPos = worldPos;
    outUV       = inXZ;           // Raw [0,1] UVs; tiling applied in fragment shader.
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    outClipPos  = gl_Position;
}
