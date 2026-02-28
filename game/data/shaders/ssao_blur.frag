#version 450
/**
 * SSAO bilateral blur: 7 taps along direction, depth-aware range weight.
 * Bilateral weight = spatial * range(depth diff). No bleed over depth discontinuities.
 */
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uSsao;
layout(binding = 1) uniform sampler2D uDepth;

layout(push_constant) uniform Push {
    vec2 direction;
    vec2 invSize;
} pc;

layout(location = 0) out vec4 outColor;

const int kTaps = 7;
const int kHalf = 3;

void main() {
    float centerDepth = texture(uDepth, vUV).r;
    float centerAo = texture(uSsao, vUV).r;

    float sigmaDepth = 0.01;
    float sigmaSpace = 2.0;

    float sum = centerAo;
    float wSum = 1.0;

    for (int i = -kHalf; i <= kHalf; ++i) {
        if (i == 0) continue;
        vec2 offset = pc.direction * float(i) * pc.invSize;
        vec2 uv = vUV + offset;
        float ao = texture(uSsao, uv).r;
        float d = texture(uDepth, uv).r;
        float depthDiff = abs(centerDepth - d);
        float rangeW = exp(-(depthDiff * depthDiff) / (2.0 * sigmaDepth * sigmaDepth));
        float dist = float(i);
        float spatialW = exp(-(dist * dist) / (2.0 * sigmaSpace * sigmaSpace));
        float w = rangeW * spatialW;
        sum += ao * w;
        wSum += w;
    }

    outColor = vec4(sum / wSum, 0.0, 0.0, 1.0);
}
