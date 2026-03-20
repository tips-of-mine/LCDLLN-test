#include "engine/gameplay/ThirdPersonCamera.h"
#include "engine/platform/Input.h"
#include "engine/core/Log.h"

#include <cmath>

namespace engine::gameplay
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float k2Pi = 2.0f * kPi;

		/// Convert degrees to radians.
		inline float DegToRad(float deg) { return deg * kPi / 180.0f; }

		/// Clamp value between lo and hi.
		inline float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		/// Exponential approach (spring damping): moves current toward target each frame.
		inline float SpringLerp(float current, float target, float stiffness, float dt)
		{
			const float alpha = Clamp(1.0f - std::exp(-stiffness * dt), 0.0f, 1.0f);
			return current + (target - current) * alpha;
		}
	}

	ThirdPersonCamera::ThirdPersonCamera() = default;

	ThirdPersonCamera::~ThirdPersonCamera()
	{
		if (m_initialized)
		{
			LOG_INFO(Gameplay, "[ThirdPersonCamera] Destroyed");
		}
	}

	void ThirdPersonCamera::Init(const Config& cfg)
	{
		m_cfg            = cfg;
		m_desiredDistance = cfg.defaultDistance;
		m_currentDistance = cfg.defaultDistance;
		m_yaw             = 0.0f;
		// Start pitch at mid-range between min and max.
		m_pitch           = DegToRad((cfg.pitchMinDeg + cfg.pitchMaxDeg) * 0.5f);
		m_initialized     = true;

		LOG_INFO(Gameplay,
			"[ThirdPersonCamera] Init OK (dist={:.1f}m, zoom=[{:.1f},{:.1f}]m, "
			"pitch=[{:.0f},{:.0f}]deg, spring={:.1f}, sphereR={:.2f}m)",
			cfg.defaultDistance, cfg.minDistance, cfg.maxDistance,
			cfg.pitchMinDeg, cfg.pitchMaxDeg,
			cfg.springStiffness, cfg.sphereCastRadius);
	}

	void ThirdPersonCamera::Update(
		const engine::math::Vec3&  targetPos,
		engine::platform::Input&   input,
		float                      dt,
		float                      mouseSensitivityRadPerPixel,
		const ICameraCollider*     collider,
		engine::render::Camera&    outCamera)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[ThirdPersonCamera] Update called before Init — skipping");
			return;
		}

		// ── 1. Rotate yaw/pitch from mouse delta ─────────────────────────────
		// Only rotate when right mouse button is held (MMO standard).
		if (input.IsMouseDown(engine::platform::MouseButton::Right))
		{
			const float sens = mouseSensitivityRadPerPixel;
			m_yaw   += static_cast<float>(input.MouseDeltaX()) * sens;
			m_pitch += static_cast<float>(input.MouseDeltaY()) * sens;
		}

		// Keep yaw in [0, 2π) to avoid float drift over time.
		m_yaw = std::fmod(m_yaw, k2Pi);
		if (m_yaw < 0.0f) m_yaw += k2Pi;

		// Clamp pitch to configured limits (avoids gimbal lock extremes).
		const float pitchMin = DegToRad(m_cfg.pitchMinDeg);
		const float pitchMax = DegToRad(m_cfg.pitchMaxDeg);
		m_pitch = Clamp(m_pitch, pitchMin, pitchMax);

		// ── 2. Zoom via mouse scroll wheel ────────────────────────────────────
		const int scrollDelta = input.MouseScrollDelta();
		if (scrollDelta != 0)
		{
			// Positive scroll = wheel up = zoom in (decrease distance).
			m_desiredDistance -= static_cast<float>(scrollDelta) * m_cfg.zoomSpeed;
			m_desiredDistance  = Clamp(m_desiredDistance, m_cfg.minDistance, m_cfg.maxDistance);
			LOG_DEBUG(Gameplay, "[ThirdPersonCamera] Zoom -> desiredDist={:.2f}m", m_desiredDistance);
		}

		// ── 3. Compute focus point (character feet + vertical offset) ─────────
		const engine::math::Vec3 focusPoint(
			targetPos.x,
			targetPos.y + m_cfg.targetOffsetY,
			targetPos.z);

		// ── 4. Compute camera direction offset (spherical coordinates) ─────────
		// Convention from Camera.cpp: forward = (-sin(yaw)*cos(pitch), -sin(pitch), -cos(yaw)*cos(pitch))
		// Camera sits opposite to forward direction: camOffset = +forward_components * distance.
		const float cy = std::cos(m_yaw);
		const float sy = std::sin(m_yaw);
		const float cp = std::cos(m_pitch);
		const float sp = std::sin(m_pitch);

		// Unit vector from focusPoint toward camera (behind the character).
		const engine::math::Vec3 camDir(sy * cp, sp, cy * cp);

		// ── 5. Sphere-cast collision: clamp desired distance if obstructed ─────
		float collisionDistance = m_desiredDistance;
		if (collider != nullptr && m_desiredDistance > 0.0f)
		{
			const engine::math::Vec3 idealCamPos(
				focusPoint.x + camDir.x * m_desiredDistance,
				focusPoint.y + camDir.y * m_desiredDistance,
				focusPoint.z + camDir.z * m_desiredDistance);

			ICameraCollider::SweepHit hit{};
			const bool hasHit = collider->SweepSphere(
				m_cfg.sphereCastRadius,
				focusPoint,
				idealCamPos,
				hit);

			if (hasHit && hit.fraction < 1.0f)
			{
				// Pull camera closer to prevent geometry clipping.
				collisionDistance = hit.fraction * m_desiredDistance;
				collisionDistance = Clamp(collisionDistance, 0.0f, m_desiredDistance);
				LOG_DEBUG(Gameplay,
					"[ThirdPersonCamera] Collision: dist clamped to {:.2f}m (frac={:.3f})",
					collisionDistance, hit.fraction);
			}
		}

		// ── 6. Spring-damping smooth interpolation toward collision distance ───
		m_currentDistance = SpringLerp(m_currentDistance, collisionDistance, m_cfg.springStiffness, dt);

		// ── 7. Write final camera position, yaw and pitch ─────────────────────
		outCamera.position = engine::math::Vec3(
			focusPoint.x + camDir.x * m_currentDistance,
			focusPoint.y + camDir.y * m_currentDistance,
			focusPoint.z + camDir.z * m_currentDistance);

		outCamera.yaw   = m_yaw;
		outCamera.pitch = m_pitch;
	}
}
