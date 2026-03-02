#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 outAlbedo;   // GBuffer A (R8G8B8A8_SRGB)
layout(location = 1) out vec4 outNormal;   // GBuffer B (normal)
layout(location = 2) out vec4 outORM;        // GBuffer C (occlusion, roughness, metallic)

void main() {
	// Simple material: albedo gray, normal view-space, ORM default
	outAlbedo = vec4(0.7, 0.7, 0.7, 1.0);
	outNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
	outORM = vec4(1.0, 0.5, 0.0, 1.0);  // occlusion, roughness, metallic
}
