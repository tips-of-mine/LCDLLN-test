#version 450
// Aperçu de création de personnage (sous-chantier A, phase 2) : skinning forward.
// Variante réduite de skinned_gbuffer.vert : mêmes entrées (skinning identique),
// mais sorties minimales (normale monde + UV) — pas de motion vectors, le rendu
// d'aperçu écrit dans une cible couleur unique échantillonnée par ImGui.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 instanceRow0;
layout(location = 4) in vec4 instanceRow1;
layout(location = 5) in vec4 instanceRow2;
layout(location = 6) in vec4 instanceRow3;
layout(location = 7) in uvec4 inBoneIdx;
layout(location = 8) in vec4  inWeights;

// Layout identique à skinned_gbuffer.vert (144 octets) pour partager le
// pipeline layout et la convention de push constants (prevViewProj inutilisé ici
// mais conservé pour aligner la taille).
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

void main() {
    // Matrice de skinning = somme pondérée des matrices d'os (poids pré-normalisés).
    mat4 skin =
        inWeights.x * bones[inBoneIdx.x] +
        inWeights.y * bones[inBoneIdx.y] +
        inWeights.z * bones[inBoneIdx.z] +
        inWeights.w * bones[inBoneIdx.w];

    vec4 posSkinned    = skin * vec4(inPosition, 1.0);
    vec3 normalSkinned = mat3(skin) * inNormal;

    mat4 instanceMatrix = mat4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
    vec4 worldPos = instanceMatrix * posSkinned;

    gl_Position = pc.viewProj * worldPos;
    vNormal = mat3(instanceMatrix) * normalSkinned;
    vUv = inUv;
}
