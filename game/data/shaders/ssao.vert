#version 450

// Fullscreen triangle for SSAO pass (M06.2). Same as lighting/tonemap.

layout(location = 0) out vec2 outUV;

void main()
{
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
