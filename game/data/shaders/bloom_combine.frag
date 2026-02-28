#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uSceneHDR;
layout(binding = 1) uniform sampler2D uBloom;

layout(push_constant) uniform Push {
    float intensity;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 scene = texture(uSceneHDR, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;
    outColor = vec4(scene + bloom * pc.intensity, 1.0);
}
