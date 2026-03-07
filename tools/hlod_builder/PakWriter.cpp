#include "PakWriter.h"

#include <cstring>
#include <fstream>
#include <algorithm>

namespace tools::hlod_builder
{
	namespace
	{
		constexpr uint32_t kHlodMagic = 0x484C4F44u; // "HLOD"
		constexpr uint32_t kHlodVersion = 1u;
	}

	bool WriteHlodPak(const std::string& path, const std::vector<ChunkOutput>& chunks,
	                  const std::vector<std::string>& materialNames)
	{
		std::ofstream f(path, std::ios::binary);
		if (!f)
			return false;

		// Header
		uint32_t magic = kHlodMagic;
		uint32_t version = kHlodVersion;
		const uint32_t numChunks = static_cast<uint32_t>(chunks.size());
		f.write(reinterpret_cast<const char*>(&magic), 4);
		f.write(reinterpret_cast<const char*>(&version), 4);
		f.write(reinterpret_cast<const char*>(&numChunks), 4);

		// Collect unique material ids for materials list
		std::vector<uint32_t> materialIds;
		for (const ChunkOutput& co : chunks)
			for (const MergedMesh& m : co.clusters)
			{
				if (std::find(materialIds.begin(), materialIds.end(), m.materialId) == materialIds.end())
					materialIds.push_back(m.materialId);
			}
		std::sort(materialIds.begin(), materialIds.end());

		for (const ChunkOutput& co : chunks)
		{
			int32_t cx = co.chunkX;
			int32_t cz = co.chunkZ;
			const uint32_t numClusters = static_cast<uint32_t>(co.clusters.size());
			f.write(reinterpret_cast<const char*>(&cx), 4);
			f.write(reinterpret_cast<const char*>(&cz), 4);
			f.write(reinterpret_cast<const char*>(&numClusters), 4);

			for (const MergedMesh& mesh : co.clusters)
			{
				uint32_t matId = mesh.materialId;
				f.write(reinterpret_cast<const char*>(&matId), 4);
				const Bounds& b = mesh.bounds;
				f.write(reinterpret_cast<const char*>(&b.minX), 4);
				f.write(reinterpret_cast<const char*>(&b.minY), 4);
				f.write(reinterpret_cast<const char*>(&b.minZ), 4);
				f.write(reinterpret_cast<const char*>(&b.maxX), 4);
				f.write(reinterpret_cast<const char*>(&b.maxY), 4);
				f.write(reinterpret_cast<const char*>(&b.maxZ), 4);
				const uint32_t vCount = static_cast<uint32_t>(mesh.data.vertices.size());
				const uint32_t iCount = static_cast<uint32_t>(mesh.data.indices.size());
				f.write(reinterpret_cast<const char*>(&vCount), 4);
				f.write(reinterpret_cast<const char*>(&iCount), 4);
				if (vCount > 0)
					f.write(reinterpret_cast<const char*>(mesh.data.vertices.data()), vCount * 32);
				if (iCount > 0)
					f.write(reinterpret_cast<const char*>(mesh.data.indices.data()), iCount * 4);
			}
		}

		// Materials list: count, then for each id (4) + name length (4) + name bytes
		const uint32_t matCount = static_cast<uint32_t>(materialIds.size());
		f.write(reinterpret_cast<const char*>(&matCount), 4);
		for (uint32_t id : materialIds)
		{
			f.write(reinterpret_cast<const char*>(&id), 4);
			std::string name;
			if (id < materialNames.size())
				name = materialNames[id];
			if (name.empty())
				name = "material_" + std::to_string(id);
			const uint32_t len = static_cast<uint32_t>(name.size());
			f.write(reinterpret_cast<const char*>(&len), 4);
			f.write(name.data(), len);
		}
		return f.good();
	}
}
