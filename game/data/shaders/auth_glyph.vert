#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inBits0;
layout(location = 4) in uint inBits1;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out uvec2 vBits;

layout(push_constant) uniform PushConstants
{
	vec2 viewportSize;
} pc;

void main()
{
	vec2 ndc;
	ndc.x = (inPos.x / pc.viewportSize.x) * 2.0 - 1.0;
	// inPos en pixels haut-gauche, Y vers le bas. Le flip vertical est fait côté CPU
	// via VkViewport.height < 0 (AuthGlyphPass), pas ici.
	ndc.y = (inPos.y / pc.viewportSize.y) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);
	vUv = inUv;
	vColor = inColor;
	vBits = uvec2(inBits0, inBits1);
}
