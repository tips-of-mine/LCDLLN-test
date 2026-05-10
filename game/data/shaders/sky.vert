#version 450
// M38.1 — Sky gradient vertex shader.
// Fullscreen triangle; view direction reconstructed in fragment shader.
//
// gl_Position.z = 1.0 (far plane Vulkan, standard depth) : combine avec
// `depthTestEnable=TRUE` + `depthCompareOp=LESS_OR_EQUAL` cote pipeline,
// le sky ne passe le test que la ou la depth a ete laissee a 1.0
// (clear value GeometryPass = "no geometry"). Les pixels avec geometrie
// ont depth < 1.0 donc le test `1.0 <= depth_geo` echoue et la geometrie
// reste visible. depthWriteEnable=FALSE pour ne pas modifier la depth :
// LightingPass detecte toujours "no geometry" via depth==1.0 et compose
// la sky depuis GBuffer A (Phase 5 Lunar / M38.1).

layout(location = 0) out vec2 vUV;

void main()
{
    // Fullscreen triangle trick (CCW winding).
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
}
