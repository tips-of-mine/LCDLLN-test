#version 450
layout(location = 0) out vec2 vUV;

void main() {
    vec2 uv = vec2(((gl_VertexIndex << 1) & 2) != 0 ? 1.0 : 0.0, (gl_VertexIndex & 2) != 0 ? 1.0 : 0.0);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vUV = uv;
}
