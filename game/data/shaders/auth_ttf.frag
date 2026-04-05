#version 450

layout(set = 0, binding = 0) uniform sampler2D fontTex;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in uvec2 vBits;

layout(location = 0) out vec4 outColor;

void main()
{
	float a = texture(fontTex, vUv).r;
	if (a < 0.02)
		discard;
	outColor = vec4(vColor.rgb, vColor.a * a);
}
