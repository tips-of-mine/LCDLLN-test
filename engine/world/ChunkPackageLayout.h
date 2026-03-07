#pragma once

#include "engine/world/WorldModel.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace engine::world
{
	/// Chunk package segment types for progressive loading (M10.5). Load order: Geo first, then Tex, then Instances, Nav, Probes.
	enum class ChunkSegment : uint8_t
	{
		Geo,       /// geo.pak — geometry/HLOD
		Tex,       /// tex.pak — textures
		Instances, /// instances.bin
		Nav,       /// navmesh.bin
		Probes     /// probes.bin
	};

	/// Number of segment types.
	constexpr uint32_t kChunkSegmentCount = 5u;

	/// Returns the relative filename for the segment (e.g. "geo.pak", "instances.bin").
	std::string_view GetChunkSegmentFilename(ChunkSegment segment);

	/// Returns load priority order: index 0 = first to load (Geo), then Tex, Instances, Nav, Probes.
	std::array<ChunkSegment, kChunkSegmentCount> GetChunkLoadOrder();

	/// Chunk.meta layout: chunk coords + flags; describes presence of geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin.
	struct ChunkMeta
	{
		ChunkCoord coord{ 0, 0 };
		uint32_t boundsMinX = 0;
		uint32_t boundsMinZ = 0;
		uint32_t boundsMaxX = 0;
		uint32_t boundsMaxZ = 0;
		uint32_t flags = 0; /// bit 0 = has geo, 1 = has tex, 2 = instances, 3 = nav, 4 = probes
	};

	/// Flag bits for ChunkMeta::flags.
	constexpr uint32_t kChunkMetaHasGeo = 1u << 0;
	constexpr uint32_t kChunkMetaHasTex = 1u << 1;
	constexpr uint32_t kChunkMetaHasInstances = 1u << 2;
	constexpr uint32_t kChunkMetaHasNav = 1u << 3;
	constexpr uint32_t kChunkMetaHasProbes = 1u << 4;
}
