#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outDecal;

layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2D decalTex;

layout(push_constant) uniform PC
{
    mat4 invViewProj;
    vec4 center;
    vec4 halfExtents;
    float fadeAlpha;
    // Rotation du decal autour de Y (radians). 0 = aligné monde (decals
    // historiques inchangés). Utilisé par le réticule de ciblage pour faire
    // tourner le cône de vision avec le yaw du PNJ ciblé.
    float yawRadians;
} pc;

void main()
{
    float depth = texture(depthTex, inUV).r;
    if (depth >= 1.0)
    {
        discard;
    }

    vec2 ndcXY = inUV * 2.0 - 1.0;
    vec4 clipPos = vec4(ndcXY, depth, 1.0);
    vec4 worldPos = pc.invViewProj * clipPos;
    worldPos /= worldPos.w;

    vec3 local = worldPos.xyz - pc.center.xyz;
    // Rotation de -yaw autour de Y : exprime le point dans le repère LOCAL du
    // decal (convention jeu : regard du PNJ = (sin yaw, 0, cos yaw) -> +Z local).
    // Miroir GPU de WorldOffsetToReticleLocal (TargetReticleGeometry.cpp).
    float cy = cos(pc.yawRadians);
    float sy = sin(pc.yawRadians);
    local = vec3(cy * local.x - sy * local.z, local.y, sy * local.x + cy * local.z);
    vec3 absLocal = abs(local);
    if (absLocal.x > pc.halfExtents.x || absLocal.y > pc.halfExtents.y || absLocal.z > pc.halfExtents.z)
    {
        discard;
    }

    vec2 uv = vec2(
        local.x / max(pc.halfExtents.x * 2.0, 1e-5) + 0.5,
        local.z / max(pc.halfExtents.z * 2.0, 1e-5) + 0.5);
    vec4 decalSample = texture(decalTex, uv);
    decalSample.a *= pc.fadeAlpha;
    if (decalSample.a <= 0.001)
    {
        discard;
    }

    outDecal = decalSample;
}
