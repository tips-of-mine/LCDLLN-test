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

layout(set = 0, binding = 0) uniform sampler2D uTextures[128];
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
    // Découpe alpha (MaterialFlags::AlphaCutout = 4) : contour des coiffures sculptées.
    if ((mat.flags & 4u) != 0u && base.a < 0.5)
        discard;

    // Personnalisation des personnages : teinte du matériau (MaterialFlags::Tint = 8,
    // TintMasked = 16). La teinte RGBA8 est rangée dans les bits de padding.x
    // (MaterialGpuData) ; elle multiplie l'albedo (masquée par 1 - alpha pour les yeux).
    if ((mat.flags & 24u) != 0u) {
        vec3 tintColor = unpackUnorm4x8(floatBitsToUint(mat.padding.x)).rgb;
        float tintAmount = ((mat.flags & 16u) != 0u) ? (1.0 - base.a) : 1.0;
        base.rgb = mix(base.rgb, base.rgb * tintColor, tintAmount);
        base.a = 1.0;
    }

    vec3 N = normalize(vNormal);
    // Lumière clé fixe (cadrage aperçu) + ambiant doux pour révéler le volume.
    vec3 L = normalize(vec3(0.4, 0.8, 0.6));
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.35;
    vec3 lit = base.rgb * (ambient + diff * 0.85);

    // Cible couleur en UNORM (pas sRGB) : on ré-encode gamma pour un affichage correct.
    // Alpha forcé à 1 : l'alpha des atlas sert de masque (yeux, découpe), pas d'opacité.
    outColor = vec4(pow(lit, vec3(1.0 / 2.2)), 1.0);
}
