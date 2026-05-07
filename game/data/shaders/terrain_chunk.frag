// game/data/shaders/terrain_chunk.frag (M100.9)
// Fragment shader 8-layer blend. Distinct du `terrain.frag` legacy (M34, 4
// layers SLAP). Échantillonne deux samplers `splatMap0` / `splatMap1` (RGBA8
// chacun = 4 layers) pour récupérer les 8 poids, puis blend 8 layers PBR
// (albedo / normal / ARM) tilées individuellement par `tilingScale[8]`.
//
// Output : 3 attachments compatibles avec le GBuffer existant (à confirmer
// au moment de l'intégration Task 14) :
//   location 0 (GBufferA, RGBA8_UNORM)        : albedo
//   location 1 (GBufferB, A2R10G10B10_UNORM)  : world normal encodé N*0.5+0.5
//   location 2 (GBufferC, RGBA8_UNORM)        : ARM (R=AO, G=Roughness, B=Metallic)
//
// Note : pour rester compatible avec la passe Geometry actuelle qui produit
// aussi un GBufferVelocity (M07 TAA), on n'écrit pas le velocity ici — le
// caller (TerrainChunkPipeline) gérera le binding velocity à part si besoin
// pendant l'intégration.
#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_worldPos;

// Set 2 : ressources splat-spécifiques (M100.9). 6 bindings.
layout(set = 2, binding = 0) uniform sampler2D u_splatMap0;       // RGBA = layers 0..3 weights (R=0, G=1, B=2, A=3)
layout(set = 2, binding = 1) uniform sampler2D u_splatMap1;       // RGBA = layers 4..7 weights (R=4, G=5, B=6, A=7)
layout(set = 2, binding = 2) uniform sampler2DArray u_albedoArray;
layout(set = 2, binding = 3) uniform sampler2DArray u_normalArray;
layout(set = 2, binding = 4) uniform sampler2DArray u_armArray;

layout(set = 2, binding = 5) uniform LayerParams {
    // tilingScale[i] = 1 / tilingMeters[i] : multiplie worldPos.xz pour
    // obtenir les UV de tiling individuels par layer.
    float tilingScale[8];
} u_layers;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outArm;

void main()
{
    vec4 a = texture(u_splatMap0, v_uv);
    vec4 b = texture(u_splatMap1, v_uv);
    float w[8] = float[8](a.r, a.g, a.b, a.a, b.r, b.g, b.b, b.a);

    vec3 albedo = vec3(0.0);
    vec3 nrm    = vec3(0.0);
    vec3 arm    = vec3(0.0);
    float totalW = 0.0;

    // Boucle déroulable par le compilateur GLSL (8 itérations connues).
    for (int i = 0; i < 8; ++i)
    {
        if (w[i] <= 0.001) continue;
        vec2 tiledUV = v_worldPos.xz * u_layers.tilingScale[i];
        albedo += texture(u_albedoArray, vec3(tiledUV, float(i))).rgb * w[i];
        nrm    += texture(u_normalArray, vec3(tiledUV, float(i))).rgb * w[i];
        arm    += texture(u_armArray,    vec3(tiledUV, float(i))).rgb * w[i];
        totalW += w[i];
    }

    if (totalW > 0.001)
    {
        albedo /= totalW;
        nrm    /= totalW;
        arm    /= totalW;
    }
    else
    {
        // Fallback : si tous les poids sont nuls (ne devrait pas arriver vu
        // l'invariant somme=255 mais sécurité), utiliser la normale vertex.
        nrm = v_normal * 0.5 + 0.5;
    }

    outAlbedo = vec4(albedo, 1.0);
    // Normale tangente space : remap [0, 1] → [-1, 1], puis normaliser.
    // Pour rester compatible avec le GBufferB du legacy, on ré-encode N*0.5+0.5.
    vec3 worldNormal = normalize(nrm * 2.0 - 1.0);
    outNormal = vec4(worldNormal * 0.5 + 0.5, 0.0);
    outArm    = vec4(arm, 1.0);
}
