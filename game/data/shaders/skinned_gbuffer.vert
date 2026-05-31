#version 450
// Skinned variant of gbuffer_geometry.vert (sub-projet A, task 12/17).
// - Per-vertex bone indices (uint16x4, widened to uvec4) at location 7
// - Per-vertex weights (float4) at location 8
// - Per-draw bone matrix palette via SSBO set 1 binding 0
// Outputs: identical to gbuffer_geometry.vert -> paired with gbuffer_geometry.frag unchanged.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 instanceRow0;
layout(location = 4) in vec4 instanceRow1;
layout(location = 5) in vec4 instanceRow2;
layout(location = 6) in vec4 instanceRow3;
layout(location = 7) in uvec4 inBoneIdx;
layout(location = 8) in vec4  inWeights;

layout(push_constant) uniform PushConstants {
    mat4 prevViewProj;
    mat4 viewProj;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

layout(set = 1, binding = 0) readonly buffer BonesSSBO {
    mat4 bones[];
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vPrevClip;
layout(location = 3) out vec4 vCurrClip;
// Le fragment partagé gbuffer_geometry.frag attend vColor en location 4. L'avatar
// n'utilise pas de vertex color : on émet du blanc (le bit VertexColorAlbedo n'est
// jamais posé sur ses matériaux, donc vColor est ignoré côté frag).
layout(location = 4) out vec4 vColor;

void main() {
    // Build the per-vertex skinning matrix as a weighted sum of bone matrices.
    // Weights are pre-normalized by FBX2glTF (--normalize-weights 1, default), so this sum equals 1.
    mat4 skin =
        inWeights.x * bones[inBoneIdx.x] +
        inWeights.y * bones[inBoneIdx.y] +
        inWeights.z * bones[inBoneIdx.z] +
        inWeights.w * bones[inBoneIdx.w];

    vec4 posSkinned = skin * vec4(inPosition, 1.0);
    vec3 normalSkinned = mat3(skin) * inNormal;

    mat4 instanceMatrix = mat4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
    vec4 worldPos = instanceMatrix * posSkinned;

    vec4 prevClip = pc.prevViewProj * worldPos;
    vec4 currClip = pc.viewProj * worldPos;
    gl_Position = currClip;

    vNormal = mat3(instanceMatrix) * normalSkinned;
    vUv = inUv;
    vPrevClip = prevClip;
    vCurrClip = currClip;
    vColor = vec4(1.0);
}
