#include "ChunkPackageWriter.h"

#include <fstream>

namespace tools::zone_builder
{
	namespace
	{
		// Match engine/world/PakFormat.h layout for .pak files
		constexpr uint32_t kPakMagic = 0x4348504Bu;
		constexpr uint32_t kPakVersion = 1u;

		bool WriteEmptyPak(const std::string& path)
		{
			std::ofstream f(path, std::ios::binary);
			if (!f) return false;
			uint32_t magic = kPakMagic;
			uint32_t version = kPakVersion;
			uint32_t numEntries = 0;
			f.write(reinterpret_cast<const char*>(&magic), 4);
			f.write(reinterpret_cast<const char*>(&version), 4);
			f.write(reinterpret_cast<const char*>(&numEntries), 4);
			return !!f;
		}

		bool WriteChunkMeta(const std::string& path, int32_t cx, int32_t cz)
		{
			std::ofstream f(path, std::ios::binary);
			if (!f) return false;
			f.write(reinterpret_cast<const char*>(&cx), 4);
			f.write(reinterpret_cast<const char*>(&cz), 4);
			uint32_t boundsMinX = static_cast<uint32_t>(cx * 256);
			uint32_t boundsMinZ = static_cast<uint32_t>(cz * 256);
			uint32_t boundsMaxX = boundsMinX + 256;
			uint32_t boundsMaxZ = boundsMinZ + 256;
			f.write(reinterpret_cast<const char*>(&boundsMinX), 4);
			f.write(reinterpret_cast<const char*>(&boundsMinZ), 4);
			f.write(reinterpret_cast<const char*>(&boundsMaxX), 4);
			f.write(reinterpret_cast<const char*>(&boundsMaxZ), 4);
			uint32_t flags = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4); // has all segments
			f.write(reinterpret_cast<const char*>(&flags), 4);
			return !!f;
		}

		bool WriteEmptyBin(const std::string& path)
		{
			std::ofstream f(path, std::ios::binary);
			return !!f;
		}
	}

	bool WriteChunkPackage(const std::string& outputDir, int32_t chunkX, int32_t chunkZ)
	{
		std::string metaPath = outputDir + "/chunk.meta";
		if (!WriteChunkMeta(metaPath, chunkX, chunkZ))
			return false;
		if (!WriteEmptyPak(outputDir + "/geo.pak"))
			return false;
		if (!WriteEmptyPak(outputDir + "/tex.pak"))
			return false;
		if (!WriteEmptyBin(outputDir + "/instances.bin"))
			return false;
		if (!WriteEmptyBin(outputDir + "/navmesh.bin"))
			return false;
		if (!WriteEmptyBin(outputDir + "/probes.bin"))
			return false;
		return true;
	}
}
