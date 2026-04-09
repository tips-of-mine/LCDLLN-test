#pragma once

#include "engine/math/Math.h"
#include <cstdint>

namespace engine::platform { class Input; }

namespace engine::render
{
	/// Camera data: position, orientation (yaw/pitch), and projection parameters.
	struct Camera
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float yaw = 0.0f;   // radians, rotation around world Y
		float pitch = 0.0f; // radians, rotation around local X (-pi/2 .. +pi/2)

		float fovYDeg = 70.0f;  // vertical FOV in degrees (e.g. 60-90)
		float aspect = 16.0f / 9.0f;
		float nearZ = 0.1f;
		float farZ = 1000.0f;

		/// Builds view matrix (world -> camera space, right-handed, camera looks along -Z).
		engine::math::Mat4 ComputeViewMatrix() const;

		/// Builds perspective projection for Vulkan (Y down NDC, Z in [0,1]).
		engine::math::Mat4 ComputeProjectionMatrix() const;
	};

	/// FPS-style camera controller: WASD movement and mouse look.
	enum class MovementLayout : uint8_t
	{
		WASD = 0,
		ZQSD = 1
	};

	class FpsCameraController
	{
	public:
		/// Walk speed in m/s.
		static constexpr float kWalkSpeed = 5.0f;
		/// Run speed in m/s (when shift held).
		static constexpr float kRunSpeed = 10.0f;
		/// Pitch clamp in radians (avoid gimbal at ±90°).
		static constexpr float kPitchMin = -89.0f * 3.14159265f / 180.0f;
		static constexpr float kPitchMax = +89.0f * 3.14159265f / 180.0f;

		/// Updates camera from input and delta time. Mouse sensitivity in rad/pixel from config.
		/// Si \p scrollWheelAdjustsFov : la molette modifie le FOV vertical (éditeur monde).
		void Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel, bool invertY,
			MovementLayout layout, bool scrollWheelAdjustsFov, Camera& camera);
	};
}
