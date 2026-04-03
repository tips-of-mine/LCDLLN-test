#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex;

void main()
{
	vec4 c = texture(tex, vUv);
	if (c.a < 0.004)
		discard;
	// Certains PNGs de logo contiennent un “blanc de fond” opaque.
	// On les évite en discardant les pixels quasi-blancs.
	if (c.a > 0.9 && c.r > 0.92 && c.g > 0.92 && c.b > 0.92)
		discard;
	outColor = c;
}
