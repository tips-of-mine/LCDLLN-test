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

	/// Chunk index within a zone (2D, XZ plane).
	struct ChunkCoord
	{
		int x = 0;
		int z = 0;

		bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
		bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
	};

	/// Axis-aligned bounds of a chunk in zone-local meters (XZ only).
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
		ChunkCoord chunkId;
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
		ChunkCoord coord;
		static constexpr int kSize = kChunkSize;
	};

	/// World: holds zone layout and produces chunk requests from player position.
	struct World
	{
		Zone zone;

		/// Converts zone-local position (meters, XZ) to chunk coordinate.
		/// Clamps to valid zone chunk indices [0, kChunksPerZoneAxis - 1].
		static ChunkCoord WorldToChunkCoord(float worldX, float worldZ);

		/// Returns chunk bounds in zone-local meters (min/max X and Z).
		static ChunkBounds ChunkBounds(ChunkCoord c);

		/// Updates required chunks from player position (zone-local).
		/// Uses hysteresis: only recomputes when player moves to a different chunk.
		/// Call GetPendingChunkRequests() after Update to obtain requests for the scheduler (M10).
		void Update(const engine::math::Vec3& playerPositionZoneLocal);

		/// Returns the list of chunk requests produced by the last Update (for M10 scheduler).
		std::span<const ChunkRequest> GetPendingChunkRequests() const;

		/// Returns the ring for the given chunk relative to the last computed center (M09.2).
		/// Only valid after at least one Update(); otherwise returns ChunkRing::Far.
		ChunkRing GetRingForChunk(ChunkCoord chunk) const;

	private:
		ChunkCoord m_lastCenterChunk{ -1, -1 };
		bool m_hasLastCenter = false;
		std::vector<ChunkRequest> m_pendingRequests;
	};

	// --- Free functions (mirror World static API for use without a World instance) ---

	/// Converts zone-local position (meters, XZ) to chunk coordinate.
	inline ChunkCoord WorldToChunkCoord(float worldX, float worldZ)
	{
		return World::WorldToChunkCoord(worldX, worldZ);
	}

	/// Returns chunk bounds in zone-local meters.
	inline ChunkBounds ChunkBounds(ChunkCoord c)
	{
		return World::ChunkBounds(c);
	}
}
