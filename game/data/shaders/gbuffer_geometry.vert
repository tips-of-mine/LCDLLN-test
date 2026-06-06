#version 450
// M07.3: prevViewProj + viewProj for motion vectors (velocity = currNDC - prevNDC).
// M09.3: instance matrix (binding 1) for instanced draws; use identity for single draw.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;        // COLOR_0 (vertex color des props nature)
layout(location = 4) in vec4 instanceRow0;
layout(location = 5) in vec4 instanceRow1;
layout(location = 6) in vec4 instanceRow2;
layout(location = 7) in vec4 instanceRow3;

layout(push_constant) uniform PushConstants {
	mat4 prevViewProj;  // 0..63
	mat4 viewProj;      // 64..127
	uint materialIndex;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vPrevClip;
layout(location = 3) out vec4 vCurrClip;
layout(location = 4) out vec4 vColor;
layout(location = 5) out vec3 vWorldPos; // position MONDE (pour la base TBN cotangente du frag)

void main() {
	mat4 instanceMatrix = mat4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
	vec4 worldPos = instanceMatrix * vec4(inPosition, 1.0);
	vec4 prevClip = pc.prevViewProj * worldPos;
	vec4 currClip = pc.viewProj * worldPos;
	gl_Position = currClip;
	// Normale en espace MONDE : mat3(instanceMatrix) applique la rotation (+
	// échelle uniforme, sans incidence après normalize côté frag). Corrige les
	// props TOURNÉS (yaw) dont la normale objet était auparavant écrite telle
	// quelle dans le GBuffer monde. Indispensable aussi à la base TBN du frag.
	vNormal = mat3(instanceMatrix) * inNormal;
	vUv = inUv;
	vPrevClip = prevClip;
	vCurrClip = currClip;
	vColor = inColor;
	vWorldPos = worldPos.xyz;
}
