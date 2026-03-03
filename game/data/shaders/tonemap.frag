#version 450

// Tonemap pass fragment shader (M03.4).
// Reads SceneColor_HDR (R16G16B16A16_SFLOAT), applies exposure,
// runs through an ACES-ish filmic curve, then applies gamma 2.2.
// Writes SceneColor_LDR (R8G8B8A8_UNORM).

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texSceneColorHDR;

// Push constants: exposure multiplier applied before the tonemap curve.
// Adjustable via config key "tonemap.exposure" (default 1.0).
layout(push_constant) uniform PushConstants
{
    float exposure;
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

    // Apply gamma 2.2 correction for display on standard sRGB monitors.
    // If the swapchain format is already SRGB the hardware will apply sRGB
    // encoding on top; in that case prefer SRGB swapchain + linear output.
    // For R8G8B8A8_UNORM output (this pass), manual gamma is required.
    const float invGamma = 1.0 / 2.2;
    vec3 gammaCorrected = pow(tonemapped, vec3(invGamma));

    outSceneColorLDR = vec4(gammaCorrected, 1.0);
}
