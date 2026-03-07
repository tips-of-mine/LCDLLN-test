#version 450

// M07.4: TAA — reproject history, clamp 3x3 neighborhood, blend.
// Binding 0: current LDR, 1: history prev, 2: velocity (R16G16, NDC delta), 3: depth (optional).
// uvHistory = uv - velocity*0.5 (velocity is currNDC - prevNDC; UV = (NDC+1)*0.5).
// Clamp history to 3x3 min/max of current; out = lerp(current, historyClamped, alpha).

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uCurrent;   // SceneColor_LDR
layout(set = 0, binding = 1) uniform sampler2D uHistory;   // HistoryPrev
layout(set = 0, binding = 2) uniform sampler2D uVelocity;  // GBufferVelocity .rg = NDC velocity
layout(set = 0, binding = 3) uniform sampler2D uDepth;     // Depth

layout(push_constant) uniform PC {
    float alpha;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

void main()
{
    vec2 velocityNDC = texture(uVelocity, inUV).rg;
    // prevUV = currUV - velocityNDC * 0.5 (NDC to UV: delta_UV = delta_NDC * 0.5)
    vec2 uvHistory = inUV - velocityNDC * 0.5;

    // Off-screen: fallback to current.
    if (uvHistory.x < 0.0 || uvHistory.x > 1.0 || uvHistory.y < 0.0 || uvHistory.y > 1.0)
    {
        outColor = texture(uCurrent, inUV);
        return;
    }

    vec4 currentCenter = texture(uCurrent, inUV);

    // 3x3 neighborhood min/max of current frame.
    vec2 texelSize = 1.0 / vec2(textureSize(uCurrent, 0));
    vec4 cMin = currentCenter;
    vec4 cMax = currentCenter;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            vec2 uv = inUV + vec2(float(dx), float(dy)) * texelSize;
            vec4 c = texture(uCurrent, uv);
            cMin = min(cMin, c);
            cMax = max(cMax, c);
        }

    vec4 history = texture(uHistory, uvHistory);
    vec4 historyClamped = clamp(history, cMin, cMax);

    outColor = mix(currentCenter, historyClamped, pc.alpha);
}
