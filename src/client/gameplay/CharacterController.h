#pragma once

#include "engine/core/Log.h"
#include "engine/math/Math.h"

#include <cstdint>

namespace engine::gameplay
{
	/// World collision query needed by CharacterController.
	/// Implementations must perform a capsule sweep test and provide hit fraction in [0,1].
	class IWorldCollider
	{
	public:
		struct Capsule
		{
			float radius = 0.3f;  ///< metres
			float height = 1.8f;  ///< metres (sphere-to-sphere total height)
		};

		struct WaterQuery
		{
			bool inWater = false;
			// World-space Y of the water surface at the query location.
			float surfaceY = 0.0f;
			// Underwater depth in meters: positive when center is below surface.
			float depth = 0.0f;
		};

		struct SweepHit
		{
			bool hit = false;
			float fraction = 1.0f;     ///< 0..1 where 1 means "no hit"
			engine::math::Vec3 normal{ 0.0f, 1.0f, 0.0f }; ///< world-space collision normal
		};

		virtual ~IWorldCollider() = default;

		/// Sweep capsule center from startCenter to endCenter.
		/// \return true if a collision occurred; outHit.fraction in [0,1].
		virtual bool SweepCapsule(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const = 0;

		/// Optional water volume query for swimming/flying.
		/// Default implementation returns `inWater=false`.
		virtual bool QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const
		{
			(void)worldCenter;
			out = WaterQuery{};
			return false;
		}
	};

	struct MoveInput
	{
		/// Desired direction on XZ plane (y ignored). Length can be 0.
		engine::math::Vec3 moveDirXZ{ 0.0f, 0.0f, 0.0f };
		bool run = false;
		/// Jump edge trigger (true on the frame the player presses jump).
		bool jumpPressed = false;

		/// Swimming vertical control (maps to space/ctrl in caller).
		bool swimUpPressed = false;
		bool swimDownPressed = false;

		/// Flying mode toggle (optional, stamina-based).
		bool flyPressed = false;
	};

	class CharacterController final
	{
	public:
		struct Config
		{
			float walkSpeed = 5.0f;  ///< range: 4-6 m/s
			float runSpeed = 9.0f;   ///< range: 8-10 m/s
			float acceleration = 25.0f; ///< m/s^2
			float friction = 20.0f;     ///< m/s^2 (horizontal decel when no input)

			float gravity = -20.0f; ///< m/s^2

			float maxSlopeDeg = 45.0f; ///< walkable slope
			float maxStep = 0.3f;      ///< m

			float jumpSpeed = 9.0f;       ///< m/s, impulse applied when jumping
			float airControlMultiplier = 0.5f; ///< 50% of ground control
			float coyoteTimeSec = 0.1f;   ///< allow jump shortly after leaving ground
			float jumpBufferSec = 0.1f;   ///< allow jump shortly before landing

			// Swimming parameters.
			float waterGravityMultiplier = 0.3f; ///< reduced gravity factor while submerged
			float waterHorizontalDamping = 6.0f; ///< horizontal velocity damping (1/s)
			float waterVerticalControlAccel = 20.0f; ///< m/s^2 toward vertical intent
			float waterSurfaceBreachingAccel = 18.0f; ///< upward force near surface
			float waterSurfaceBreachingDepth = 0.25f; ///< meters depth considered "near surface"

			// Flying parameters (optional).
			bool enableFlying = true;
			float flyVerticalControlAccel = 25.0f; ///< m/s^2 toward vertical intent while flying
			float flyMaxStamina = 1.0f; ///< seconds of flight stamina (simplified)
			float flyStaminaDrainPerSec = 1.0f; ///< stamina drain while flying

			IWorldCollider::Capsule capsule{};
		};

		CharacterController();
		explicit CharacterController(const Config& cfg);
		~CharacterController();

		bool Init(const engine::math::Vec3& startCenter);

		/// Updates position and velocity using sweeps (capsule vs world).
		/// \return true if update succeeded; false on invalid inputs.
		bool Update(float dt, const MoveInput& input, const IWorldCollider& world);

		bool IsGrounded() const { return m_isGrounded; }
		engine::math::Vec3 GetPosition() const { return m_positionCenter; }
		engine::math::Vec3 GetVelocity() const { return m_velocity; }
		IWorldCollider::Capsule GetCapsule() const { return m_capsule; }

	private:
		static float DotXZ(const engine::math::Vec3& a, const engine::math::Vec3& b)
		{
			return a.x * b.x + a.z * b.z;
		}

		static engine::math::Vec3 ProjectOnPlane(const engine::math::Vec3& v, const engine::math::Vec3& n)
		{
			const float d = v.x * n.x + v.y * n.y + v.z * n.z;
			return engine::math::Vec3(v.x - d * n.x, v.y - d * n.y, v.z - d * n.z);
		}

		static bool IsWalkable(const engine::math::Vec3& normal, float maxSlopeCos)
		{
			return normal.y >= maxSlopeCos;
		}

		bool TryStepUp(const MoveInput& input, float dt, float timeRemaining,
			const IWorldCollider& world,
			const engine::math::Vec3& startPos,
			const engine::math::Vec3& horizontalDisp,
			const engine::math::Vec3& currentVel,
			engine::math::Vec3& outPos,
			engine::math::Vec3& outVel,
			bool& outGrounded);

	private:
		Config m_cfg{};
		IWorldCollider::Capsule m_capsule{};
		enum class MovementMode
		{
			Ground,
			Air,
			Water,
			Fly
		};
		MovementMode m_mode = MovementMode::Air;
		float m_staminaSec = 0.0f;
		engine::math::Vec3 m_positionCenter{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 m_velocity{ 0.0f, 0.0f, 0.0f };
		bool m_isGrounded = false;
		float m_timeSinceLeftGroundSec = 0.0f;
		float m_timeSinceJumpPressedSec = 999.0f; // invalid until jumpPressed occurs
	};
}

