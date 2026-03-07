#version 450

// Bloom downsample pass (M08.1).
// Box filter 2x2: samples four texels, outputs average to next mip level.

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texSource;

layout(location = 0) out vec4 outBloom;

void main()
{
    // Half-pixel offset to sample the four texels of the 2x2 block.
    vec2 texelSize = 1.0 / vec2(textureSize(texSource, 0));
    vec2 off = texelSize * 0.5;

    vec4 a = texture(texSource, inUV + vec2(-off.x, -off.y));
    vec4 b = texture(texSource, inUV + vec2( off.x, -off.y));
    vec4 c = texture(texSource, inUV + vec2(-off.x,  off.y));
    vec4 d = texture(texSource, inUV + vec2( off.x,  off.y));

    outBloom = (a + b + c + d) * 0.25;
}
