#include "engine/world/terrain/TerrainChunkLoader.h"
#include "engine/world/StreamCache.h"

#include <sstream>

namespace engine::world::terrain
{
	std::shared_ptr<TerrainChunk> LoadFromCache(
		engine::world::StreamCache& cache,
		std::string_view cacheKey,
		std::string& outError)
	{
		auto blob = cache.Lookup(cacheKey);
		if (!blob.has_value())
		{
			outError = "cache miss";
			return nullptr;
		}
		auto chunk = std::make_shared<TerrainChunk>();
		std::span<const uint8_t> bytes(blob->data(), blob->size());
		if (!LoadTerrainBin(bytes, *chunk, outError)) return nullptr;
		return chunk;
	}

	std::string MakeTerrainCacheKey(int chunkX, int chunkZ)
	{
		std::ostringstream os;
		os << "chunks/chunk_" << chunkX << "_" << chunkZ << "/terrain.bin";
		return os.str();
	}
}
