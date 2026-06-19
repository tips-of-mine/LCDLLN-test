#include "src/client/gameplay/CharacterController.h"

#include <cmath>
#include <algorithm>

namespace engine::gameplay
{
	namespace
	{
		engine::math::Vec3 Vec3XZ(const engine::math::Vec3& v)
		{
			return engine::math::Vec3(v.x, 0.0f, v.z);
		}

		float LengthXZ(const engine::math::Vec3& v)
		{
			return std::sqrt(v.x * v.x + v.z * v.z);
		}
	}

	CharacterController::CharacterController()
	{
		LOG_INFO(Core, "[CharacterController] Default ctor");
		m_cfg = Config{};
		m_capsule = m_cfg.capsule;
		m_timeSinceLeftGroundSec = 0.0f;
		m_timeSinceJumpPressedSec = 999.0f;
		m_mode = MovementMode::Air;
		m_staminaSec = m_cfg.flyMaxStamina;
	}

	CharacterController::CharacterController(const Config& cfg)
		: m_cfg(cfg)
	{
		m_capsule = m_cfg.capsule;
		LOG_INFO(Core, "[CharacterController] Ctor OK (capsule r={} h={})",
			m_capsule.radius, m_capsule.height);
		m_timeSinceLeftGroundSec = 0.0f;
		m_timeSinceJumpPressedSec = 999.0f;
		m_mode = MovementMode::Air;
		m_staminaSec = m_cfg.flyMaxStamina;
	}

	CharacterController::~CharacterController()
	{
		LOG_INFO(Core, "[CharacterController] Destroyed");
	}

	bool CharacterController::Init(const engine::math::Vec3& startCenter)
	{
		// Basic validation of capsule geometry.
		if (m_capsule.radius <= 0.0f || m_capsule.height <= 2.0f * m_capsule.radius)
		{
			LOG_ERROR(Core, "[CharacterController] Init FAILED: invalid capsule (r={} h={})",
				m_capsule.radius, m_capsule.height);
			return false;
		}

		m_positionCenter = startCenter;
		m_velocity = engine::math::Vec3(0.0f, 0.0f, 0.0f);
		m_isGrounded = false;
		m_mode = MovementMode::Air;
		m_staminaSec = m_cfg.flyMaxStamina;
		LOG_INFO(Core, "[CharacterController] Init OK (pos={}, {}, {})",
			m_positionCenter.x, m_positionCenter.y, m_positionCenter.z);
		return true;
	}

	void CharacterController::ApplyDodgeImpulse(const engine::math::Vec3& dirXZ)
	{
		const float len = std::sqrt(dirXZ.x * dirXZ.x + dirXZ.z * dirXZ.z);
		if (len < 1e-4f)
			return;
		m_dodgeDirXZ = engine::math::Vec3(dirXZ.x / len, 0.0f, dirXZ.z / len);
		m_dodgeTimeRemaining = m_cfg.dodgeDurationSec;
		m_velocity.x = m_dodgeDirXZ.x * m_cfg.dodgeSpeed;
		m_velocity.z = m_dodgeDirXZ.z * m_cfg.dodgeSpeed;
	}

