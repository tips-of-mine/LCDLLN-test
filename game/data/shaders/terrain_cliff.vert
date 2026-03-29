#version 450
// M34.3: Terrain cliff vertex shader.
//
// Renders manually-authored cliff mesh geometry into the GBuffer.
// Vertex format (CliffVertex, 36 bytes):
//   location 0: vec3  position    — world-space XYZ
//   location 1: vec3  normal      — world-space unit normal
//   location 2: vec2  uv          — texture UV [0,1]
//   location 3: float blendWeight — 0=cliff interior, 1=terrain-edge blend
//
// Descriptor set 0:
//   binding 0: CliffFrameUbo  { mat4 viewProj; mat4 prevViewProj; }

layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in float inBlendWeight;

// Per-frame UBO: view-projection matrices only (world-space mesh, no model matrix).
layout(set = 0, binding = 0) uniform CliffFrameUbo {
    mat4  viewProj;      // current frame view-projection
    mat4  prevViewProj;  // previous frame view-projection (for TAA velocity)
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec3  vWorldPos;
layout(location = 1) out vec3  vWorldNormal;
layout(location = 2) out vec2  vUV;
layout(location = 3) out float vBlendWeight;
layout(location = 4) out vec4  vPrevClip;
layout(location = 5) out vec4  vCurrClip;

void main()
{
    vec4 worldPos4 = vec4(inPosition, 1.0);

    vWorldPos    = inPosition;
    vWorldNormal = normalize(inNormal);
    vUV          = inUV;
    vBlendWeight = inBlendWeight;
    vCurrClip    = ubo.viewProj     * worldPos4;
    vPrevClip    = ubo.prevViewProj * worldPos4;

    gl_Position = vCurrClip;
}
