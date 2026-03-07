#version 450

// Bloom combine pass (M08.2). SceneColor_HDR + bloom * intensity → HDR output (before tonemap).

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texSceneColorHDR;
layout(set = 0, binding = 1) uniform sampler2D texBloom;

layout(push_constant) uniform PushConstants
{
    float intensity;
} pc;

layout(location = 0) out vec4 outHDR;

void main()
{
    vec3 scene = texture(texSceneColorHDR, inUV).rgb;
    vec3 bloom = texture(texBloom, inUV).rgb;
    outHDR = vec4(scene + bloom * pc.intensity, 1.0);
}
