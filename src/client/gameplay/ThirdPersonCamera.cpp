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

	void ThirdPersonCamera::SetCombatMode(bool combat)
	{
		if (m_combatMode == combat)
			return;

		m_combatMode = combat;
		LOG_INFO(Gameplay, "[ThirdPersonCamera] CombatMode {}", combat ? "ON" : "OFF");
	}

	void ThirdPersonCamera::Init(const Config& cfg)
	{
		m_cfg = cfg;

		// Validate and clamp configuration (avoid broken camera math).
		if (m_cfg.springStiffness <= 0.0f)
		{
			LOG_WARN(Gameplay, "[ThirdPersonCamera] springStiffness invalid ({:.3f}) — fallback to 10.0", m_cfg.springStiffness);
			m_cfg.springStiffness = 10.0f;
		}
		if (m_cfg.followStiffness <= 0.0f)
		{
			LOG_WARN(Gameplay, "[ThirdPersonCamera] followStiffness invalid ({:.3f}) — fallback to 35.0", m_cfg.followStiffness);
			m_cfg.followStiffness = 35.0f;
		}
		if (m_cfg.followDamping < 0.0f)
		{
			LOG_WARN(Gameplay, "[ThirdPersonCamera] followDamping invalid ({:.3f}) — fallback to 12.0", m_cfg.followDamping);
			m_cfg.followDamping = 12.0f;
		}
		m_cfg.lookAheadSeconds = Clamp(m_cfg.lookAheadSeconds, 0.5f, 1.0f);
		if (m_cfg.lookAheadSeconds != cfg.lookAheadSeconds)
			LOG_WARN(Gameplay, "[ThirdPersonCamera] lookAheadSeconds out of range ({:.3f}) — clamped to {:.3f}", cfg.lookAheadSeconds, m_cfg.lookAheadSeconds);
		m_cfg.lookAheadMaxMeters = Clamp(m_cfg.lookAheadMaxMeters, 2.0f, 3.0f);
		if (m_cfg.lookAheadMaxMeters != cfg.lookAheadMaxMeters)
			LOG_WARN(Gameplay, "[ThirdPersonCamera] lookAheadMaxMeters out of range ({:.3f}) — clamped to {:.3f}", cfg.lookAheadMaxMeters, m_cfg.lookAheadMaxMeters);

		m_cfg.defaultDistance = Clamp(m_cfg.defaultDistance, m_cfg.minDistance, m_cfg.maxDistance);
		m_desiredDistance = m_cfg.defaultDistance;
		m_currentDistance = m_cfg.defaultDistance;
		m_yaw = 0.0f;
		// Start pitch at mid-range between min and max.
		m_pitch = DegToRad((m_cfg.pitchMinDeg + m_cfg.pitchMaxDeg) * 0.5f);

		m_focusPos           = engine::math::Vec3{};
		m_focusVel           = engine::math::Vec3{};
		m_prevTargetPos      = engine::math::Vec3{};
		m_prevTargetValid    = false;
		m_focusInitialized   = false;
		m_combatMode         = false;

		m_initialized = true;

		LOG_INFO(Gameplay,
			"[ThirdPersonCamera] Init OK (dist={:.1f}m, zoom=[{:.1f},{:.1f}]m, "
			"pitch=[{:.0f},{:.0f}]deg, spring={:.1f}, sphereR={:.2f}m)",
			m_cfg.defaultDistance, m_cfg.minDistance, m_cfg.maxDistance,
			m_cfg.pitchMinDeg, m_cfg.pitchMaxDeg,
			m_cfg.springStiffness, m_cfg.sphereCastRadius);

		LOG_INFO(Gameplay,
			"[ThirdPersonCamera] Follow&lookAhead (stiff={:.1f}, damp={:.1f}, lookAhead={:.2f}s, max={:.2f}m, combatDist={:.1f}m, combatPitch={:.0f}deg, lock={})",
			m_cfg.followStiffness, m_cfg.followDamping,
			m_cfg.lookAheadSeconds, m_cfg.lookAheadMaxMeters,
			m_cfg.combatDistance, m_cfg.combatPitchDeg,
			m_cfg.lockTargetInCombat ? "true" : "false");
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
		const bool allowOrbitRotation = !(m_combatMode && m_cfg.lockTargetInCombat);
		if (allowOrbitRotation && input.IsMouseDown(engine::platform::MouseButton::Right))
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
		if (!m_combatMode && scrollDelta != 0)
		{
			// Positive scroll = wheel up = zoom in (decrease distance).
			m_desiredDistance -= static_cast<float>(scrollDelta) * m_cfg.zoomSpeed;
			m_desiredDistance  = Clamp(m_desiredDistance, m_cfg.minDistance, m_cfg.maxDistance);
			LOG_DEBUG(Gameplay, "[ThirdPersonCamera] Zoom -> desiredDist={:.2f}m", m_desiredDistance);
		}

		// Combat mode override: tighten distance and force pitch.
		if (m_combatMode)
		{
			m_desiredDistance = Clamp(m_cfg.combatDistance, m_cfg.minDistance, m_cfg.maxDistance);
			const float combatPitchRad = DegToRad(m_cfg.combatPitchDeg);
			m_pitch = Clamp(combatPitchRad, pitchMin, pitchMax);
		}

		// ── 3. Look-ahead + spring-damper follow for focus point ─────────────
		engine::math::Vec3 projectedVelocityXZ{};
		if (dt > 0.0f && m_prevTargetValid)
		{
			const float invDt = 1.0f / dt;
			const engine::math::Vec3 rawVel = (targetPos - m_prevTargetPos) * invDt;
			projectedVelocityXZ = engine::math::Vec3(rawVel.x, 0.0f, rawVel.z);
		}

		// Look-ahead offset based on projected velocity.
		engine::math::Vec3 lookAheadOffset = projectedVelocityXZ * m_cfg.lookAheadSeconds;
		const float maxLookAheadSq = m_cfg.lookAheadMaxMeters * m_cfg.lookAheadMaxMeters;
		const float lookAheadLenSq = lookAheadOffset.LengthSq();
		if (lookAheadLenSq > maxLookAheadSq)
		{
			// Clamp look-ahead to avoid camera jumping too far ahead.
			const float lookAheadLen = std::sqrt(lookAheadLenSq);
			if (lookAheadLen > 0.0f)
			{
				const float scale = m_cfg.lookAheadMaxMeters / lookAheadLen;
				lookAheadOffset = lookAheadOffset * scale;
				LOG_DEBUG(Gameplay, "[ThirdPersonCamera] Look-ahead clamped to {:.2f}m", m_cfg.lookAheadMaxMeters);
			}
		}

		const engine::math::Vec3 followTarget(
			targetPos.x + lookAheadOffset.x,
			targetPos.y + m_cfg.targetOffsetY,
			targetPos.z + lookAheadOffset.z);

		if (!m_focusInitialized)
		{
			m_focusPos = followTarget;
			m_focusVel = engine::math::Vec3{};
			m_focusInitialized = true;
		}
		else
		{
			if (dt > 0.0f)
			{
				const engine::math::Vec3 delta = followTarget - m_focusPos;
				const engine::math::Vec3 accel = delta * m_cfg.followStiffness - m_focusVel * m_cfg.followDamping;
				m_focusVel = m_focusVel + accel * dt;
				m_focusPos = m_focusPos + m_focusVel * dt;
			}
			else
			{
				// No time step: avoid NaNs, snap focus.
				m_focusPos = followTarget;
				m_focusVel = engine::math::Vec3{};
			}
		}

		m_prevTargetPos = targetPos;
		m_prevTargetValid = true;

		const engine::math::Vec3 focusPoint = m_focusPos;

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
