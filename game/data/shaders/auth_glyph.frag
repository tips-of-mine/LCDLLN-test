#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in uvec2 vBits;

layout(location = 0) out vec4 outColor;

uint rowBitsFor(int row)
{
	if (row < 4)
	{
		return (vBits.x >> (row * 8)) & 0xffu;
	}
	return (vBits.y >> ((row - 4) * 8)) & 0xffu;
}

void main()
{
	int col = clamp(int(floor(vUv.x * 5.0)), 0, 4);
	int row = clamp(int(floor(vUv.y * 7.0)), 0, 6);
	uint rowBits = rowBitsFor(row);
	uint mask = 1u << uint(4 - col);
	if ((rowBits & mask) == 0u)
	{
		discard;
	}
	// Adoucit les bords de chaque « pixel » de la fonte bitmap (évite l’effet bloc EGA/CGA).
	vec2 f = fract(vUv * vec2(5.0, 7.0));
	vec2 d = abs(f - 0.5) * 2.0;
	float cellEdge = max(d.x, d.y);
	float soften = 1.0 - smoothstep(0.70, 1.0, cellEdge);
	outColor = vec4(vColor.rgb, vColor.a * soften);
}
