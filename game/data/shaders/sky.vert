#version 450
// M38.1 — Sky gradient vertex shader.
// Fullscreen triangle; view direction reconstructed in fragment shader.

layout(location = 0) out vec2 vUV;

void main()
{
    // Fullscreen triangle trick (CCW winding).
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
