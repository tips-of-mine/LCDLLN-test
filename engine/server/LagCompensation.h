#pragma once

#include "engine/core/Log.h"
#include "engine/server/ReplicationTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server
{
	/// One time-stamped XZ position sample (M30.3).
	struct EntityPositionSample
	{
		/// Wall-clock server time in milliseconds when this sample was captured.
		uint64_t timestampMs = 0;

		/// World-space X coordinate in metres.
		float positionX = 0.0f;

		/// World-space Z coordinate in metres (Y / vertical is not lag-compensated).
		float positionZ = 0.0f;
	};

	/// Fixed-capacity ring buffer of historical XZ positions for one entity (M30.3).
	///
	/// Stores up to kMaxSamples entries at 60 Hz, covering 1 second of history.
	/// When full, the oldest sample is overwritten first (FIFO eviction).
	class EntityPositionHistory final
	{
	public:
		/// Maximum number of samples: 60 Hz × 1 second.
		static constexpr size_t kMaxSamples = 60;

		EntityPositionHistory() = default;

		/// Record a new position sample.
		/// \param timestampMs  Wall-clock server time in milliseconds.
		/// \param posX         World-space X in metres.
		/// \param posZ         World-space Z in metres.
		void Record(uint64_t timestampMs, float posX, float posZ);

		/// Query the linearly interpolated XZ position at a given timestamp.
		/// Returns false when \p timestampMs is outside the stored window,
		/// or when fewer than two samples are available for interpolation.
		///
		/// \param timestampMs  Target time in milliseconds.
		/// \param outPosX      Interpolated X on success.
		/// \param outPosZ      Interpolated Z on success.
		bool QueryAtTime(uint64_t timestampMs, float& outPosX, float& outPosZ) const;

		/// Number of valid samples currently in the buffer.
		size_t Count() const { return m_count; }

	private:
		/// Return the logical index of the i-th oldest sample (i=0 is oldest).
		size_t OldestIndex(size_t i) const;

		std::array<EntityPositionSample, kMaxSamples> m_samples{};
		size_t m_head  = 0; ///< Index of the next write slot.
		size_t m_count = 0; ///< Number of valid entries (≤ kMaxSamples).
	};

	// -----------------------------------------------------------------------

	/// Client request to use a skill with a timestamp for lag compensation (M30.3).
	/// Populated by the server after parsing the incoming network message.
	struct LagCompSkillRequest
	{
		/// Stable server-side identifier of the casting entity.
		EntityId casterEntityId = 0;

		/// Stable server-side identifier of the target entity.
		EntityId targetEntityId = 0;

		/// Skill identifier (matches SkillDefinition::id from M28.1).
		std::string skillId;

		/// Wall-clock time in milliseconds at the client when the skill was activated.
		/// Used to compute the rewind time: rewindTime = clientTimestampMs - rttMs/2.
		uint64_t clientTimestampMs = 0;

		/// Maximum effective range of the skill in metres (from skill definition).
		float skillRangeMeters = 0.0f;

		/// Line-of-sight decision provided by the caller for the current server state.
		/// Full rewound-geometry LOS is beyond the scope of M30.3; the caller supplies
		/// this value based on the best available approximation.
		bool hasLineOfSight = true;
	};

	/// Result produced by LagCompensationManager::ValidateHit (M30.3).
	struct HitValidationResult
	{
		/// True when range and LOS checks passed at the rewound positions.
		bool valid = false;

		/// Target entity identifier (mirrors LagCompSkillRequest::targetEntityId).
		EntityId targetEntityId = 0;

		/// XZ distance between caster and target at the rewound timestamp (metres).
		float rewindedDistanceMeters = 0.0f;

		/// Actual server timestamp used for the rewind query (milliseconds).
		uint64_t rewindTimestampMs = 0;

		/// Reason string when valid=false (diagnostic only).
		std::string rejectReason;
	};

	// -----------------------------------------------------------------------

	/// Server-side lag compensation manager (M30.3).
	///
	/// Responsibilities:
	///  - Maintain a ring-buffer position history for every entity at 60 Hz.
	///  - On ValidateHit(): compute the rewind timestamp (clientTs - rtt/2),
	///    clamp to maxRewindMs (anti-abuse), fetch interpolated XZ positions,
	///    validate range and LOS, return the authoritative result.
	///  - Effects are applied to the current (non-rewound) state by the caller.
	///
	/// Thread-safety: NOT thread-safe. Must be used on the server tick thread.
	class LagCompensationManager final
	{
	public:
		/// Configuration for the lag compensation manager.
		struct Config
		{
			/// Maximum rewind window in milliseconds (anti-abuse cap).
			/// Requests asking for a rewind older than this are clamped.
			float maxRewindMs = 500.0f;
		};

		LagCompensationManager();
		explicit LagCompensationManager(const Config& cfg);
		~LagCompensationManager();

		/// Initialise the manager. Must be called before any other method.
		/// \return true on success; false if already initialised or config invalid.
		bool Init();

		/// Release all entity histories and reset state.
		void Shutdown();

		/// Record the current XZ position of an entity for this server tick.
		/// Call once per entity per physics tick (at ~60 Hz).
		///
		/// \param entityId    Stable entity identifier.
		/// \param timestampMs Wall-clock server time in milliseconds.
		/// \param posX        World-space X in metres.
		/// \param posZ        World-space Z in metres.
		void RecordEntityPosition(EntityId entityId,
		                          uint64_t timestampMs,
		                          float posX,
		                          float posZ);

		/// Remove the position history for a despawned entity.
		void RemoveEntity(EntityId entityId);

		/// Validate a skill-use hit request using rewound entity positions.
		///
		/// Algorithm:
		///  1. rewindDelta  = serverNowMs - (clientTimestampMs - estimatedRttMs/2)
		///  2. clamp rewindDelta to [0, maxRewindMs]
		///  3. rewindTime   = serverNowMs - rewindDelta
		///  4. Fetch caster XZ at rewindTime from history.
		///  5. Fetch target XZ at rewindTime from history.
		///  6. Compute XZ distance; validate <= request.skillRangeMeters.
		///  7. Validate request.hasLineOfSight.
		///  8. Return HitValidationResult with valid=true on success.
		///
		/// \param request         Populated by the caller after parsing the network message.
		/// \param serverNowMs     Current server wall-clock time in milliseconds.
		/// \param estimatedRttMs  Estimated round-trip time for the requesting client.
		HitValidationResult ValidateHit(const LagCompSkillRequest& request,
		                                uint64_t serverNowMs,
		                                uint32_t estimatedRttMs) const;

		/// Return true when Init() has been called successfully.
		bool IsInitialized() const { return m_initialized; }

		/// Number of entities currently tracked.
		size_t TrackedEntityCount() const { return m_positionHistories.size(); }

	private:
		Config m_cfg{};
		bool   m_initialized = false;

		/// Per-entity ring-buffer position history.
		std::unordered_map<EntityId, EntityPositionHistory> m_positionHistories;
	};

} // namespace engine::server
