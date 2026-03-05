#version 450

// Depth-only shadow map vertex shader.
// Expects position/normal/uv layout identical to gbuffer_geometry.vert.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform ShadowPC
{
	mat4 lightViewProj;
} pc;

void main()
{
	gl_Position = pc.lightViewProj * vec4(inPosition, 1.0);
}

