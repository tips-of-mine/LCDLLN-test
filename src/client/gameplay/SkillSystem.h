#pragma once

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"
#include "engine/math/Math.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::gameplay
{
	/// Data-driven MMO skill executor (definitions + server-authoritative cooldowns).
	///
	/// This system is intentionally self-contained:
	/// - JSON parsing/loading happens at `Init()` (boot-time).
	/// - Cooldown tracking is per-player and keyed by `skillId`.
	/// - Resource costs are consumed from a per-player local pool.
	class SkillSystem final
	{
	public:
		using EntityId = uint64_t;

		/// AoE targeting shapes supported by the skill system.
		enum class AoEShapeType : uint8_t
		{
			None = 0,
			Circle = 1,
			Cone = 2,
			Line = 3,
			Ring = 4
		};

		/// One cast progress state used by the casting UI and state machine.
		enum class CastState : uint8_t
		{
			Idle = 0,
			Casting = 1,
			Channeling = 2,
			Cooldown = 3
		};

		/// Resource types supported by the ticket.
		enum class ResourceType : uint8_t
		{
			Health = 0,
			Mana = 1,
			Energy = 2,
			Rage = 3,
			ComboPoints = 4
		};

		/// Cost bundle applied when starting a cast.
		struct ResourceCost
		{
			uint32_t health = 0;
			uint32_t mana = 0;
			uint32_t energy = 0;
			uint32_t rage = 0;
			uint32_t comboPoints = 0;
		};

		/// One effect entry declared by the skill json.
		struct SkillEffect
		{
			std::string type;
			uint32_t amount = 0;
			std::string damageType;
			/// When > 0: builder effect that generates combo points (on hit only).
			uint32_t generateCombo = 0;
			/// When true: spender effect that consumes all combo points (on hit only).
			bool consumeCombo = false;
		};

		/// AoE targeting definition loaded from the skill json.
		struct AoEDefinition
		{
			AoEShapeType shape = AoEShapeType::None;

			// Circle
			float radiusMeters = 0.0f;

			// Cone
			float angleDeg = 0.0f;      // full angle (degrees)
			float rangeMeters = 0.0f;  // max distance in meters

			// Line
			float widthMeters = 0.0f;  // total width
			float lengthMeters = 0.0f; // total length

			// Ring (outer + thickness)
			float outerRadiusMeters = 0.0f;
			float innerRadiusMeters = 0.0f; // when provided directly; otherwise computed from thickness
			float ringThicknessMeters = 0.0f;
		};

		/// Optional parsing flags used by casting interruption and channeling.
		struct SkillRuntimeFlags
		{
			/// If true, movement interruption is ignored while casting/channeling.
			bool frozenMovement = false;
			/// Channel tick interval in seconds. 0 disables channel ticking.
			float channelTickIntervalSeconds = 0.0f;
			/// If false, channel cannot be canceled early (still interrupts via other conditions).
			bool channelCancelable = true;
			/// Per-skill damage threshold in [0..1] for interrupting casting/channeling.
			/// When current cast receives damageFractionOfMaxHealth > threshold -> interrupt.
			float interruptDamageFractionThreshold = 0.0f;
		};

		/// Parsed skill definition loaded from `game/data/skills/*.json`.
		struct SkillDefinition
		{
			std::string id;
			std::string name;
			float cooldownSeconds = 0.0f;
			float castTimeSeconds = 0.0f;
			ResourceCost cost{};
			float rangeMeters = 0.0f;
			std::vector<SkillEffect> effects;
			AoEDefinition aoe{};
			SkillRuntimeFlags runtime{};
		};

		/// One player's resource pool (used to validate and apply resource costs).
		struct PlayerResources
		{
			uint32_t health = 0;
			uint32_t mana = 0;
			uint32_t energy = 0;
			uint32_t rage = 0;
			uint32_t comboPoints = 0;
			uint32_t maxComboPoints = 5;
		};

		/// Construct a skill system using the engine config to resolve `paths.content`.
		explicit SkillSystem(const engine::core::Config& config);
		~SkillSystem();

		/// Parse and load skills at boot.
		bool Init();

		/// Release runtime state and emit shutdown logs.
		void Shutdown();

		/// Advance casting/channeling state machines for all active players.
		/// This method should be called periodically by the server/client tick loop.
		void Tick();

		/// Query the current cast state machine status for UI/progression.
		/// \param outProgress01 when not null: 0..1 progress for casting/channeling.
		CastState GetCastState(uint32_t playerId, float* outProgress01 = nullptr) const;

		/// Provide player world position used for range validation.
		void SetPlayerPosition(uint32_t playerId, const engine::math::Vec3& worldPosMeters);
		/// Provide target world position used for range validation.
		void SetTargetPosition(EntityId targetId, const engine::math::Vec3& worldPosMeters);

		/// Provide explicit LOS decision used by validation.
		void SetLineOfSight(uint32_t playerId, EntityId targetId, bool hasLineOfSight);

		/// Notify the system that one target died (optional combo decay reset).
		void NotifyTargetDeath(uint32_t casterPlayerId, EntityId targetId);

		/// Notify cast interruption caused by damage taken during the current cast.
		/// \param damageFractionOfMaxHealth damage as a fraction of target max health in [0..1].
		void NotifyDamageTaken(uint32_t playerId, float damageFractionOfMaxHealth);
		/// Notify cast interruption caused by stun.
		void NotifyStun(uint32_t playerId);
		/// Notify cast interruption caused by knockback.
		void NotifyKnockback(uint32_t playerId);
		/// Notify cast interruption caused by movement (only effective when !frozenMovement).
		void NotifyMovement(uint32_t playerId);
		/// Notify cast interruption caused by death.
		void NotifyDeath(uint32_t playerId);
		/// Early-cancel channeling (refund 50% of mana cost like other interrupts).
		void CancelChannel(uint32_t playerId);

		/// Attempt to cast one skill.
		/// \return true when cooldown/resources/range/LOS validate and cast is triggered.
		bool UseSkill(uint32_t playerId, std::string_view skillId, EntityId targetId);

		/// Attempt to cast one AoE skill.
		/// targetDir uses XZ plane convention (Y ignored).
		bool UseAoESkill(uint32_t playerId, std::string_view skillId, const engine::math::Vec3& targetPosMeters, const engine::math::Vec3& targetDirXZ);

		/// Query one player's current combo state for UI (pips/orbs).
		bool GetComboState(uint32_t playerId, uint32_t& outComboPoints, uint32_t& outMaxComboPoints) const;

		/// Find one loaded definition by id.
		const SkillDefinition* FindSkill(std::string_view skillId) const;

	private:
		bool EnsurePlayerInitialized(uint32_t playerId);

		bool ValidateCooldown(uint32_t playerId, const SkillDefinition& def, uint64_t nowTicks) const;
		bool ValidateResources(uint32_t playerId, const ResourceCost& cost) const;
		bool ConsumeResources(uint32_t playerId, const ResourceCost& cost);
		bool ValidateRangeAndLos(uint32_t playerId, EntityId targetId, const SkillDefinition& def) const;

		static uint64_t SecondsToTicks(float seconds);
		uint64_t ResolveGcdTicks() const;

		void StartCast(uint32_t playerId, const SkillDefinition& def, EntityId targetId, uint64_t nowTicks);
		void StartAoECast(
			uint32_t playerId,
			const SkillDefinition& def,
			const engine::math::Vec3& aoeTargetPosMeters,
			const engine::math::Vec3& aoeTargetDirXZ,
			uint64_t nowTicks);
		void CompleteCast(uint32_t playerId, const SkillDefinition& def, EntityId targetId, uint64_t nowTicks, bool interrupted);
		void ApplySkillEffectsOnPlayer(uint32_t playerId, const SkillDefinition& def, bool tick);
		struct ActiveCast;
		void ApplySkillEffectsOnAoETargets(uint32_t casterPlayerId, const SkillDefinition& def, const ActiveCast& active, bool tick);

		struct InterruptInputs
		{
			bool hasDamage = false;
			float damageFractionOfMaxHealth = 0.0f;
			bool hasStun = false;
			bool hasKnockback = false;
			bool hasMovement = false;
			bool hasDeath = false;
			bool hasEarlyCancel = false;
		};

		struct ActiveCast
		{
			CastState state = CastState::Idle;
			std::string skillId;
			EntityId targetId = 0;
			uint64_t startTicks = 0;
			uint64_t endTicks = 0;

			// Channel ticks.
			uint64_t nextChannelTickTicks = 0;
			uint64_t channelTickIntervalTicks = 0;

			// Cached runtime flags for quick interruption checks.
			bool frozenMovement = false;
			bool channelCancelable = true;
			float interruptDamageFractionThreshold = 0.0f;

			// Cached cost for mana refund on interrupt.
			ResourceCost cost{};

			// AoE placement (only valid when isAoE=true).
			bool isAoE = false;
			engine::math::Vec3 aoeTargetPosMeters{};
			engine::math::Vec3 aoeTargetDirXZ{};
		};

		engine::core::Config m_config;
		std::string m_skillsRelativePath = "skills"; // resolved under paths.content
		bool m_initialized = false;

		// Loaded definitions (by id).
		std::unordered_map<std::string, SkillDefinition> m_definitionsById;

		// Skill cooldown tracking (timestamp last use in ticks).
		std::unordered_map<uint32_t, std::unordered_map<std::string, uint64_t>> m_lastUseTicks;

		// Active cast state machine (one cast at a time per player).
		std::unordered_map<uint32_t, ActiveCast> m_activeCastByPlayer;

		// Cooldown state for UI/progression: ends when either gcd or skill cooldown ends.
		std::unordered_map<uint32_t, uint64_t> m_castCooldownEndTicks;

		// Global cooldown (GCD) lock (block all skills).
		std::unordered_map<uint32_t, uint64_t> m_gcdEndTicks;

		// Pending interrupt inputs accumulated since last Tick().
		std::unordered_map<uint32_t, InterruptInputs> m_pendingInterruptInputs;

		// Track when one builder generated combo points (for decay).
		std::unordered_map<uint32_t, uint64_t> m_lastComboGainTicks;

		// Optional: track the last target that received a builder hit.
		std::unordered_map<uint32_t, EntityId> m_lastComboTargetByCaster;

		// Local validation pools.
		std::unordered_map<uint32_t, PlayerResources> m_playerResources;
		PlayerResources m_defaultPlayerResources{};

		// Position/LOS used for validation.
		std::unordered_map<uint32_t, engine::math::Vec3> m_playerPositions;
		std::unordered_map<EntityId, engine::math::Vec3> m_targetPositions;
		std::unordered_map<uint64_t, bool> m_losTable; // key = playerId<<32 | (targetId&0xFFFFFFFF)
	};
}

