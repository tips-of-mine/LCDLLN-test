// game/data/shaders/impostor.frag (M45.5b — impostors végétation runtime, format v2)
//
// Décode la direction caméra->instance en coords octaédriques (INVERSE de
// OctaToDir de tools/impostor_builder/OctahedralMap.h), puis INTERPOLE
// BILINÉAIREMENT les 4 vues (tiles) voisines de la grille octaédrique (réduit le
// snap de vue, M45.5b) : pour chaque tile on échantillonne albedo + normal + ORM
// avec un décalage de parallax single-step (depth en normal.a), et on blende les
// 4 par les poids bilinéaires. L'alpha-test de silhouette porte sur la couverture
// BLENDÉE (albedo.a). Un dither fade anti-popping (atlasParams.z) discard les
// fragments en cours d'apparition AVANT le blend. Enfin on écrit les 4 sorties
// GBuffer (ordre/format IDENTIQUE à gbuffer_geometry.frag).
//
// Sorties GBuffer (cf. gbuffer_geometry.frag) :
//   location 0 (GBufferA, sRGB)            : albedo (rgb, a=1)
//   location 1 (GBufferB, A2B10G10R10)     : normale monde encodée n*0.5+0.5
//   location 2 (GBufferC, RGBA8)           : ORM (R=AO, G=Roughness, B=Metallic)
//   location 3 (GBufferVelocity, R16G16F)  : velocity (0 en v1)
#version 450

layout(location = 0) in vec2 vUv;       // UV du quad billboard [0,1]²
layout(location = 1) in vec3 vViewDir;   // direction monde instance -> caméra (unitaire)
layout(location = 2) in vec2 vViewTangent; // inclinaison de vue (right/up) pour la parallax

layout(set = 0, binding = 0) uniform sampler2D uAlbedo; // atlas albedo (sRGB, a=couverture)
layout(set = 0, binding = 1) uniform sampler2D uNormal;  // atlas normal (UNORM, a=profondeur)
layout(set = 0, binding = 2) uniform sampler2D uOrm;     // atlas ORM (UNORM, r=AO g=rough b=metal)

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 cameraPos;
    vec4 atlasParams;   // x=viewsPerAxis, y=tileSize, z=fadeAlpha, w=parallaxScale
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

// Échantillonne UNE tile octaédrique (i,j) = `tileF` et renvoie albedo (rgb +
// a=couverture), normale encodée (rgb) et ORM (rgb), avec le MÊME parallax
// single-step que la v1 appliqué intra-tuile.
//   tileF       : indices entiers (i,j) de la tile, déjà clampés à [0, N-1].
//   quadUv      : UV du quad billboard ∈ [0,1]² (non décalée).
//   tileSpan    : taille d'une tile en UV atlas (= 1/N).
//   parallaxScale : échelle du décalage de parallax (atlasParams.w).
// Sorties par référence : albedoOut (.a=couverture), normalOut (.rgb encodé), ormOut.
// NB : la profondeur de parallax est lue dans CETTE tile (normal.a à l'UV non
// décalée), donc chaque tile a son propre décalage cohérent avec sa vue.
void sampleTile(vec2 tileF, vec2 quadUv, float tileSpan, float parallaxScale,
                out vec4 albedoOut, out vec4 normalOut, out vec4 ormOut)
{
    vec2 tileOrigin = tileF * tileSpan;
    // Parallax single-step : profondeur relative (normal.a) à l'UV non décalée,
    // puis décalage intra-tuile borné à la tile (clamp -> pas de bleeding).
    vec2  atlasUv0 = tileOrigin + quadUv * tileSpan;
    float depth    = texture(uNormal, atlasUv0).a;
    vec2  par      = vViewTangent * (depth - 0.5) * parallaxScale;
    vec2  quadUvP  = clamp(quadUv + par, vec2(0.0), vec2(1.0));
    vec2  atlasUv  = tileOrigin + quadUvP * tileSpan;

    albedoOut = texture(uAlbedo, atlasUv); // .rgb = albedo, .a = couverture
    normalOut = texture(uNormal, atlasUv); // .rgb = n*0.5+0.5, .a = profondeur
    ormOut    = texture(uOrm,    atlasUv); // .r = AO, .g = rough, .b = metal
}

