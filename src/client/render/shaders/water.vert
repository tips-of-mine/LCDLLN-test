#version 450

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;
    float flowSpeed;
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inFlowDir;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec2 vFlowDir;

void main()
{
    vUv       = inUv;
    vWorldPos = inPosition;
    vFlowDir  = inFlowDir;
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}
