#version 450
// M07.3: prevViewProj + viewProj for motion vectors (velocity = currNDC - prevNDC).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform PushConstants {
	mat4 prevViewProj;  // 0..63
	mat4 viewProj;      // 64..127
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vPrevClip;
layout(location = 3) out vec4 vCurrClip;

void main() {
	vec4 worldPos = vec4(inPosition, 1.0);
	vec4 prevClip = pc.prevViewProj * worldPos;
	vec4 currClip = pc.viewProj * worldPos;
	gl_Position = currClip;
	vNormal = inNormal;
	vUv = inUv;
	vPrevClip = prevClip;
	vCurrClip = currClip;
}
