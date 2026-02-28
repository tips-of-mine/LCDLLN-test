#version 450
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uLDR;
layout(binding = 1) uniform sampler2D uHistoryA;
layout(binding = 2) uniform sampler2D uHistoryB;
layout(binding = 3) uniform sampler2D uVelocity;
layout(binding = 4) uniform sampler2D uDepth;

layout(push_constant) uniform Push {
    uint historyIndex; // 0 = sample A (prev), 1 = sample B (prev)
} pc;

layout(location = 0) out vec4 outColor;

// Alpha for blend: ~0.9 (more history = more stable, more ghost risk)
const float kAlpha = 0.9;

void main() {
    vec4 current = texture(uLDR, vUV);
    vec2 velocityNDC = texture(uVelocity, vUV).rg;
    // NDC to UV: 1 NDC unit = 0.5 UV. prevUV = currUV - velocityNDC * 0.5
    vec2 uvHistory = vUV - velocityNDC * 0.5;

    // Off-screen fallback -> use current (M07.4 note)
    if (uvHistory.x < 0.0 || uvHistory.x > 1.0 || uvHistory.y < 0.0 || uvHistory.y > 1.0) {
        outColor = current;
        return;
    }

    vec4 history = (pc.historyIndex == 0u) ? texture(uHistoryA, uvHistory) : texture(uHistoryB, uvHistory);

    // 3x3 neighborhood min/max of current (clamp anti-ghost)
    vec2 texelSize = 1.0 / vec2(textureSize(uLDR, 0));
    vec3 minC = current.rgb;
    vec3 maxC = current.rgb;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 off = vec2(float(dx), float(dy)) * texelSize;
            vec3 s = texture(uLDR, vUV + off).rgb;
            minC = min(minC, s);
            maxC = max(maxC, s);
        }
    }
    vec3 historyClamped = clamp(history.rgb, minC, maxC);

    // Blend: lerp(current, historyClamped, alpha)
    vec3 outRGB = mix(current.rgb, historyClamped, kAlpha);
    outColor = vec4(outRGB, 1.0);
}
