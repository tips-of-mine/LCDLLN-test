#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex;

void main()
{
	vec4 c = texture(tex, vUv);
	if (c.a < 0.004)
		discard;
	outColor = c;
}
