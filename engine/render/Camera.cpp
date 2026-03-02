#include "engine/render/Camera.h"
#include "engine/platform/Input.h"
#include <cmath>

namespace engine::render
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
	}

	engine::math::Mat4 Camera::ComputeViewMatrix() const
	{
		const float cy = std::cos(yaw);
		const float sy = std::sin(yaw);
		const float cp = std::cos(pitch);
		const float sp = std::sin(pitch);
		// Forward in world (camera looks along -Z in camera space, so forward = -view direction).
		const engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
		// Right = cross(world up, forward); world up = (0, 1, 0).
		engine::math::Vec3 right(forward.z, 0.0f, -forward.x);
		float rlen = right.Length();
		if (rlen > 0.0f) right = right * (1.0f / rlen);
		else right = engine::math::Vec3(1.0f, 0.0f, 0.0f);
		// Up = cross(forward, right).
		engine::math::Vec3 up(forward.y * right.z - forward.z * right.y,
			forward.z * right.x - forward.x * right.z,
			forward.x * right.y - forward.y * right.x);
		float ulen = up.Length();
		if (ulen > 0.0f) up = up * (1.0f / ulen);
		else up = engine::math::Vec3(0.0f, 1.0f, 0.0f);

		engine::math::Mat4 V;
		V.m[0] = right.x;  V.m[1] = right.y;  V.m[2] = right.z;  V.m[3] = 0.0f;
		V.m[4] = up.x;     V.m[5] = up.y;     V.m[6] = up.z;     V.m[7] = 0.0f;
		V.m[8] = -forward.x; V.m[9] = -forward.y; V.m[10] = -forward.z; V.m[11] = 0.0f;
		V.m[12] = -(right.x * position.x + right.y * position.y + right.z * position.z);
		V.m[13] = -(up.x * position.x + up.y * position.y + up.z * position.z);
		V.m[14] = (forward.x * position.x + forward.y * position.y + forward.z * position.z);
		V.m[15] = 1.0f;
		return V;
	}

	engine::math::Mat4 Camera::ComputeProjectionMatrix() const
	{
		const float fovYRad = fovYDeg * kPi / 180.0f;
		return engine::math::Mat4::PerspectiveVulkan(fovYRad, aspect, nearZ, farZ);
	}

	void FpsCameraController::Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel, Camera& camera)
	{
		const float sens = static_cast<float>(mouseSensitivityRadPerPixel);
		camera.yaw += static_cast<float>(input.MouseDeltaX()) * sens;
		camera.pitch += static_cast<float>(input.MouseDeltaY()) * sens;
		if (camera.pitch < kPitchMin) camera.pitch = kPitchMin;
		if (camera.pitch > kPitchMax) camera.pitch = kPitchMax;

		const float speed = input.IsDown(engine::platform::Key::Shift) ? kRunSpeed : kWalkSpeed;
		const float dist = static_cast<float>(dt) * speed;
		const float cy = std::cos(camera.yaw);
		const float sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch);
		// Move in camera horizontal plane (yaw only for forward/back/left/right).
		const float forwardX = -sy * cp;
		const float forwardZ = -cy * cp;
		const float rightX = cy;
		const float rightZ = -sy;
		if (input.IsDown(engine::platform::Key::W))
		{
			camera.position.x += forwardX * dist;
			camera.position.z += forwardZ * dist;
		}
		if (input.IsDown(engine::platform::Key::S))
		{
			camera.position.x -= forwardX * dist;
			camera.position.z -= forwardZ * dist;
		}
		if (input.IsDown(engine::platform::Key::D))
		{
			camera.position.x += rightX * dist;
			camera.position.z += rightZ * dist;
		}
		if (input.IsDown(engine::platform::Key::A))
		{
			camera.position.x -= rightX * dist;
			camera.position.z -= rightZ * dist;
		}
	}
}
