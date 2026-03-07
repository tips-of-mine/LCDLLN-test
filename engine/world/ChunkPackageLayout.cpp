#include "engine/world/ChunkPackageLayout.h"

namespace engine::world
{
	std::string_view GetChunkSegmentFilename(ChunkSegment segment)
	{
		switch (segment)
		{
		case ChunkSegment::Geo:       return "geo.pak";
		case ChunkSegment::Tex:       return "tex.pak";
		case ChunkSegment::Instances: return "instances.bin";
		case ChunkSegment::Nav:       return "navmesh.bin";
		case ChunkSegment::Probes:    return "probes.bin";
		}
		return "geo.pak";
	}

	std::array<ChunkSegment, kChunkSegmentCount> GetChunkLoadOrder()
	{
		return {{
			ChunkSegment::Geo,
			ChunkSegment::Tex,
			ChunkSegment::Instances,
			ChunkSegment::Nav,
			ChunkSegment::Probes
		}};
	}
}
