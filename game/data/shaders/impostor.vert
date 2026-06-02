// game/data/shaders/impostor.vert (M45.5 — impostors végétation runtime)
//
// Génère un quad camera-facing (billboard) de 2 triangles (6 sommets via
// gl_VertexIndex) centré sur instancePos.xyz, de demi-taille instancePos.w
// (radius), aligné sur les axes right/up dérivés de la matrice viewProj.
//
// Sorties :
//   - position clip = viewProj * coin monde du quad ;
//   - vUv  : UV du quad dans [0,1]² ;
//   - vViewDir : direction monde INSTANCE -> CAMÉRA (unitaire), décodée en
//                coords octaédriques dans le fragment (cf. impostor.frag).
//   - vViewTangent : direction de vue (instance->caméra) projetée dans le repère
//                tangent (camRight, camUp) du billboard. Sert d'inclinaison de
//                vue pour le décalage de parallax single-step (frag v2). C'est
//                une APPROXIMATION v1 (billboard plan, pas de vraie reconstruction
//                de la profondeur de surface).
#version 450

// Push constants : cf. ImpostorPushConstants (ImpostorPass.h), 176 octets.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;      // 64
    mat4 prevViewProj;  // 64 (réservé velocity, non utilisé ici)
    vec4 cameraPos;     // 16 (xyz)
    vec4 atlasParams;   // 16 (x=viewsPerAxis, y=tileSize, z=fadeAlpha, w=0)
    vec4 instancePos;   // 16 (xyz centre monde, w=radius)
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vViewDir;
layout(location = 2) out vec2 vViewTangent; // inclinaison de vue dans le repère tangent (parallax)

void main()
{
    // Quad unitaire [-0.5,0.5]² en 2 triangles (6 sommets). offsets : coin local.
    // Triangle 1 : (0,0)(1,0)(1,1) ; Triangle 2 : (0,0)(1,1)(0,1) (UV [0,1]²).
    vec2 quadUv[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 uv = quadUv[gl_VertexIndex];
    vUv = uv;
    // Offset centré : [-0.5, 0.5].
    vec2 corner = uv - vec2(0.5);

    vec3 center = pc.instancePos.xyz;
    float radius = pc.instancePos.w;

    // Axes caméra (right/up) monde extraits de viewProj. La Mat4 moteur est
    // column-major (cf. Math.h) : GLSL charge donc le buffer directement, et
    // viewProj[col] est une COLONNE. Le vecteur "right" monde de la caméra est
    // la LIGNE 0 de viewProj (axe X de l'espace vue), soit
    // vec3(viewProj[0].x, viewProj[1].x, viewProj[2].x) ; "up" = LIGNE 1.
    // On normalise pour éliminer la mise à l'échelle de la projection.
    vec3 camRight = normalize(vec3(pc.viewProj[0].x, pc.viewProj[1].x, pc.viewProj[2].x));
    vec3 camUp    = normalize(vec3(pc.viewProj[0].y, pc.viewProj[1].y, pc.viewProj[2].y));

    // Position monde du coin du billboard.
    vec3 worldPos = center
        + camRight * (corner.x * 2.0 * radius)
        + camUp    * (corner.y * 2.0 * radius);

    // Direction instance -> caméra (mesh -> caméra), pour le décodage octaédrique.
    vec3 toCam = normalize(pc.cameraPos.xyz - center);
    vViewDir = toCam;

    // Projection de la direction de vue dans le repère tangent (right/up) du
    // billboard : composantes de l'inclinaison de vue utilisées par le décalage
    // de parallax intra-tuile dans le fragment. Approximation v1.
    vViewTangent = vec2(dot(toCam, camRight), dot(toCam, camUp));

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
}
