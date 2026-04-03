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
	// On les évite en discardant les pixels quasiment blancs.
	// Si votre logo contient aussi des zones blanches importantes,
	// on pourra ajuster ce seuil.
	if (c.r > 0.98 && c.g > 0.98 && c.b > 0.98)
		discard;
	outColor = c;
}
