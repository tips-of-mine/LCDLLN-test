#version 450
// Billboard quad: camera-facing. inCorner = quad corner (-1,-1), (1,-1), (-1,1), (1,1).
layout(location = 0) in vec2 inCorner;

layout(location = 1) in vec3 inPosition;
layout(location = 2) in float inSize;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 view;
    mat4 proj;
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    vec4 viewPos = pc.view * vec4(inPosition, 1.0);
    viewPos.xy += inCorner * inSize;
    gl_Position = pc.proj * viewPos;
    vColor = inColor;
}
