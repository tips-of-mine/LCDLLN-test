#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uSource;

layout(push_constant) uniform Push {
    float invSourceWidth;
    float invSourceHeight;
} pc;

layout(location = 0) out vec4 outColor;

// Box/tent: sample 4 texels (half-texel offsets in source) and average.
void main() {
    vec2 uv = vUV;
    float dx = pc.invSourceWidth * 0.5;
    float dy = pc.invSourceHeight * 0.5;
    vec4 a = texture(uSource, uv + vec2(-dx, -dy));
    vec4 b = texture(uSource, uv + vec2( dx, -dy));
    vec4 c = texture(uSource, uv + vec2(-dx,  dy));
    vec4 d = texture(uSource, uv + vec2( dx,  dy));
    outColor = (a + b + c + d) * 0.25;
}
