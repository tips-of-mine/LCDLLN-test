#version 450
// Decal volume: cube in [-0.5,0.5]; worldPos = position + inPosition * (2 * halfExtents). M17.3.
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform Push {
    mat4 view;
    mat4 proj;
    vec3 decalPosition;
    float _pad0;
    vec3 decalHalfExtents;
    float fade;
} pc;

layout(location = 0) out vec3 vWorldPos;

void main() {
    vec3 worldPos = pc.decalPosition + inPosition * (2.0 * pc.decalHalfExtents);
    vWorldPos = worldPos;
    gl_Position = pc.proj * pc.view * vec4(worldPos, 1.0);
}
