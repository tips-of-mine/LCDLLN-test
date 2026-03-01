#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 viewProjCurr;
    mat4 viewProjPrev;
    mat4 modelCurr;
    mat4 modelPrev;
#ifdef USE_INDIRECT_DRAWS
    uint useIndirect;  // M18.2: 1 = use visibleTransforms[gl_DrawID]
#endif
} pc;

#ifdef USE_INDIRECT_DRAWS
layout(set = 1, binding = 0) buffer VisibleTransforms { mat4 visibleTransforms[]; };
#endif

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec4 vClipCurr;
layout(location = 3) out vec4 vClipPrev;

void main() {
    mat4 modelCurr = pc.modelCurr;
    mat4 modelPrev = pc.modelPrev;
#ifdef USE_INDIRECT_DRAWS
    if (pc.useIndirect != 0u) {
        modelCurr = visibleTransforms[gl_DrawID];
        modelPrev = visibleTransforms[gl_DrawID];
    }
#endif
    vec4 worldCurr = modelCurr * vec4(inPosition, 1.0);
    vec4 worldPrev = modelPrev * vec4(inPosition, 1.0);
    gl_Position = pc.viewProjCurr * worldCurr;
    vClipCurr = gl_Position;
    vClipPrev = pc.viewProjPrev * worldPrev;
    vNormal = inNormal;
    vUV = inUV;
}