	bool CharacterController::Update(float dt, const MoveInput& input, const IWorldCollider& world)
	{
		if (dt <= 0.0f)
		{
			LOG_WARN(Core, "[CharacterController] Update ignored: dt<=0 (dt={})", dt);
			return false;
		}

		// Detect water/flying mode (using optional water query + input flags).
		IWorldCollider::WaterQuery wq{};
		const bool inWater = world.QueryWater(m_positionCenter, wq) && wq.inWater;
		const bool wantFly = m_cfg.enableFlying && input.flyPressed && (!inWater) && (m_staminaSec > 0.0f);
		const bool isFlying = wantFly;

		const MovementMode desiredMode =
			isFlying ? MovementMode::Fly : (inWater ? MovementMode::Water : (m_isGrounded ? MovementMode::Ground : MovementMode::Air));

		if (desiredMode != m_mode)
		{
			m_mode = desiredMode;
			if (m_mode == MovementMode::Water)
				LOG_INFO(Core, "[CharacterController] Mode -> Water (depth={})", wq.depth);
			else if (m_mode == MovementMode::Fly)
				LOG_INFO(Core, "[CharacterController] Mode -> Fly (staminaSec={})", m_staminaSec);
			else if (m_mode == MovementMode::Ground)
				LOG_INFO(Core, "[CharacterController] Mode -> Ground");
			else
				LOG_INFO(Core, "[CharacterController] Mode -> Air");
		}

		// Timers for coyote time + jump buffer (disabled while swimming/flying).
		const bool useGroundJumpLogic = !inWater && !isFlying;
		if (useGroundJumpLogic)
		{
			if (m_isGrounded)
				m_timeSinceLeftGroundSec = 0.0f;
			else
				m_timeSinceLeftGroundSec += dt;

			if (input.jumpPressed)
				m_timeSinceJumpPressedSec = 0.0f;
			else
				m_timeSinceJumpPressedSec += dt;
		}
		else
		{
			// Prevent jumping directly after swimming/flying transitions.
			m_timeSinceLeftGroundSec = 999.0f;
			m_timeSinceJumpPressedSec = 999.0f;
		}

		const bool coyoteValid = m_timeSinceLeftGroundSec <= m_cfg.coyoteTimeSec;
		const bool jumpBufferValid = m_timeSinceJumpPressedSec <= m_cfg.jumpBufferSec;

		const engine::math::Vec3 desiredMoveXZ = Vec3XZ(input.moveDirXZ);
		const float moveLen = LengthXZ(desiredMoveXZ);
		const bool hasMove = moveLen > 1e-5f;

		const float targetSpeed = input.crouch ? m_cfg.crouchSpeed
		                        : (input.sprint ? m_cfg.sprintSpeed
		                        : (input.run    ? m_cfg.runSpeed : m_cfg.walkSpeed));
		engine::math::Vec3 desiredVelXZ(0.0f, 0.0f, 0.0f);
		if (hasMove)
			desiredVelXZ = desiredMoveXZ * (targetSpeed / moveLen);

		// Horizontal acceleration or friction. Exception : pendant une esquive
		// (m_dodgeTimeRemaining > 0), la vitesse horizontale est forcee a
		// l'impulsion d'esquive ; collision (sweep) et gravite restent appliquees.
		engine::math::Vec3 velXZ = Vec3XZ(m_velocity);
		if (m_dodgeTimeRemaining > 0.0f && !inWater && !isFlying)
		{
			m_dodgeTimeRemaining -= dt;
			velXZ = m_dodgeDirXZ * m_cfg.dodgeSpeed;
		}
		else if (hasMove)
		{
			const engine::math::Vec3 delta = desiredVelXZ - velXZ;
			const float deltaLen = delta.Length();
			const float airMul =
				(inWater || isFlying) ? 1.0f : (m_isGrounded ? 1.0f : m_cfg.airControlMultiplier);
			const float maxChange = (m_cfg.acceleration * airMul) * dt;
			if (deltaLen > maxChange && deltaLen > 0.0f)
			{
				const engine::math::Vec3 clamped = delta * (maxChange / deltaLen);
				velXZ += clamped;
			}
			else
			{
				velXZ = desiredVelXZ;
			}
		}
		else
		{
			const float speed = velXZ.Length();
			if (speed > 0.0f)
			{
				const float drop = m_cfg.friction * dt;
				const float newSpeed = (speed > drop) ? (speed - drop) : 0.0f;
				if (newSpeed <= 0.0f)
					velXZ = engine::math::Vec3(0.0f, 0.0f, 0.0f);
				else
					velXZ = velXZ * (newSpeed / speed);
			}
		}

		m_velocity.x = velXZ.x;
		m_velocity.z = velXZ.z;

		// Gravity.
		if (isFlying)
		{
			// No gravity while flying.
		}
		else if (inWater)
		{
			// Reduced gravity in water.
			m_velocity.y += (m_cfg.gravity * m_cfg.waterGravityMultiplier) * dt;
			// Damping for swimming horizontal movement (velocity damping, 3D movement).
			const float damp = std::max(0.0f, 1.0f - (m_cfg.waterHorizontalDamping * dt));
			velXZ = velXZ * damp;
			m_velocity.x = velXZ.x;
			m_velocity.z = velXZ.z;
		}
		else
		{
			if (!m_isGrounded)
				m_velocity.y += m_cfg.gravity * dt;
			else if (m_velocity.y < 0.0f)
				m_velocity.y = 0.0f;
		}

		const float maxSlopeCos = std::cos(m_cfg.maxSlopeDeg * 3.1415926535f / 180.0f);

		// Capsule sweep + collision resolution loop.
		engine::math::Vec3 pos = m_positionCenter;
		engine::math::Vec3 vel = m_velocity;
		float timeRemaining = 1.0f;
		bool grounded = false;
		bool didJumpThisFrame = false;

		// Sticky ground probe : sans ca, quand le perso est immobile
		// (vel ~ 0 -> disp ~ 0), la boucle de sweep ne touche rien et
		// laisse `grounded` a false. Frame suivante, m_isGrounded=false
		// declenche la gravite, le sweep snape au sol, m_isGrounded=true.
		// Cycle infini a 60 Hz qui fait flipper la state machine d'anim
		// entre Land et Fall (visible dans le log d'exec). On probe donc
		// d'abord un sweep court vers le bas pour detecter si le perso
		// est encore pose sur un sol walkable, et on conserve grounded.
		if (m_isGrounded && !isFlying && !inWater)
		{
			constexpr float kGroundProbeDist = 0.05f;  // 5 cm
			const engine::math::Vec3 probeEnd =
				pos - engine::math::Vec3(0.0f, kGroundProbeDist, 0.0f);
			IWorldCollider::SweepHit probeHit{};
			probeHit.hit = false;
			probeHit.fraction = 1.0f;
			probeHit.normal = engine::math::Vec3(0.0f, 1.0f, 0.0f);
			if (world.SweepCapsule(m_capsule, pos, probeEnd, probeHit) &&
				probeHit.hit && probeHit.fraction < 1.0f &&
				IsWalkable(probeHit.normal, maxSlopeCos))
			{
				grounded = true;
			}
		}

		// Water vertical control and surface breaching.
		if (inWater)
		{
			const float verticalIntent = (input.swimUpPressed ? 1.0f : 0.0f) - (input.swimDownPressed ? 1.0f : 0.0f);
			vel.y += (verticalIntent * m_cfg.waterVerticalControlAccel) * dt;

			if (wq.depth > 0.0f && wq.depth < m_cfg.waterSurfaceBreachingDepth)
			{
				const float t = 1.0f - (wq.depth / m_cfg.waterSurfaceBreachingDepth);
				vel.y += m_cfg.waterSurfaceBreachingAccel * t * dt;
			}
		}

		// Flying vertical control and stamina drain.
		if (isFlying)
		{
			const float verticalIntent = (input.swimUpPressed ? 1.0f : 0.0f) - (input.swimDownPressed ? 1.0f : 0.0f);
			vel.y += (verticalIntent * m_cfg.flyVerticalControlAccel) * dt;
			m_staminaSec = std::max(0.0f, m_staminaSec - (m_cfg.flyStaminaDrainPerSec * dt));
		}

		// If we can jump now (coyote or buffer), apply jump impulse before collision sweeps.
		if (!inWater && !isFlying && jumpBufferValid && (m_isGrounded || coyoteValid))
		{
			vel.y = m_cfg.jumpSpeed;
			// Reset timers to prevent double jump within same grounded window.
			m_timeSinceJumpPressedSec = m_cfg.jumpBufferSec + 1.0f;
			m_timeSinceLeftGroundSec = m_cfg.coyoteTimeSec + 1.0f;
			m_isGrounded = false;
			// Cancel the sticky-ground hit we may have detected just above :
			// si on saute, on quitte explicitement le sol et on ne veut
			// PAS que m_isGrounded reste true (sinon re-jump immediate).
			grounded = false;
			didJumpThisFrame = true;
		}

		// Clamp vertical velocity to avoid accumulation.
		{
			const float maxVy = m_cfg.jumpSpeed * 2.0f;
			if (vel.y > maxVy)
				vel.y = maxVy;
		}

		// Up to a few collision iterations (typical character controller behavior).
		const int maxIter = 4;
		for (int iter = 0; iter < maxIter && timeRemaining > 0.0f; ++iter)
		{
			const engine::math::Vec3 disp = vel * (dt * timeRemaining);
			const engine::math::Vec3 end = pos + disp;

			IWorldCollider::SweepHit hit{};
			hit.hit = false;
			hit.fraction = 1.0f;
			hit.normal = engine::math::Vec3(0.0f, 1.0f, 0.0f);

			if (!world.SweepCapsule(m_capsule, pos, end, hit) || !hit.hit || hit.fraction >= 1.0f)
			{
				pos = end;
				break;
			}

			// Move to impact point.
			const engine::math::Vec3 impact = pos + disp * hit.fraction;
			pos = impact;

			timeRemaining *= (1.0f - hit.fraction);

			const bool walkable = IsWalkable(hit.normal, maxSlopeCos);
			const bool movingDown = vel.y < 0.0f;

			const bool canGround = walkable && movingDown && (m_mode != MovementMode::Water) && (m_mode != MovementMode::Fly);
			if (canGround)
			{
				grounded = true;
				vel.y = 0.0f;
				// Slide on floor: remove normal component from velocity.
				vel = ProjectOnPlane(vel, hit.normal);
				// Re-force gravity axis off when grounded.
				vel.y = 0.0f;
			}
			else
			{
				grounded = false;
				// Step up detection: try to climb small obstacles.
				if (m_cfg.maxStep > 0.0f && hasMove && vel.y <= 0.01f && (m_mode != MovementMode::Water) && (m_mode != MovementMode::Fly))
				{
					engine::math::Vec3 steppedPos;
					engine::math::Vec3 steppedVel;
					bool steppedGrounded = false;
					if (TryStepUp(input, dt, timeRemaining, world, pos, Vec3XZ(vel) * (dt * timeRemaining), vel,
						hit.stair, steppedPos, steppedVel, steppedGrounded))
					{
						pos = steppedPos;
						vel = steppedVel;
						grounded = steppedGrounded;
						timeRemaining = 0.0f;
						break;
					}
				}

				// Slide along obstacle.
				vel = ProjectOnPlane(vel, hit.normal);
			}
		}

		// If we landed during the collision resolution and we had a buffered jump pending,
		// trigger it right away.
		if (!inWater && !isFlying && !didJumpThisFrame && grounded && (m_timeSinceJumpPressedSec <= m_cfg.jumpBufferSec))
		{
			vel.y = m_cfg.jumpSpeed;
			m_timeSinceJumpPressedSec = m_cfg.jumpBufferSec + 1.0f;
			m_timeSinceLeftGroundSec = m_cfg.coyoteTimeSec + 1.0f;
			grounded = false;
		}

		// ── Recuperation anti-encastrement (penetration recovery) ────────────────
		// SweepCapsule ne rattrape le sol QUE si le centre le traverse de haut en
		// bas pendant la frame (cf. TerrainCollider::SweepCapsule). Si le perso
		// finit la frame DEJA sous le sol — cas typique : il nage en surface puis
		// ressort la ou le terrain remonte (bord de bassin), ou une pente raide a
		// laisse le sweep descendant "tunneler" — plus aucune frame ne le detecte
		// et il tombe a l'infini (bug "eau sans fond", chute jusqu'a y<<0).
		//
		// Parade : on sonde depuis un point HAUT au-dessus du perso vers sa
		// position. Ce sweep traverse forcement le sol par le haut, donc
		// SweepCapsule renvoie la hauteur de repos (sol + halfHeight). Si le centre
		// est sous ce niveau, on l'y remonte et on annule la vitesse descendante.
		// Ne se declenche jamais en jeu normal (le perso est au niveau ou au-dessus
		// du sol -> pas de hit, ou restY ~= pos.y) : impact nul hors encastrement.
		if (!isFlying)
		{
			constexpr float kRecoverProbeUp = 50.0f;  // > toute penetration par frame
			const engine::math::Vec3 probeFrom =
				pos + engine::math::Vec3(0.0f, kRecoverProbeUp, 0.0f);
			IWorldCollider::SweepHit recHit{};
			recHit.hit = false;
			recHit.fraction = 1.0f;
			recHit.normal = engine::math::Vec3(0.0f, 1.0f, 0.0f);
			if (world.SweepCapsule(m_capsule, probeFrom, pos, recHit) && recHit.hit)
			{
				// restY = sol + halfHeight (hauteur de repos du centre capsule).
				const float restY = probeFrom.y + (pos.y - probeFrom.y) * recHit.fraction;
				if (pos.y < restY - 1e-3f)
				{
					pos.y = restY;
					if (vel.y < 0.0f)
						vel.y = 0.0f;
					// En nage/vol on remonte hors du terrain mais on ne force pas
					// l'etat "grounded" (gere par le mode courant).
					if (m_mode != MovementMode::Water && m_mode != MovementMode::Fly)
						grounded = true;
				}
			}
		}

		m_positionCenter = pos;
		m_velocity = vel;
		m_isGrounded = grounded;
		if (!inWater && !isFlying && m_isGrounded)
			m_timeSinceLeftGroundSec = 0.0f;
		return true;
	}

