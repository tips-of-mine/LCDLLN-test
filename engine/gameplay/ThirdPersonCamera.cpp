#include "engine/gameplay/ThirdPersonCamera.h"
#include "engine/platform/Input.h"
#include "engine/core/Log.h"

#include <cmath>

namespace engine::gameplay
{
	namespace
	{
		constexpr float kPi  = 3.14159265f;
		constexpr float k2Pi = 2.0f * kPi;

		/// Convert degrees to radians.
		inline float DegToRad(float deg) { return deg * kPi / 180.0f; }

		/// Clamp value between lo and hi.
		inline float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		/// Advance a spring-damper scalar by one time step.
		/// Returns the new position; updates velocity in-place.
		/// Uses the semi-implicit Euler integration (stable for typical game dt values).
		inline float SpringStep(float current, float target,
		                        float& velocity,
		                        float stiffness, float damping,
		                        float dt)
		{
			const float force = stiffness * (target - current) - damping * velocity;
			velocity         += force * dt;
			return current   +  velocity * dt;
		}

		/// Advance a spring-damper Vec3 by one time step.
		/// Returns the new position; updates velocity in-place.
		inline engine::math::Vec3 SpringStepVec3(
			const engine::math::Vec3& current,
			const engine::math::Vec3& target,
			engine::math::Vec3&       velocity,
			float stiffness, float damping, float dt)
		{
			const engine::math::Vec3 force(
				stiffness * (target.x - current.x) - damping * velocity.x,
				stiffness * (target.y - current.y) - damping * velocity.y,
				stiffness * (target.z - current.z) - damping * velocity.z);
			velocity.x += force.x * dt;
			velocity.y += force.y * dt;
			velocity.z += force.z * dt;
			return engine::math::Vec3(
				current.x + velocity.x * dt,
				current.y + velocity.y * dt,
				current.z + velocity.z * dt);
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
		m_cfg             = cfg;
		m_desiredDistance = cfg.defaultDistance;
		m_currentDistance = cfg.defaultDistance;
		m_distVelocity    = 0.0f;
		m_yaw             = 0.0f;
		// Start pitch at mid-range between min and max.
		m_pitch           = DegToRad((cfg.pitchMinDeg + cfg.pitchMaxDeg) * 0.5f);
		m_smoothedFocus   = engine::math::Vec3(0.0f, 0.0f, 0.0f);
		m_focusVelocity   = engine::math::Vec3(0.0f, 0.0f, 0.0f);
		m_state           = CameraState::Exploration;
		m_firstUpdate     = true;
		m_initialized     = true;

		LOG_INFO(Gameplay,
			"[ThirdPersonCamera] Init OK "
			"(dist={:.1f}m, zoom=[{:.1f},{:.1f}]m, pitch=[{:.0f},{:.0f}]deg, "
			"distSpring={:.1f}/{:.1f}, followSpring={:.1f}/{:.1f}, "
			"lookAhead={:.2f}s/max={:.1f}m, combat={:.1f}m/{:.0f}deg)",
			cfg.defaultDistance, cfg.minDistance, cfg.maxDistance,
			cfg.pitchMinDeg, cfg.pitchMaxDeg,
			cfg.distStiffness, cfg.distDamping,
			cfg.followStiffness, cfg.followDamping,
			cfg.lookAheadTime, cfg.lookAheadMaxDist,
			cfg.combatDistance, cfg.combatPitchDeg);
	}

	void ThirdPersonCamera::SetState(CameraState state)
	{
		if (m_state == state) return;
		m_state = state;
		const char* name = (state == CameraState::Combat) ? "Combat" : "Exploration";
		LOG_INFO(Gameplay, "[ThirdPersonCamera] State -> {}", name);
	}

