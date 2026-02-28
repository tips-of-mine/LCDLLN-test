#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 outColor;

// Bilinear upsample: single sample at vUV (hardware bilinear).
void main() {
    outColor = texture(uSource, vUV);
}
