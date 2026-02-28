#version 450
/**
 * SSAO generate pass: depth + normal (view-space) -> raw occlusion 0..1.
 * Reconstruct posVS via inverse projection; TBN from normal + noise; kernel samples; range check smoothstep.
 */
layout(location = 0) in vec2 vUV;

layout(binding = 0) uniform sampler2D uDepth;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform SSAOKernelUBO {
    vec3 samples[32];
    float radius;
    float bias;
} kernel;

layout(binding = 3) uniform sampler2D uNoise;

layout(push_constant) uniform Push {
    mat4 invProj;
    mat4 proj;
    mat4 view;
} pc;

layout(location = 0) out vec4 outColor;

const int kKernelSize = 32;

void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 N_world = normalize(texture(uNormal, vUV).rgb * 2.0 - 1.0);
    vec3 N = normalize((pc.view * vec4(N_world, 0.0)).xyz);

    vec4 ndc = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 posVS4 = pc.invProj * ndc;
    vec3 posVS = posVS4.xyz / posVS4.w;

    vec2 noiseUV = vUV * vec2(textureSize(uDepth, 0)) / 4.0;
    vec2 noiseVal = texture(uNoise, noiseUV).rg * 2.0 - 1.0;
    vec3 T = normalize(cross(vec3(0.0, 1.0, 0.0), N));
    if (length(T) < 0.01) T = normalize(cross(vec3(1.0, 0.0, 0.0), N));
    vec3 B = cross(N, T);
    vec3 T_rand = normalize(T * noiseVal.x + B * noiseVal.y);
    vec3 B_rand = cross(N, T_rand);
    mat3 TBN = mat3(T_rand, B_rand, N);

    float occlusion = 0.0;
    for (int i = 0; i < kKernelSize; ++i) {
        vec3 sampleOffset = TBN * kernel.samples[i];
        vec3 samplePosVS = posVS + kernel.radius * sampleOffset;

        vec4 sampleClip = pc.proj * vec4(samplePosVS, 1.0);
        vec3 sampleNDC = sampleClip.xyz / sampleClip.w;
        vec2 sampleUV = sampleNDC.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float depthSample = texture(uDepth, sampleUV).r;
        vec4 sceneVS4 = pc.invProj * vec4(sampleNDC.xy, depthSample, 1.0);
        float sceneZ = (sceneVS4.z / sceneVS4.w);

        float rangeCheck = smoothstep(0.0, 1.0, kernel.radius / abs(posVS.z - sceneZ));
        if (samplePosVS.z >= sceneZ - kernel.bias)
            occlusion += rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(kKernelSize));
    outColor = vec4(occlusion, 0.0, 0.0, 1.0);
}