	bool CharacterController::TryStepUp(const MoveInput& input, float dt, float timeRemaining,
		const IWorldCollider& world,
		const engine::math::Vec3& startPos,
		const engine::math::Vec3& horizontalDisp,
		const engine::math::Vec3& currentVel,
		bool obstacleIsStair,
		engine::math::Vec3& outPos,
		engine::math::Vec3& outVel,
		bool& outGrounded)
	{
		(void)input;
		(void)dt;

		// Hauteur de step-up autorisée :
		// - ESCALIER (mesh « escalier », obstacleIsStair) : on monte jusqu'à `maxClimb`
		//   (≫ maxStep) pour se poser sur le dessus de la marche/volée.
		// - SINON (mur, bâtiment, prop normal, micro-marche de terrain) : plafonné à
		//   `maxStep` (~0,3 m) = simple lissage. On NE grimpe PAS les parois verticales
		//   (sinon le perso « vole » en se collant à un mur — bug observé sur l'auberge).
		// Auparavant `maxClimb` s'appliquait à TOUT obstacle (rendait tout mesh
		// escaladable quelle que soit sa hauteur) → c'était la cause du vol le long
		// des murs. On réserve désormais la grande montée aux escaliers taggés.
		const float climb = obstacleIsStair
			? ((m_cfg.maxClimb > m_cfg.maxStep) ? m_cfg.maxClimb : m_cfg.maxStep)
			: m_cfg.maxStep;
		const engine::math::Vec3 up(0.0f, 1.0f, 0.0f);
		const engine::math::Vec3 upStart = startPos + up * climb;
		const engine::math::Vec3 upEnd = upStart + horizontalDisp;

		IWorldCollider::SweepHit hit{};
		if (world.SweepCapsule(m_capsule, upStart, upEnd, hit) && hit.hit && hit.fraction < 1.0f)
			return false;

		// Now sweep down to land (jusqu'à `climb` plus bas).
		const engine::math::Vec3 downStart = upEnd;
		const engine::math::Vec3 downEnd = downStart + up * (-climb);
		IWorldCollider::SweepHit downHit{};
		if (!world.SweepCapsule(m_capsule, downStart, downEnd, downHit) || !downHit.hit)
		{
			// No landing found; still allow moving on the obstacle top but mark not grounded.
			outPos = downEnd;
		outVel = currentVel;
			outVel.y = 0.0f;
			outGrounded = false;
			return true;
		}

		const float maxSlopeCos = std::cos(m_cfg.maxSlopeDeg * 3.1415926535f / 180.0f);
		if (!IsWalkable(downHit.normal, maxSlopeCos))
			return false;

		outPos = downStart + (downEnd - downStart) * downHit.fraction;
		outGrounded = true;

		// Preserve horizontal velocity, cancel vertical.
		outVel = currentVel;
		outVel.y = 0.0f;
		outVel = ProjectOnPlane(outVel, downHit.normal);
		outVel.y = 0.0f;
		return true;
	}
}

