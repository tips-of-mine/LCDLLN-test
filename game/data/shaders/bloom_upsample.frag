#version 450

// Bloom upsample pass (M08.2). Échantillonne le mip plus petit, sortie ajoutée à
// la destination (blend additif). Filtre TENTE 3x3 (Jimenez/COD) au lieu d'un
// simple tap bilinéaire : étale doucement l'énergie, halo lisse sans aliasing.

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D texBloomMip;

layout(location = 0) out vec4 outBloom;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(texBloomMip, 0));
    float dx = texelSize.x;
    float dy = texelSize.y;

    // Noyau tente 3x3 : centre 4, arêtes 2, coins 1 (somme = 16).
    vec3 s  = texture(texBloomMip, inUV + vec2(-dx,  dy)).rgb * 1.0;
    s      += texture(texBloomMip, inUV + vec2( 0.0, dy)).rgb * 2.0;
    s      += texture(texBloomMip, inUV + vec2( dx,  dy)).rgb * 1.0;
    s      += texture(texBloomMip, inUV + vec2(-dx,  0.0)).rgb * 2.0;
    s      += texture(texBloomMip, inUV).rgb               * 4.0;
    s      += texture(texBloomMip, inUV + vec2( dx,  0.0)).rgb * 2.0;
    s      += texture(texBloomMip, inUV + vec2(-dx, -dy)).rgb * 1.0;
    s      += texture(texBloomMip, inUV + vec2( 0.0,-dy)).rgb * 2.0;
    s      += texture(texBloomMip, inUV + vec2( dx, -dy)).rgb * 1.0;
    s      *= (1.0 / 16.0);

    outBloom = vec4(s, 1.0);
}
