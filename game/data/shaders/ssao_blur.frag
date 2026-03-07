#version 450

// M06.3: Bilateral blur for SSAO. Depth-aware weights (5–7 taps).
// Binding 0: input AO (R16F), 1: depth.
// Push: texelSize (xy), horizontal (z).

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outBlur;

layout(set = 0, binding = 0) uniform sampler2D aoTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform PC {
    vec2 texelSize;
    float horizontal;
    float _pad;
} pc;

const int kTaps = 7;
const float kSigmaDepth = 0.02;

void main()
{
    float depthCenter = texture(depthTex, inUV).r;
    if (depthCenter >= 1.0) {
        outBlur = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    vec2 offset = pc.horizontal > 0.5
        ? vec2(pc.texelSize.x, 0.0)
        : vec2(0.0, pc.texelSize.y);

    float sumAo = 0.0;
    float sumW  = 0.0;
    int radius = (kTaps - 1) / 2; // -3..+3 for 7 taps

    for (int i = -radius; i <= radius; ++i) {
        vec2 uv = inUV + float(i) * offset;
        float ao = texture(aoTex, uv).r;
        float depth = texture(depthTex, uv).r;
        float depthDiff = abs(depth - depthCenter);
        float depthW = exp(-depthDiff / kSigmaDepth);
        float spatialW = 1.0 - abs(float(i)) / float(radius + 1);
        float w = depthW * spatialW;
        sumAo += ao * w;
        sumW  += w;
    }

    float result = sumW > 1e-6 ? (sumAo / sumW) : texture(aoTex, inUV).r;
    outBlur = vec4(result, 0.0, 0.0, 1.0);
}
