#version 450

// Fullscreen triangle vertex shader (tonemap pass).
// No vertex buffer required: three vertices are generated from gl_VertexIndex.
// Produces a CCW triangle covering the entire screen in NDC [-1,+1].

layout(location = 0) out vec2 outUV; // [0,1]^2 UV coordinates

void main()
{
    // Standard fullscreen-triangle trick:
    //   index 0 -> uv (0, 0)  -> ndc (-1, -1)
    //   index 1 -> uv (2, 0)  -> ndc ( 3, -1)
    //   index 2 -> uv (0, 2)  -> ndc (-1,  3)
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
