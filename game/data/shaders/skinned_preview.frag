#version 450
// Aperçu de création de personnage (sous-chantier A, phase 2) : éclairage forward
// simple, sortie couleur unique. Réutilise le MÊME descriptor set 0 bindless que
// gbuffer_geometry.frag (set 0, binding 0 = textures, binding 1 = MaterialBuffer)
// pour partager le pipeline layout et le descriptor set matériau de l'engine.
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;

// DOIT rester identique à MaterialGpuData de gbuffer_geometry.frag (même layout
// mémoire), sinon l'indexation dans le SSBO partagé serait décalée.
struct MaterialGpuData
{
    uint baseColorIndex;
    uint normalIndex;
    uint ormIndex;
    uint flags;
    vec2 tiling;
    vec2 padding;
};

layout(set = 0, binding = 0) uniform sampler2D uTextures[64];
layout(set = 0, binding = 1) readonly buffer MaterialBuffer
{
    MaterialGpuData materials[];
} uMaterialBuffer;

layout(push_constant) uniform PushConstants {
    mat4 prevViewProj;
    mat4 viewProj;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialGpuData mat = uMaterialBuffer.materials[pc.materialIndex];
    vec2 uv = vUv * mat.tiling;
    vec4 base = texture(uTextures[mat.baseColorIndex], uv); // sRGB -> linéaire au sample

    vec3 N = normalize(vNormal);
    // Lumière clé fixe (cadrage aperçu) + ambiant doux pour révéler le volume.
    vec3 L = normalize(vec3(0.4, 0.8, 0.6));
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.35;
    vec3 lit = base.rgb * (ambient + diff * 0.85);

    // Cible couleur en UNORM (pas sRGB) : on ré-encode gamma pour un affichage correct.
    outColor = vec4(pow(lit, vec3(1.0 / 2.2)), base.a);
}
