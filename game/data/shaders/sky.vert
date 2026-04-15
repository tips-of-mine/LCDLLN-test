#version 450
// M38.1 — Sky gradient vertex shader.
// Fullscreen triangle — no vertex buffer (same technique as tonemap.vert).
// Reconstructs a view ray for each fragment using the inverse VP matrix.

layout(location = 0) out vec2 outUV;      // [0,1]^2 screen UV
layout(location = 1) out vec3 outViewRay; // World-space ray from camera (unnormalised)

layout(push_constant) uniform PushConstants
{
    mat4  invViewProj;      // Inverse view-projection for ray reconstruction.
    vec4  cameraPos;        // Camera world-space position.
    vec4  sunDirection;     // Normalised toward-sun direction.
    vec4  skyZenith;        // Sky colour at top of dome.
    vec4  skyHorizon;       // Sky colour at horizon.
    float sunElevation;     // sin(sunAngle), −1..+1.
    float timeHours;        // In-game time 0..24 h (for moon/night blend).
    float _pad0;
    float _pad1;
} pc;

void main()
{
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 1.0, 1.0); // Far-plane depth (z=1)

    // Reconstruct world-space ray for this screen pixel.
    vec4 clip = vec4(outUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = pc.invViewProj * clip;
    outViewRay  = world.xyz / world.w - pc.cameraPos.xyz;
}
