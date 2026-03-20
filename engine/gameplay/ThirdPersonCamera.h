#pragma once

#include "engine/core/Log.h"
#include "engine/math/Math.h"
#include "engine/render/Camera.h"

namespace engine::platform { class Input; }

namespace engine::gameplay
{
	/// Sphere-cast collision interface for camera occlusion queries.
	/// Implementors sweep a sphere from start to end and report the first hit fraction.
	class ICameraCollider
	{
	public:
		struct SweepHit
		{
			bool  hit      = false;
			float fraction = 1.0f; ///< 0..1 (1 = no hit)
		};

		virtual ~ICameraCollider() = default;

		/// Sweep a sphere of given radius from start to end in world space.
		/// \return true if a hit was detected; outHit.fraction in [0,1].
		virtual bool SweepSphere(float radius,
			const engine::math::Vec3& start,
			const engine::math::Vec3& end,
			SweepHit& outHit) const = 0;
	};

	/// Camera operating state — drives distance and pitch override during transitions.
	enum class CameraState
	{
		Exploration, ///< Default roaming mode: full zoom range, free look.
		Combat,      ///< Combat mode: tighter orbit distance, blended pitch override.
	};

	/// Third-person orbit camera controller for MMO gameplay.
	///
	/// Features (M27.1 + M27.2):
	/// - Orbit with yaw/pitch (right-mouse-button drag) and zoom (scroll wheel)
	/// - Sphere-cast collision to prevent geometry clipping
	/// - Spring-damper for focus-point follow (smooth character tracking)
	/// - Look-ahead offset based on character velocity
	/// - State transitions: Exploration / Combat
	class ThirdPersonCamera
	{
	public:
		/// Full runtime configuration, including M27.2 spring-damper and look-ahead params.
		struct Config
		{
			// ── Orbit ─────────────────────────────────────────────────────────
			/// Default orbit distance in metres.
			float defaultDistance  = 5.0f;
			/// Minimum zoom distance in metres.
			float minDistance      = 2.0f;
			/// Maximum zoom distance in metres.
			float maxDistance      = 10.0f;
			/// Minimum pitch angle in degrees (negative = camera below focus, looking up).
			float pitchMinDeg      = -30.0f;
			/// Maximum pitch angle in degrees (positive = camera above focus, looking down).
			float pitchMaxDeg      = 80.0f;
			/// Vertical offset above character feet position in metres.
			float targetOffsetY    = 1.5f;
			/// Zoom speed in metres per scroll tick.
			float zoomSpeed        = 1.0f;
			/// Radius of the sphere used in the collision sweep in metres.
			float sphereCastRadius = 0.2f;

			// ── Spring-damper: orbit distance ─────────────────────────────────
			/// Spring stiffness controlling how fast the camera reaches target distance.
			float distStiffness = 10.0f;
			/// Damping coefficient for the distance spring (critical: 2*sqrt(distStiffness)).
			float distDamping   = 6.0f;

			// ── Spring-damper: focus-point follow ────────────────────────────
			/// Spring stiffness for the focus point following the character position.
			float followStiffness = 8.0f;
			/// Damping coefficient for the focus-point spring.
			float followDamping   = 4.0f;

			// ── Look-ahead ───────────────────────────────────────────────────
			/// Look-ahead time horizon in seconds (0.5–1.0 recommended by spec).
			float lookAheadTime    = 0.7f;
			/// Maximum look-ahead offset in metres (clamp, spec: 2–3 m).
			float lookAheadMaxDist = 2.5f;

			// ── Combat mode ──────────────────────────────────────────────────
			/// Orbit distance used when in Combat state.
			float combatDistance = 3.5f;
			/// Target pitch angle in degrees while in Combat state.
			float combatPitchDeg = 15.0f;
			/// Spring stiffness for blending pitch toward the combat target.
			float combatPitchStiffness = 5.0f;
		};

		ThirdPersonCamera();
		~ThirdPersonCamera();

		/// Initialise the controller with given configuration.
		/// Must be called before Update().
		void Init(const Config& cfg);

		/// Update camera follow, look-ahead, orbit, zoom, collision and state transitions.
		/// \param targetPos                   World-space feet position of the tracked character.
		/// \param targetVelocity              World-space velocity of the character (used for look-ahead).
		/// \param input                        Input system for mouse delta and scroll.
		/// \param dt                           Delta time in seconds.
		/// \param mouseSensitivityRadPerPixel  Mouse sensitivity (radians per pixel).
		/// \param collider                     Optional sphere-cast collider (may be nullptr).
		/// \param outCamera                    Receives updated position, yaw, and pitch.
		void Update(const engine::math::Vec3& targetPos,
			const engine::math::Vec3& targetVelocity,
			engine::platform::Input& input,
			float dt,
			float mouseSensitivityRadPerPixel,
			const ICameraCollider* collider,
			engine::render::Camera& outCamera);

		/// Switch camera state (Exploration / Combat) with smooth transition.
		void SetState(CameraState state);

		/// Returns current yaw angle in radians.
		float       GetYaw()             const { return m_yaw; }
		/// Returns current pitch angle in radians.
		float       GetPitch()           const { return m_pitch; }
		/// Returns current smoothed orbit distance in metres.
		float       GetCurrentDistance() const { return m_currentDistance; }
		/// Returns active camera state.
		CameraState GetState()           const { return m_state; }

	private:
		Config m_cfg{};

		// Orbit state
		float  m_yaw             = 0.0f; ///< current yaw (radians)
		float  m_pitch           = 0.0f; ///< current pitch (radians)
		float  m_desiredDistance = 5.0f; ///< zoom target before collision (metres)
		float  m_currentDistance = 5.0f; ///< smoothed distance after collision (metres)
		float  m_distVelocity    = 0.0f; ///< spring velocity for distance smoothing

		// Focus-point spring state
		engine::math::Vec3 m_smoothedFocus{};   ///< spring-followed focus point in world space
		engine::math::Vec3 m_focusVelocity{};   ///< spring velocity for focus-point follow

		// State
		CameraState m_state       = CameraState::Exploration;
		bool        m_initialized = false;
		bool        m_firstUpdate = true; ///< snap focus on the very first Update call
	};
}
