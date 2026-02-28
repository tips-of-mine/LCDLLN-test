#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uHDR;

layout(push_constant) uniform Push {
    float exposure;
} pc;

layout(location = 0) out vec4 outColor;

// ACES filmic approximation (ACES-ish): compresses highlights, no hard clipping.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    hdr *= pc.exposure;
    vec3 ldr = ACESFilm(hdr);
    float gamma = 2.2;
    ldr = pow(ldr, vec3(1.0 / gamma));
    outColor = vec4(ldr, 1.0);
}
