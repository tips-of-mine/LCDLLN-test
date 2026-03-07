#version 450

// Bloom upsample pass (M08.2). Samples smaller mip (bilinear), output is added to destination (additive blend).

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texBloomMip;

layout(location = 0) out vec4 outBloom;

void main()
{
    outBloom = texture(texBloomMip, inUV);
}
