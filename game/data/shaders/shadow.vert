#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 lightViewProj;
} pc;

void main() {
    gl_Position = pc.lightViewProj * vec4(inPosition, 1.0);
}