void main()
{
    float viewsPerAxis  = pc.atlasParams.x;
    float fadeAlpha     = pc.atlasParams.z;
    float parallaxScale = pc.atlasParams.w;

    // 1) Dither fade anti-popping : discard pseudo-aléatoire stable sur l'écran.
    //    Appliqué AVANT le blend de vues (un fragment fondu est purement éliminé).
    if (fadeAlpha < 1.0)
    {
        float threshold = Bayer4x4(ivec2(gl_FragCoord.xy));
        if (fadeAlpha < threshold)
            discard;
    }

    float tileSpan = 1.0 / viewsPerAxis;
    vec2  quadUv   = clamp(vUv, vec2(0.0), vec2(1.0));

    // 2) Interpolation BILINÉAIRE des 4 vues octaédriques voisines (M45.5b) :
    //    réduit le snap visible quand la direction de vue traverse une frontière
    //    de tile. On place octUv sur la grille des CENTRES de tiles (- 0.5), d'où
    //    g0 = coin bas-gauche du carré de 4 tiles et f = poids fractionnaires.
    vec2 octUv = DirToOcta(normalize(vViewDir));
    vec2 g  = octUv * viewsPerAxis - 0.5;
    vec2 g0 = floor(g);
    vec2 f  = g - g0; // poids bilinéaires ∈ [0,1]²
    // Indices des 4 tiles, clampés à [0, N-1]. APPROX bordure : aux bords de la
    // grille octaédrique, le clamp répète la tile de bord au lieu de "wrapper" sur
    // la vue antipodale repliée (couture octaédrique). Acceptable en v1.5 — le
    // fondu reste continu, l'erreur n'apparaît qu'au tout dernier rang de tiles.
    float nMax = viewsPerAxis - 1.0;
    float i0 = clamp(g0.x,        0.0, nMax);
    float i1 = clamp(g0.x + 1.0,  0.0, nMax);
    float j0 = clamp(g0.y,        0.0, nMax);
    float j1 = clamp(g0.y + 1.0,  0.0, nMax);

    // 3) Échantillonne les 4 tiles (chacune avec son propre parallax).
    vec4 a00, n00, o00; sampleTile(vec2(i0, j0), quadUv, tileSpan, parallaxScale, a00, n00, o00);
    vec4 a10, n10, o10; sampleTile(vec2(i1, j0), quadUv, tileSpan, parallaxScale, a10, n10, o10);
    vec4 a01, n01, o01; sampleTile(vec2(i0, j1), quadUv, tileSpan, parallaxScale, a01, n01, o01);
    vec4 a11, n11, o11; sampleTile(vec2(i1, j1), quadUv, tileSpan, parallaxScale, a11, n11, o11);

    // 4) Poids bilinéaires.
    float w00 = (1.0 - f.x) * (1.0 - f.y);
    float w10 = f.x         * (1.0 - f.y);
    float w01 = (1.0 - f.x) * f.y;
    float w11 = f.x         * f.y;

    // 5) Blend des 4 résultats (albedo rgb+a couverture, normale rgb, orm rgb).
    vec4 albedo = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
    vec3 normalEnc = n00.rgb * w00 + n10.rgb * w10 + n01.rgb * w01 + n11.rgb * w11;
    vec3 orm    = o00.rgb * w00 + o10.rgb * w10 + o01.rgb * w01 + o11.rgb * w11;

    // 6) Alpha-test de silhouette sur la COUVERTURE BLENDÉE (albedo.a interpolée).
    if (albedo.a < 0.5)
        discard;

    // 7) Écriture GBuffer.
    outAlbedo = vec4(albedo.rgb, 1.0);
    // Normale : on décode la moyenne pondérée (n*0.5+0.5), renormalise, ré-encode.
    //   Le blend d'encodages reste proche de la moyenne des normales pour des vues
    //   voisines ; la renormalisation corrige la dérive de longueur.
    vec3 nWorld = normalize(normalEnc * 2.0 - 1.0);
    outNormal = vec4(nWorld * 0.5 + 0.5, 1.0);
    // ORM blendé depuis l'atlas v2.
    outORM = vec4(orm, 1.0);
    // Velocity nulle en v1 (impostors statiques ; pas de reprojection dédiée).
    outVelocity = vec4(0.0, 0.0, 0.0, 1.0);
}
