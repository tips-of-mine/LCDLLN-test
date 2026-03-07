#version 450

// Tonemap pass fragment shader (M03.4 + M08.4 LUT).
// Reads SceneColor_HDR, applies exposure, ACES filmic, gamma 2.2.
// Optional: apply color grading LUT (strip 256x16) with strength blend.

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texSceneColorHDR;
layout(set = 0, binding = 1) uniform sampler2D texLUT;

layout(push_constant) uniform PushConstants
{
    float exposure;
    float strength;  // LUT blend 0..1 (M08.4)
} pc;

layout(location = 0) out vec4 outSceneColorLDR;

// ---------------------------------------------------------------------------
// ACES filmic approximation (Krzysztof Narkowicz, 2015).
// Maps HDR luminances to [0,1] with a filmic shoulder and toe.
// Input and output are linear-light values.
// ---------------------------------------------------------------------------
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    // Sample linear-light HDR color.
    vec3 hdr = texture(texSceneColorHDR, inUV).rgb;

    // Apply linear exposure (1.0 = no change; >1.0 brightens; <1.0 darkens).
    vec3 exposed = hdr * pc.exposure;

    // Apply ACES filmic tonemap curve: highlights are compressed, midtones preserved.
    vec3 tonemapped = ACESFilm(exposed);

    const float invGamma = 1.0 / 2.2;
    vec3 gammaCorrected = pow(tonemapped, vec3(invGamma));

    // M08.4: Optional LUT color grading (strip 256x16: 16 slices in v, 16x16 in u per slice).
    vec3 finalColor = gammaCorrected;
    if (pc.strength > 0.001)
    {
        vec3 c = clamp(gammaCorrected, 0.0, 1.0);
        float r_idx = floor(c.r * 15.99);
        float g_idx = floor(c.g * 15.99);
        float b_idx = floor(c.b * 15.99);
        float u = (r_idx * 16.0 + g_idx + 0.5) / 256.0;
        float v = (b_idx + 0.5) / 16.0;
        vec3 lutColor = texture(texLUT, vec2(u, v)).rgb;
        finalColor = mix(gammaCorrected, lutColor, pc.strength);
    }

    outSceneColorLDR = vec4(finalColor, 1.0);
}
