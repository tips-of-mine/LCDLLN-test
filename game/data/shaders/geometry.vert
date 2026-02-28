#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 viewProjCurr;
    mat4 viewProjPrev;
    mat4 modelCurr;
    mat4 modelPrev;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vClipCurr;
layout(location = 3) out vec4 vClipPrev;

void main() {
    vec4 worldCurr = pc.modelCurr * vec4(inPosition, 1.0);
    vec4 worldPrev = pc.modelPrev * vec4(inPosition, 1.0);
    gl_Position = pc.viewProjCurr * worldCurr;
    vClipCurr = gl_Position;
    vClipPrev = pc.viewProjPrev * worldPrev;
    vNormal = inNormal;
    vUV = inUV;
}
