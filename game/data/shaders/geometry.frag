#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vClipCurr;
layout(location = 3) in vec4 vClipPrev;

layout(binding = 0) uniform sampler2D uBaseColor;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uORM;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outORM;
layout(location = 3) out vec2 outVelocity;

void main() {
    outAlbedo = texture(uBaseColor, vUV);
    vec3 N = normalize(texture(uNormal, vUV).rgb * 2.0 - 1.0);
    outNormal = vec4(N * 0.5 + 0.5, 1.0);
    vec3 orm = texture(uORM, vUV).rgb;
    outORM = vec4(orm.r, orm.g, orm.b, 1.0);

    // Velocity = currNDC - prevNDC (M07.3)
    vec2 ndcCurr = vClipCurr.xy / vClipCurr.w;
    vec2 ndcPrev = vClipPrev.xy / vClipPrev.w;
    outVelocity = ndcCurr - ndcPrev;
}
