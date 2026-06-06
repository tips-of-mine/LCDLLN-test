#version 450

// Bloom downsample pass (M08.1).
// Filtre 13-tap « dual filtering » (Jimenez, "Next Generation Post Processing in
// Call of Duty: Advanced Warfare"). Remplace l'ancien box 2x2 : échantillonnage
// stable, supprime le scintillement et l'aspect « en blocs » du downsample box.

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texSource;

layout(location = 0) out vec4 outBloom;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(texSource, 0));
    float dx = texelSize.x;
    float dy = texelSize.y;

    // 13 échantillons : un anneau extérieur (±2 texels) + un carré intérieur (±1 texel).
    vec3 a = texture(texSource, inUV + vec2(-2.0 * dx,  2.0 * dy)).rgb;
    vec3 b = texture(texSource, inUV + vec2( 0.0,       2.0 * dy)).rgb;
    vec3 c = texture(texSource, inUV + vec2( 2.0 * dx,  2.0 * dy)).rgb;
    vec3 d = texture(texSource, inUV + vec2(-2.0 * dx,  0.0)).rgb;
    vec3 e = texture(texSource, inUV).rgb;
    vec3 f = texture(texSource, inUV + vec2( 2.0 * dx,  0.0)).rgb;
    vec3 g = texture(texSource, inUV + vec2(-2.0 * dx, -2.0 * dy)).rgb;
    vec3 h = texture(texSource, inUV + vec2( 0.0,      -2.0 * dy)).rgb;
    vec3 i = texture(texSource, inUV + vec2( 2.0 * dx, -2.0 * dy)).rgb;
    vec3 j = texture(texSource, inUV + vec2(-dx,  dy)).rgb;
    vec3 k = texture(texSource, inUV + vec2( dx,  dy)).rgb;
    vec3 l = texture(texSource, inUV + vec2(-dx, -dy)).rgb;
    vec3 m = texture(texSource, inUV + vec2( dx, -dy)).rgb;

    // Pondération Jimenez (somme = 1) : centre 0.125, carré intérieur 4×0.125,
    // coins extérieurs 4×0.03125, arêtes extérieures 4×0.0625.
    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    outBloom = vec4(result, 1.0);
}
