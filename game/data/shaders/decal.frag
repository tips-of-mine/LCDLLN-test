#version 450
// Decal albedo projection; fade for lifetime. M17.3.
layout(location = 0) in vec3 vWorldPos;

layout(push_constant) uniform Push {
    mat4 view;
    mat4 proj;
    vec3 decalPosition;
    float _pad0;
    vec3 decalHalfExtents;
    float fade;
} pc;

layout(set = 0, binding = 0) uniform sampler2D uAlbedo;

layout(location = 0) out vec4 outAlbedo;

void main() {
    vec3 local = (vWorldPos - pc.decalPosition) / (2.0 * pc.decalHalfExtents);
    if (any(greaterThan(abs(local), vec3(0.5))))
        discard;
    // Projection per face: dominant axis gives face, then UV from the other two.
    vec2 uv;
    vec3 a = abs(local);
    if (a.y >= a.x && a.y >= a.z) uv = local.xz + 0.5;
    else if (a.x >= a.z) uv = local.zy + 0.5;
    else uv = local.xy + 0.5;
    vec4 albedo = texture(uAlbedo, uv);
    albedo.a *= pc.fade;
    if (albedo.a < 0.01)
        discard;
    outAlbedo = albedo;
}
