#version 450
// M07.3: prevViewProj + viewProj for motion vectors (velocity = currNDC - prevNDC).
// M09.3: instance matrix (binding 1) for instanced draws; use identity for single draw.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 instanceRow0;
layout(location = 4) in vec4 instanceRow1;
layout(location = 5) in vec4 instanceRow2;
layout(location = 6) in vec4 instanceRow3;

layout(push_constant) uniform PushConstants {
	mat4 prevViewProj;  // 0..63
	mat4 viewProj;      // 64..127
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vPrevClip;
layout(location = 3) out vec4 vCurrClip;

void main() {
	mat4 instanceMatrix = mat4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
	vec4 worldPos = instanceMatrix * vec4(inPosition, 1.0);
	vec4 prevClip = pc.prevViewProj * worldPos;
	vec4 currClip = pc.viewProj * worldPos;
	gl_Position = currClip;
	vNormal = inNormal;
	vUv = inUv;
	vPrevClip = prevClip;
	vCurrClip = currClip;
}
