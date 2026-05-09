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

	/// Third-person orbit camera controller for MMO gameplay.
	///
	/// Orbits around a target (character) position, handles right-mouse-button
	/// drag for yaw/pitch rotation, mouse wheel for distance zoom, and sphere-cast
	/// collision to prevent camera clipping through geometry.
	/// Smooth interpolation is applied via spring damping.
	class ThirdPersonCamera
	{
	public:
		/// Runtime configuration for the third-person camera.
		struct Config
		{
			/// Default orbit distance in metres.
			float defaultDistance = 5.0f;
			/// Minimum zoom distance in metres.
			float minDistance = 2.0f;
			/// Maximum zoom distance in metres.
			float maxDistance = 10.0f;
			/// Minimum pitch angle in degrees (negative = camera below focus, looking up).
			float pitchMinDeg = -30.0f;
			/// Maximum pitch angle in degrees (positive = camera above focus, looking down).
			float pitchMaxDeg = 80.0f;
			/// Vertical offset above character feet position in metres.
			float targetOffsetY = 1.5f;
			/// Spring stiffness for smooth distance interpolation (higher = snappier).
			float springStiffness = 10.0f;
			/// Spring stiffness for smooth follow of the focus point (higher = snappier).
			float followStiffness = 35.0f;
			/// Damping coefficient for smooth follow of the focus point (higher = less overshoot).
			float followDamping = 12.0f;
			/// Look-ahead time multiplier: lookAhead = velocityProjected * lookAheadSeconds.
			/// Expected range from ticket: 0.5..1.0 seconds.
			float lookAheadSeconds = 0.75f;
			/// Clamp look-ahead distance to avoid extreme camera offsets (ticket: 2..3m).
			float lookAheadMaxMeters = 2.75f;
			/// Zoom speed in metres per scroll tick.
			float zoomSpeed = 1.0f;
			/// Radius of the sphere used in the collision sweep in metres.
			float sphereCastRadius = 0.2f;

			/// Combat mode: override orbit distance to tighten camera.
			float combatDistance = 3.5f;
			/// Combat mode: override pitch so the target stays framed.
			float combatPitchDeg = 25.0f;
			/// If true, ignore mouse orbit rotation while in combat.
			bool lockTargetInCombat = false;
		};

		ThirdPersonCamera();
		~ThirdPersonCamera();

		/// Initialise the controller with given configuration.
		/// Must be called before Update().
		void Init(const Config& cfg);

		/// Update camera orbit, input rotation, zoom and collision.
		/// \param targetPos                   World-space position of the tracked target (character feet).
		/// \param input                        Input system for mouse delta and scroll.
		/// \param dt                           Delta time in seconds.
		/// \param mouseSensitivityRadPerPixel  Mouse sensitivity (radians per pixel).
		/// \param collider                     Optional world collider for sphere-cast occlusion (may be nullptr).
		/// \param outCamera                    Camera struct that receives updated position, yaw, and pitch.
		void Update(const engine::math::Vec3& targetPos,
			engine::platform::Input& input,
			float dt,
			float mouseSensitivityRadPerPixel,
			const ICameraCollider* collider,
			engine::render::Camera& outCamera);

		/// Sets camera combat/exploration state.
		/// \param combat true -> tighten distance and override pitch.
		void SetCombatMode(bool combat);

		/// Returns current yaw angle in radians.
		float GetYaw()             const { return m_yaw; }
		/// Returns current pitch angle in radians.
		float GetPitch()           const { return m_pitch; }
		/// Returns current smoothed orbit distance in metres.
		float GetCurrentDistance() const { return m_currentDistance; }

	private:
		Config m_cfg{};
		float  m_yaw             = 0.0f; ///< current yaw (radians)
		float  m_pitch           = 0.0f; ///< current pitch (radians)
		float  m_desiredDistance = 5.0f; ///< zoom target before collision (metres)
		float  m_currentDistance = 5.0f; ///< smoothed distance after collision (metres)
		engine::math::Vec3 m_focusPos{};  ///< Smoothed focus point (character pos + look-ahead + up offset)
		engine::math::Vec3 m_focusVel{};  ///< Follow spring velocity
		engine::math::Vec3 m_prevTargetPos{};
		bool m_prevTargetValid = false;
		bool m_focusInitialized = false;
		bool m_combatMode = false;
		bool   m_initialized     = false;
	};
}
