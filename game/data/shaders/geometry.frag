#version 450
layout(location = 0) in vec3 vNormal;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outORM;

void main() {
    outAlbedo = vec4(0.8, 0.4, 0.2, 1.0);
    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
    outORM = vec4(1.0, 0.5, 0.1, 1.0);
}
