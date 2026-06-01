// game/data/shaders/impostor.frag (M45.5 — impostors végétation runtime)
//
// Décode la direction caméra->instance en coords octaédriques (INVERSE de
// OctaToDir de tools/impostor_builder/OctahedralMap.h) pour choisir la tile
// (i,j), échantillonne albedo + normal dans la tile, applique l'alpha-test de
// silhouette + un dither fade anti-popping, puis écrit les 4 sorties GBuffer
// (ordre/format IDENTIQUE à gbuffer_geometry.frag).
//
// Sorties GBuffer (cf. gbuffer_geometry.frag) :
//   location 0 (GBufferA, sRGB)            : albedo (rgb, a=1)
//   location 1 (GBufferB, A2B10G10R10)     : normale monde encodée n*0.5+0.5
//   location 2 (GBufferC, RGBA8)           : ORM (R=AO, G=Roughness, B=Metallic)
//   location 3 (GBufferVelocity, R16G16F)  : velocity (0 en v1)
#version 450

layout(location = 0) in vec2 vUv;       // UV du quad billboard [0,1]²
layout(location = 1) in vec3 vViewDir;   // direction monde instance -> caméra (unitaire)

layout(set = 0, binding = 0) uniform sampler2D uAlbedo; // atlas albedo (sRGB)
layout(set = 0, binding = 1) uniform sampler2D uNormal;  // atlas normal (UNORM, a=masque)

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 cameraPos;
    vec4 atlasParams;   // x=viewsPerAxis, y=tileSize, z=fadeAlpha, w=0
    vec4 instancePos;
} pc;

layout(location = 0) out vec4 outAlbedo;   // GBufferA
layout(location = 1) out vec4 outNormal;   // GBufferB
layout(location = 2) out vec4 outORM;      // GBufferC
layout(location = 3) out vec4 outVelocity; // GBufferVelocity

// Encode une direction unitaire en coords octaédriques (u,v) ∈ [0,1]².
// INVERSE EXACT de OctaToDir (Cigolle 2014, octahedron complet) :
//   p = dir.xy / (|x|+|y|+|z|)
//   si z < 0 : p = (1 - |p.yx|) * sign(p.xy)
//   uv = p * 0.5 + 0.5
vec2 DirToOcta(vec3 dir)
{
    vec3 n = dir / (abs(dir.x) + abs(dir.y) + abs(dir.z));
    vec2 p = n.xy;
    if (n.z < 0.0)
    {
        p = (1.0 - abs(vec2(n.y, n.x))) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
                                               n.y >= 0.0 ? 1.0 : -1.0);
    }
    return p * 0.5 + 0.5;
}

// Matrice de Bayer 4x4 (seuils [0,1)) pour le dither fade stable, compatible TAA.
float Bayer4x4(ivec2 p)
{
    const float m[16] = float[16](
        0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
        3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    int idx = (p.y & 3) * 4 + (p.x & 3);
    return m[idx];
}

void main()
{
    float viewsPerAxis = pc.atlasParams.x;
    float fadeAlpha    = pc.atlasParams.z;

    // 1) Dither fade anti-popping : discard pseudo-aléatoire stable sur l'écran.
    if (fadeAlpha < 1.0)
    {
        float threshold = Bayer4x4(ivec2(gl_FragCoord.xy));
        if (fadeAlpha < threshold)
            discard;
    }

    // 2) Choix de la tile (i,j) depuis la direction de vue.
    vec2 octUv = DirToOcta(normalize(vViewDir));
    // (i,j) = floor(octUv * N), clampé à [0, N-1].
    vec2 tileF = floor(octUv * viewsPerAxis);
    tileF = clamp(tileF, vec2(0.0), vec2(viewsPerAxis - 1.0));

    // 3) UV dans l'atlas : origine de la tile + UV du quad ramenée à la tile.
    //    L'atlas est une grille N×N ; chaque tile occupe 1/N de l'atlas.
    //    quadUv [0,1] -> sous-région [tile/N, (tile+1)/N].
    vec2 atlasUv = (tileF + clamp(vUv, vec2(0.0), vec2(1.0))) / viewsPerAxis;

    vec4 albedo = texture(uAlbedo, atlasUv);
    vec4 normalSample = texture(uNormal, atlasUv); // .rgb = n*0.5+0.5, .a = masque silhouette

    // 4) Alpha-test de silhouette (masque de couverture M45.4 stocké en normal.a).
    if (normalSample.a < 0.5)
        discard;

    // 5) Écriture GBuffer.
    outAlbedo = vec4(albedo.rgb, 1.0);
    // L'atlas stocke déjà la normale encodée n*0.5+0.5 -> on la réécrit telle quelle.
    outNormal = vec4(normalSample.rgb, 1.0);
    // ORM : AO=1, Roughness=1 (feuillage mat), Metallic=0.
    outORM = vec4(1.0, 1.0, 0.0, 1.0);
    // Velocity nulle en v1 (impostors statiques ; pas de reprojection dédiée).
    outVelocity = vec4(0.0, 0.0, 0.0, 1.0);
}
