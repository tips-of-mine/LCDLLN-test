#version 450

// Bloom prefilter pass (M08.1).
// Samples SceneColor_HDR, applies soft threshold (threshold + knee), outputs to BloomMip0 (HDR).

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texSceneColorHDR;

// Push constants: threshold (luminance above which to extract), knee (soft transition).
layout(push_constant) uniform PushConstants
{
    float threshold;
    float knee;
} pc;

layout(location = 0) out vec4 outBloom;

void main()
{
    vec3 color = texture(texSceneColorHDR, inUV).rgb;
    float L = dot(color, vec3(0.2126, 0.7152, 0.114));

    // Soft threshold: contribution above threshold, smoothed by knee.
    float contribution = max(0.0, L - pc.threshold);
    float soft = contribution / (contribution + pc.knee + 1e-6);

    // Preserve color ratio; avoid division by zero.
    float scale = (L > 1e-6) ? (soft / L) : 0.0;
    vec3 result = color * scale;

    outBloom = vec4(result, 1.0);
}
