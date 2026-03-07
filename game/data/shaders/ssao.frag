#version 450

// M06.2: SSAO generate. Depth + normal (view-space), kernel + noise -> occlusion 0..1.
// Binding 0: depth (D32 or R32), 1: normal (GBuffer B, world), 2: kernel UBO, 3: noise 4x4.
// Push: invProj, view, proj (192 bytes).

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outSsaoRaw; // R16F: occlusion in .r, 0=occluded, 1=no occlusion

layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2D normalTex;
layout(set = 0, binding = 2) uniform SsaoKernel {
    vec3 samples[32];
    float radius;
    float bias;
} kernel;
layout(set = 0, binding = 3) uniform sampler2D noiseTex;

layout(push_constant) uniform PC {
    mat4 invProj;
    mat4 view;
    mat4 proj;
} pc;

const int kKernelSize = 32;

void main()
{
    float depth = texture(depthTex, inUV).r;
    if (depth >= 1.0) {
        outSsaoRaw = vec4(1.0, 0.0, 0.0, 1.0); // no occlusion at sky
        return;
    }

    vec3 normalW = normalize(texture(normalTex, inUV).rgb * 2.0 - 1.0);
    vec3 normalVS = normalize(mat3(pc.view) * normalW);

    vec2 ndc = inUV * 2.0 - 1.0;
    vec4 posVS = pc.invProj * vec4(ndc, depth, 1.0);
    posVS /= posVS.w;

    vec2 noiseUV = inUV * vec2(textureSize(noiseTex, 0));
    vec3 noiseVec = vec3(texture(noiseTex, noiseUV).rg * 2.0 - 1.0, 0.0);
    vec3 tangent = normalize(noiseVec - normalVS * dot(noiseVec, normalVS));
    vec3 bitangent = cross(normalVS, tangent);
    mat3 tbn = mat3(tangent, bitangent, normalVS);

    float occlusion = 0.0;
    float radius = kernel.radius;
    float bias = kernel.bias;

    for (int i = 0; i < kKernelSize; ++i) {
        vec3 offsetVS = tbn * kernel.samples[i];
        offsetVS *= radius;
        vec4 samplePosVS = vec4(posVS.xyz + offsetVS, 1.0);
        vec4 sampleClip = pc.proj * samplePosVS;
        vec3 sampleNDC = sampleClip.xyz / sampleClip.w;
        if (sampleNDC.z > 1.0 || sampleNDC.x < -1.0 || sampleNDC.x > 1.0 || sampleNDC.y < -1.0 || sampleNDC.y > 1.0)
            continue;
        vec2 sampleUV = sampleNDC.xy * 0.5 + 0.5;
        float sampleDepth = texture(depthTex, sampleUV).r;
        vec4 samplePosReconstruct = pc.invProj * vec4(sampleNDC.xy, sampleDepth, 1.0);
        samplePosReconstruct /= samplePosReconstruct.w;
        float sampleZ = samplePosReconstruct.z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(posVS.z - sampleZ));
        if (sampleZ >= posVS.z - bias)
            occlusion += rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(kKernelSize));
    outSsaoRaw = vec4(occlusion, 0.0, 0.0, 1.0);
}
