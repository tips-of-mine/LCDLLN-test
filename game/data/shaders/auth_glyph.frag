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
	outColor = vColor;
}
