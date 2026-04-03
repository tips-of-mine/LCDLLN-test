#version 450

layout(location = 0) out vec2 vUv;

layout(push_constant) uniform Push
{
	vec2 viewportSize;
	vec2 centerPx;
	vec2 halfExtentPx;
	float cosA;
	float sinA;
} pc;

void main()
{
	const vec2 uvs[6] = vec2[](
		vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
		vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
	vec2 uv = uvs[gl_VertexIndex];
	vec2 local = (uv * 2.0 - 1.0) * pc.halfExtentPx;
	float c = pc.cosA;
	float s = pc.sinA;
	vec2 rot = vec2(c * local.x - s * local.y, s * local.x + c * local.y);
	vec2 pix = pc.centerPx + rot;
	vec2 ndc;
	ndc.x = (pix.x / pc.viewportSize.x) * 2.0 - 1.0;
	ndc.y = (pix.y / pc.viewportSize.y) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);
	// Retourner horizontalement (miroir) pour corriger l’inversion constatée.
	vUv = vec2(1.0 - uv.x, uv.y);
}
