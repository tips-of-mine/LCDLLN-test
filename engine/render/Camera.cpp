/**
 * @file Camera.cpp
 * @brief Camera view/projection computation and FPS controller.
 */

#include "engine/render/Camera.h"
#include "engine/core/Config.h"

#include <cmath>

namespace engine::render {

namespace {

void setIdentity(float m[16]) {
    m[0] = 1; m[4] = 0; m[8]  = 0; m[12] = 0;
    m[1] = 0; m[5] = 1; m[9]  = 0; m[13] = 0;
    m[2] = 0; m[6] = 0; m[10] = 1; m[14] = 0;
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}

} // namespace

void ComputeViewMatrix(const Camera& cam, float out[16]) {
    const float cp = std::cos(cam.pitch);
    const float sp = std::sin(cam.pitch);
    const float cy = std::cos(cam.yaw);
    const float sy = std::sin(cam.yaw);

    float forward[3] = {
        cp * sy,
        -sp,
        -cp * cy
    };
    float right[3] = {
        cy,
        0.0f,
        -sy
    };
    float up[3] = {
        sy * sp,
        cp,
        cy * sp
    };

    /* Column-major: column 0 = right, 1 = up, 2 = -forward, 3 = translation */
    out[0]  = right[0];   out[4]  = up[0];   out[8]  = -forward[0]; out[12] = 0.0f;
    out[1]  = right[1];   out[5]  = up[1];   out[9]  = -forward[1]; out[13] = 0.0f;
    out[2]  = right[2];   out[6]  = up[2];   out[10] = -forward[2]; out[14] = 0.0f;
    out[3]  = 0.0f;       out[7]  = 0.0f;   out[11] = 0.0f;         out[15] = 1.0f;

    out[12] = -(right[0] * cam.position[0] + right[1] * cam.position[1] + right[2] * cam.position[2]);
    out[13] = -(up[0] * cam.position[0] + up[1] * cam.position[1] + up[2] * cam.position[2]);
    out[14] = -(-forward[0] * cam.position[0] - forward[1] * cam.position[1] - forward[2] * cam.position[2]);
}

void ComputeProjectionMatrix(const Camera& cam, float out[16]) {
    const float n = cam.nearZ;
    const float f = cam.farZ;
    const float invTan = 1.0f / std::tan(cam.fovY * 0.5f);
    const float sx = invTan / cam.aspect;
    const float sy = -invTan;
    const float A = f / (n - f);
    const float B = n * f / (n - f);

    setIdentity(out);
    out[0]  = sx;  out[4]  = 0;   out[8]  = 0;   out[12] = 0;
    out[1]  = 0;   out[5]  = sy;  out[9]  = 0;   out[13] = 0;
    out[2]  = 0;   out[6]  = 0;   out[10] = A;   out[14] = B;
    out[3]  = 0;   out[7]  = 0;   out[11] = -1;  out[15] = 0;
}

void CameraController::Update(Camera& cam, const CameraControllerInput& input, float dt) {
    const float sensitivity = engine::core::Config::GetFloat("camera.mouse_sensitivity", 0.002f);
    cam.yaw   -= input.mouseDeltaX * sensitivity;
    cam.pitch += input.mouseDeltaY * sensitivity;
    if (cam.pitch < kPitchMin) cam.pitch = kPitchMin;
    if (cam.pitch > kPitchMax) cam.pitch = kPitchMax;

    const float walkSpeed = 5.0f;
    const float runSpeed  = 10.0f;
    const float speed = (input.keyShift ? runSpeed : walkSpeed) * dt;

    const float cp = std::cos(cam.pitch);
    const float cy = std::cos(cam.yaw);
    const float sy = std::sin(cam.yaw);

    float forward[3] = { cp * sy, -std::sin(cam.pitch), -cp * cy };
    float right[3]   = { cy, 0.0f, -sy };

    if (input.keyW) {
        cam.position[0] += forward[0] * speed;
        cam.position[1] += forward[1] * speed;
        cam.position[2] += forward[2] * speed;
    }
    if (input.keyS) {
        cam.position[0] -= forward[0] * speed;
        cam.position[1] -= forward[1] * speed;
        cam.position[2] -= forward[2] * speed;
    }
    if (input.keyD) {
        cam.position[0] += right[0] * speed;
        cam.position[1] += right[1] * speed;
        cam.position[2] += right[2] * speed;
    }
    if (input.keyA) {
        cam.position[0] -= right[0] * speed;
        cam.position[1] -= right[1] * speed;
        cam.position[2] -= right[2] * speed;
    }
}

} // namespace engine::render
