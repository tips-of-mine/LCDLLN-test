#version 450
// M37.3 — Underwater post-effects vertex shader.
// Generates a fullscreen covering triangle from gl_VertexIndex (no VBO needed).
// The same technique as tonemap.vert.

layout(location = 0) out vec2 outUV; // [0, 1]^2 screen UV

void main()
{
    // Standard fullscreen-triangle trick:
    //   index 0 -> uv (0, 0) -> ndc (-1, -1)
    //   index 1 -> uv (2, 0) -> ndc ( 3, -1)
    //   index 2 -> uv (0, 2) -> ndc (-1,  3)
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
