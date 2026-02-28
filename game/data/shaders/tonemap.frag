#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uHDR;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    vec3 ldr = hdr / (hdr + 1.0);
    float gamma = 2.2;
    ldr = pow(ldr, vec3(1.0 / gamma));
    outColor = vec4(ldr, 1.0);
}
