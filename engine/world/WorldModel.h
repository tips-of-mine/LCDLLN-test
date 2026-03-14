#pragma once

#include "engine/math/Math.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::world
{
	/// Zone size in meters (one zone = 4 km side).
	constexpr int kZoneSize = 4096;
	/// Chunk size in meters (chunk = 256 m side).
	constexpr int kChunkSize = 256;
	/// Number of chunks per zone axis (kZoneSize / kChunkSize).
	constexpr int kChunksPerZoneAxis = kZoneSize / kChunkSize;

	/// Chunk index in world space (2D, XZ plane), without clamp.
	/// Values are signed and may be negative for zones left/below origin.
	struct GlobalChunkCoord
	{
		int32_t x = 0;
		int32_t z = 0;

		bool operator==(const GlobalChunkCoord& o) const { return x == o.x && z == o.z; }
		bool operator!=(const GlobalChunkCoord& o) const { return !(*this == o); }
	};

	/// Chunk index within a zone (always in [0, kChunksPerZoneAxis - 1]).
	struct LocalChunkCoord
	{
		int32_t x = 0;
		int32_t z = 0;
	};

	/// Zone index in the global world grid.
	struct ZoneCoord
	{
		int32_t x = 0;
		int32_t z = 0;
	};

	/// Compatibility alias for existing call-sites migrated incrementally.
	using ChunkCoord = GlobalChunkCoord;

	/// Axis-aligned bounds of a chunk in absolute world-space meters (XZ only).
	struct ChunkBounds
	{
		float minX = 0.0f;
		float minZ = 0.0f;
		float maxX = 0.0f;
		float maxZ = 0.0f;
	};

	/// Ring type for streaming priority (Active > Visible > Far).
	enum class ChunkRing : uint8_t
	{
		Active,  /// 5x5 around player
		Visible, /// 7x7 band (excluding Active)
		Far      /// HLOD beyond 7x7
	};

	/// Request emitted toward the streaming scheduler (M10).
	/// Priority is set by the scheduler (higher = load first): Active > Visible > Far; in front of player > behind.
	/// streamVersion (M10.2) is set by the scheduler when pushing; used to drop stale jobs at IO/CPU/GPU stages.
	struct ChunkRequest
	{
		GlobalChunkCoord chunkId;
		ChunkRing targetState;
		uint32_t priority = 0;
		uint32_t streamVersion = 0;
	};

	/// One zone: 4 km x 4 km, origin-local in meters.
	struct Zone
	{
		static constexpr int kSize = kZoneSize;
	};

	/// One chunk: 256 m x 256 m within a zone.
	struct Chunk
	{
		LocalChunkCoord coord;
		static constexpr int kSize = kChunkSize;
	};

	/// World: holds zone layout and produces chunk requests from player position.
	struct World
	{
		Zone zone;

		/// Converts absolute world position (meters, XZ) to global chunk coordinate.
		/// Uses floor and never clamps to the current zone.
		static GlobalChunkCoord WorldToGlobalChunkCoord(float worldX, float worldZ);

		/// Converts a global chunk coordinate into zone index + local chunk index.
		/// Local coordinates are always in [0, kChunksPerZoneAxis - 1].
		static void GlobalToZoneAndLocal(GlobalChunkCoord g, ZoneCoord& zone, LocalChunkCoord& local);

		/// Returns chunk bounds in absolute world-space meters (min/max X and Z).
		static struct ChunkBounds ChunkBounds(GlobalChunkCoord c);

		/// Updates required chunks from absolute player position.
		/// Uses hysteresis: only recomputes when player moves to a different chunk.
		/// Call GetPendingChunkRequests() after Update to obtain requests for the scheduler (M10).
		void Update(const engine::math::Vec3& playerPositionWorld);

		/// Returns the list of chunk requests produced by the last Update (for M10 scheduler).
		std::span<const ChunkRequest> GetPendingChunkRequests() const;

		/// Returns the ring for the given chunk relative to the last computed center (M09.2).
		/// Only valid after at least one Update(); otherwise returns ChunkRing::Far.
		ChunkRing GetRingForChunk(GlobalChunkCoord chunk) const;

	private:
		GlobalChunkCoord m_lastCenterChunk{ -1, -1 };
		bool m_hasLastCenter = false;
		std::vector<ChunkRequest> m_pendingRequests;
	};

	// --- Free functions (mirror World static API for use without a World instance) ---

	/// Converts absolute world position (meters, XZ) to global chunk coordinate.
	inline GlobalChunkCoord WorldToGlobalChunkCoord(float worldX, float worldZ)
	{
		return World::WorldToGlobalChunkCoord(worldX, worldZ);
	}

	/// Returns chunk bounds in absolute world-space meters.
	inline struct ChunkBounds ChunkBounds(ChunkCoord c)
	{
		return World::ChunkBounds(c);
	}
}
