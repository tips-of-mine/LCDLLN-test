#version 450

// Surface d'eau (lacs, rivières, mer).
// EAU.6 — la MER n'est plus plate : près du joueur (grille de mer, cf.
// src/client/world/water/SeaGrid.h) ses sommets sont soulevés par la houle,
// qui s'éteint en eau peu profonde, et par la vague de bord qui monte puis se
// retire sur la plage. Lacs et rivières ne bougent pas (décision EAU.6).
//
// RÈGLE DE PARITÉ : seaSwellHeight / seaSurge / seaSwellDepthScale sont le
// double exact de src/client/world/water/SeaSwell.cpp (la nage lit la même
// hauteur). Toute modification se fait des deux côtés ; le test sea_swell_tests
// vérifie les constantes ci-dessous.

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;   // mer : direction du vent (XZ)
    float flowSpeed;       // < 0 : mer, |flowSpeed| = amplitude de la houle (m)
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inFlowDir;
layout(location = 3) in vec2 inSea;      // x = profondeur d'eau (m), y = fondu des vagues (0..1)

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec2 vFlowDir;
layout(location = 3) out vec2 vSea;      // x = profondeur d'eau (m), y = fondu des vagues

// ── Constantes partagées avec SeaSwell.h (parité) ───────────────────────────
const float kSeaRunUpM            = 0.30;
const float kSeaSurfPeriodS       = 9.0;
const float kSeaSurfPhasePerMeter = 0.9;
const float kSeaSurfZoneDepthM    = 2.5;
const float kSeaSwellShallowM     = 0.2;
const float kSeaSwellFullDepthM   = 5.0;
const float kSwellLen[4]    = float[4](21.0, 13.0, 8.0, 5.0);
const float kSwellWeight[4] = float[4](0.55, 0.30, 0.18, 0.10);
const float kSwellAngle[4]  = float[4](0.0, 0.45, -0.60, 1.10);
const float kSwellWeightSum = 1.13;

float seaSwellDepthScale(float depthM)
{
    return smoothstep(kSeaSwellShallowM, kSeaSwellFullDepthM, depthM);
}

// Houle au large (m, moyenne nulle) : 4 sinusoïdes autour du vent, mêmes
// directions et phases que l'éclairage de la houle (water.frag, swellSlope).
float seaSwellHeight(vec2 p, vec2 wind, float amp, float t)
{
    vec2 w = length(wind) > 1e-3 ? normalize(wind) : vec2(1.0, 0.0);
    float h = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        float a = kSwellAngle[i];
        vec2 d = normalize(vec2(w.x * cos(a) - w.y * sin(a), w.x * sin(a) + w.y * cos(a)));
        float k = 6.2831853 / kSwellLen[i];
        float c = sqrt(9.81 / k);
        float ph = k * dot(d, p) - k * c * t;
        h += (kSwellWeight[i] / kSwellWeightSum) * sin(ph);
    }
    return amp * h;
}

float seaSurfPhase(float depthM, float t)
{
    return 6.2831853 * (t / kSeaSurfPeriodS) + kSeaSurfPhasePerMeter * depthM;
}

// Montée de l'eau sur la plage (m, ≥ 0).
float seaSurge(vec2 p, float depthM, float t)
{
    float zone = 1.0 - smoothstep(0.8, kSeaSurfZoneDepthM, depthM);
    if (zone <= 0.0)
        return 0.0;
    float s = max(0.0, sin(seaSurfPhase(depthM, t)));
    float along = 0.75 + 0.25 * sin(0.031 * p.x + 0.017 * p.y + 0.05 * t);
    return kSeaRunUpM * along * s * s * zone;
}

void main()
{
    vec3 pos = inPosition;
    bool isRiver = length(inFlowDir) > 0.001;
    bool isSea   = !isRiver && pc.flowSpeed < -0.001;
    if (isSea && inSea.y > 0.0)
    {
        float amp = -pc.flowSpeed;
        float off = seaSwellHeight(pos.xz, pc.flowDirection, amp, pc.timeSeconds) * seaSwellDepthScale(inSea.x)
                  + seaSurge(pos.xz, inSea.x, pc.timeSeconds);
        // Fondu de bord de grille : raccord sans marche avec la mer plate.
        pos.y += off * inSea.y;
    }
    vUv       = inUv;
    vWorldPos = pos;
    vFlowDir  = inFlowDir;
    vSea      = inSea;
    gl_Position = pc.viewProj * vec4(pos, 1.0);
}
