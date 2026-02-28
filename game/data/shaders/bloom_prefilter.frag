#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uHDR;

layout(push_constant) uniform Push {
    float threshold;
    float knee;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 rgb = texture(uHDR, vUV).rgb;
    float luminance = dot(rgb, vec3(0.2126, 0.7152, 0.0784));
    // Soft threshold: smoothstep(threshold - knee, threshold + knee, luminance)
    float soft = smoothstep(pc.threshold - pc.knee, pc.threshold + pc.knee, luminance);
    outColor = vec4(rgb * soft, 1.0);
}
