#pragma once

/**
 * @file Camera.h
 * @brief Camera data, view/projection matrices (Vulkan), FPS controller.
 *
 * Ticket: M03.0 — Camera & Frustum Culling MVP.
 *
 * Camera = data (pos, yaw, pitch, FOV, aspect, near, far).
 * View matrix from lookAt; projection matrix perspective Vulkan (Y flip, Z [0,1]).
 * CameraController = behavior (WASD + mouse look).
 */

#include <cstdint>

namespace engine::render {

/**
 * @brief Camera data: position, orientation (yaw/pitch), projection params.
 *
 * Yaw/pitch in radians. Right-handed: +Y up, -Z forward, yaw around Y, pitch around X.
 */
struct Camera {
    float position[3]{0.0f, 0.0f, 0.0f};
    float yaw   = 0.0f;   ///< Radians, rotation around world Y.
    float pitch = 0.0f;   ///< Radians, rotation around local X (clamped ±89°).
    float fovY  = 60.0f * 3.1415926535f / 180.0f;
    float aspect = 16.0f / 9.0f;
    float nearZ  = 0.1f;
    float farZ   = 1000.0f;
};

/**
 * @brief Fills the view matrix (column-major) from camera position and yaw/pitch.
 *
 * @param cam  Camera state.
 * @param out  Column-major 4x4 matrix (16 floats).
 */
void ComputeViewMatrix(const Camera& cam, float out[16]);

/**
 * @brief Fills the projection matrix (column-major) for Vulkan (Y flip, Z [0,1]).
 *
 * @param cam  Camera state (fovY, aspect, nearZ, farZ).
 * @param out  Column-major 4x4 matrix (16 floats).
 */
void ComputeProjectionMatrix(const Camera& cam, float out[16]);

/**
 * @brief Input state passed to the FPS controller (avoids platform dependency).
 */
struct CameraControllerInput {
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    bool keyW = false, keyA = false, keyS = false, keyD = false;
    bool keyShift = false;
};

/**
 * @brief FPS camera controller: WASD move, mouse yaw/pitch.
 *
 * Walk 5 m/s, run 10 m/s (shift). Pitch clamped to ±89°.
 * Mouse sensitivity from config "camera.mouse_sensitivity" (rad/pixel).
 */
class CameraController {
public:
    CameraController() = default;

    /**
     * @brief Updates camera from input state and dt.
     *
     * @param cam   Camera to update (position, yaw, pitch).
     * @param input Input state (mouse delta, keys).
     * @param dt    Delta time in seconds.
     */
    void Update(Camera& cam, const CameraControllerInput& input, float dt);

private:
    static constexpr float kPitchMin = -89.0f * 3.1415926535f / 180.0f;
    static constexpr float kPitchMax = +89.0f * 3.1415926535f / 180.0f;
};

} // namespace engine::render
