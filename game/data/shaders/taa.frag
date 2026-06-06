#version 450

// M07.4: TAA — reproject history, variance clamping du voisinage 3x3, blend.
// Binding 0: current LDR, 1: history prev, 2: velocity (R16G16, NDC delta), 3: depth (optional).
// uvHistory = uv - velocity*0.5 (velocity is currNDC - prevNDC; UV = (NDC+1)*0.5).
// Borne l'historique à mean ± sigma (variance clamping) du voisinage 3x3 courant ;
// out = lerp(current, historyClamped, alpha). Moins de ghosting qu'un min/max dur.

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

    // Variance clamping (Salvi/Karis) : au lieu d'un AABB min/max dur du voisinage
    // 3x3 (trop permissif -> ghosting), on borne l'historique à mean ± gamma*sigma.
    // La boîte est plus serrée et adaptée au bruit local -> moins de traînées,
    // tout en gardant assez de marge pour l'anti-aliasing.
    vec2 texelSize = 1.0 / vec2(textureSize(uCurrent, 0));
    vec4 m1 = vec4(0.0);
    vec4 m2 = vec4(0.0);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec4 c = texture(uCurrent, inUV + vec2(float(dx), float(dy)) * texelSize);
            m1 += c;
            m2 += c * c;
        }
    const float invN = 1.0 / 9.0;
    vec4 mean  = m1 * invN;
    vec4 sigma = sqrt(max(m2 * invN - mean * mean, vec4(0.0)));
    const float gamma = 1.0; // largeur de la boîte en écarts-types
    vec4 cMin = mean - gamma * sigma;
    vec4 cMax = mean + gamma * sigma;

    vec4 history = texture(uHistory, uvHistory);
    vec4 historyClamped = clamp(history, cMin, cMax);

    outColor = mix(currentCenter, historyClamped, pc.alpha);
}