	void ThirdPersonCamera::Update(
		const engine::math::Vec3&  targetPos,
		const engine::math::Vec3&  targetVelocity,
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
		// Only rotate when right mouse button is held (MMO convention).
		if (input.IsMouseDown(engine::platform::MouseButton::Right))
		{
			const float sens = mouseSensitivityRadPerPixel;
			m_yaw   += static_cast<float>(input.MouseDeltaX()) * sens;
			m_pitch += static_cast<float>(input.MouseDeltaY()) * sens;
		}

		// Keep yaw in [0, 2π) to avoid float drift over time.
		m_yaw = std::fmod(m_yaw, k2Pi);
		if (m_yaw < 0.0f) m_yaw += k2Pi;

		// ── 2. Combat state: blend pitch toward combat target ─────────────────
		if (m_state == CameraState::Combat)
		{
			const float combatPitch = DegToRad(m_cfg.combatPitchDeg);
			// Only override pitch when RMB is not held so the player keeps control.
			if (!input.IsMouseDown(engine::platform::MouseButton::Right))
			{
				const float alpha = Clamp(1.0f - std::exp(-m_cfg.combatPitchStiffness * dt), 0.0f, 1.0f);
				m_pitch += (combatPitch - m_pitch) * alpha;
			}
		}

		// Clamp pitch to configured limits (avoids gimbal lock extremes).
		const float pitchMin = DegToRad(m_cfg.pitchMinDeg);
		const float pitchMax = DegToRad(m_cfg.pitchMaxDeg);
		m_pitch = Clamp(m_pitch, pitchMin, pitchMax);

		// ── 3. Zoom via mouse scroll wheel ────────────────────────────────────
		// In Combat state the distance is overridden; scroll is ignored.
		if (m_state == CameraState::Exploration)
		{
			const int scrollDelta = input.MouseScrollDelta();
			if (scrollDelta != 0)
			{
				// Positive scroll = wheel up = zoom in (decrease distance).
				m_desiredDistance -= static_cast<float>(scrollDelta) * m_cfg.zoomSpeed;
				m_desiredDistance  = Clamp(m_desiredDistance, m_cfg.minDistance, m_cfg.maxDistance);
				LOG_DEBUG(Gameplay,
					"[ThirdPersonCamera] Zoom -> desiredDist={:.2f}m", m_desiredDistance);
			}
		}
		else // CameraState::Combat
		{
			// Override zoom with combat distance.
			m_desiredDistance = m_cfg.combatDistance;
		}

		// ── 4. Look-ahead offset based on character velocity (XZ plane only) ──
		// offset = velocityXZ * lookAheadTime, clamped to lookAheadMaxDist.
		engine::math::Vec3 lookAheadOffset(
			targetVelocity.x * m_cfg.lookAheadTime,
			0.0f,
			targetVelocity.z * m_cfg.lookAheadTime);
		{
			const float len = lookAheadOffset.Length();
			if (len > m_cfg.lookAheadMaxDist && len > 0.0f)
			{
				lookAheadOffset = lookAheadOffset * (m_cfg.lookAheadMaxDist / len);
			}
		}

		// ── 5. Spring-damper follow: smooth focus point toward target + offsets ─
		// Target focus = character feet + look-ahead (XZ) + vertical offset (Y).
		const engine::math::Vec3 targetFocus(
			targetPos.x + lookAheadOffset.x,
			targetPos.y + m_cfg.targetOffsetY,
			targetPos.z + lookAheadOffset.z);

		if (m_firstUpdate)
		{
			// Snap to target on the very first frame to avoid a jump from origin.
			m_smoothedFocus = targetFocus;
			m_focusVelocity = engine::math::Vec3(0.0f, 0.0f, 0.0f);
			m_firstUpdate   = false;
		}
		else
		{
			m_smoothedFocus = SpringStepVec3(
				m_smoothedFocus, targetFocus,
				m_focusVelocity,
				m_cfg.followStiffness, m_cfg.followDamping,
				dt);
		}

		// ── 6. Compute camera direction (spherical offset from smoothed focus) ──
		// Convention from Camera.cpp: forward = (-sin(yaw)*cos(pitch), -sin(pitch), -cos(yaw)*cos(pitch))
		// Camera sits opposite to forward: camDir = (sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch)).
		const float cy = std::cos(m_yaw);
		const float sy = std::sin(m_yaw);
		const float cp = std::cos(m_pitch);
		const float sp = std::sin(m_pitch);
		const engine::math::Vec3 camDir(sy * cp, sp, cy * cp);

		// ── 7. Sphere-cast collision: clamp desired distance if obstructed ─────
		float collisionDistance = m_desiredDistance;
		if (collider != nullptr && m_desiredDistance > 0.0f)
		{
			const engine::math::Vec3 idealCamPos(
				m_smoothedFocus.x + camDir.x * m_desiredDistance,
				m_smoothedFocus.y + camDir.y * m_desiredDistance,
				m_smoothedFocus.z + camDir.z * m_desiredDistance);

			ICameraCollider::SweepHit hit{};
			const bool hasHit = collider->SweepSphere(
				m_cfg.sphereCastRadius,
				m_smoothedFocus,
				idealCamPos,
				hit);

			if (hasHit && hit.fraction < 1.0f)
			{
				collisionDistance = hit.fraction * m_desiredDistance;
				collisionDistance = Clamp(collisionDistance, 0.0f, m_desiredDistance);
				LOG_DEBUG(Gameplay,
					"[ThirdPersonCamera] Collision: dist clamped to {:.2f}m (frac={:.3f})",
					collisionDistance, hit.fraction);
			}
		}

		// ── 8. Spring-damper: smooth distance toward collision distance ─────────
		m_currentDistance = SpringStep(
			m_currentDistance, collisionDistance,
			m_distVelocity,
			m_cfg.distStiffness, m_cfg.distDamping,
			dt);
		m_currentDistance = Clamp(m_currentDistance, 0.0f, m_cfg.maxDistance);

		// ── 9. Write final camera position, yaw and pitch ─────────────────────
		outCamera.position = engine::math::Vec3(
			m_smoothedFocus.x + camDir.x * m_currentDistance,
			m_smoothedFocus.y + camDir.y * m_currentDistance,
			m_smoothedFocus.z + camDir.z * m_currentDistance);

		outCamera.yaw   = m_yaw;
		outCamera.pitch = m_pitch;
	}
}
