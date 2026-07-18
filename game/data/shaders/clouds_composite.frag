#version 450

// Composite des nuages basse résolution sur la scène (lot 1, 2026-07-18).
// La passe Clouds_March écrit dans une cible RÉDUITE (1/2 ou 1/4 de la
// swapchain, cf. render.clouds.resolution_divider) :
//   rgb = couleur nuages PRÉ-MULTIPLIÉE (scattered * fade)
//   a   = visibilité de la scène = (1 - opacité nuages) * ombre au sol
// Cette passe pleine résolution upsample (bilinéaire) et compose :
//   final = scene * clouds.a + clouds.rgb
// Fullscreen triangle partagé (lighting.vert).

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor; // HDR post-fog (pleine rés.)
layout(set = 0, binding = 1) uniform sampler2D uClouds;     // nuages basse rés. (premult + visibilité)

void main()
{
	vec3 scene  = texture(uSceneColor, inUV).rgb;
	vec4 clouds = texture(uClouds, inUV);
	outColor = vec4(scene * clouds.a + clouds.rgb, 1.0);
}
