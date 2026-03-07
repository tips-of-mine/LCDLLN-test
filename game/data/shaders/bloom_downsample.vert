#version 450

// Fullscreen triangle vertex shader (bloom downsample pass, M08.1).
// Same as prefilter: NDC triangle, outUV in [0,1]^2.

layout(location = 0) out vec2 outUV;

void main()
{
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
