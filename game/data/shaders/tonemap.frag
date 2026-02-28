#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uHDR;
layout(binding = 1) uniform ExposureBuf {
    float exposure;
};
layout(binding = 2) uniform sampler2D uLUT;

layout(push_constant) uniform LutParams {
    uint lutEnable;
    float lutStrength;
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

// 256x16 LUT strip (16^3 entries): texel (r*15+g*240, b*15), normalized uv with half-texel offset
vec3 sampleLUT256x16(vec3 ldr) {
    float u = (ldr.r * 15.0 + ldr.g * 240.0 + 0.5) / 256.0;
    float v = (ldr.b * 15.0 + 0.5) / 16.0;
    return texture(uLUT, vec2(u, v)).rgb;
}

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    hdr *= exposure;
    vec3 ldr = ACESFilm(hdr);
    float gamma = 2.2;
    ldr = pow(ldr, vec3(1.0 / gamma));
    if (pc.lutEnable != 0u && pc.lutStrength > 0.0) {
        vec3 lutColor = sampleLUT256x16(ldr);
        ldr = mix(ldr, lutColor, pc.lutStrength);
    }
    outColor = vec4(ldr, 1.0);
}
