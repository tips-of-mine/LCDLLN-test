#include "MeshLoader.h"

#include <fstream>
#include <filesystem>

namespace tools::hlod_builder
{
	namespace
	{
		constexpr uint32_t kMeshMagic = 0x4853454Du; // bytes "MESH"
		constexpr size_t kVertexStride = 32u;
	}

	MeshData LoadMesh(const std::string& contentDir, const std::string& relativePath)
	{
		MeshData out;
		std::filesystem::path fullPath = std::filesystem::path(contentDir) / relativePath;
		std::ifstream f(fullPath, std::ios::binary);
		if (!f)
			return out;

		uint32_t magic = 0, version = 0, numVertices = 0, numIndices = 0;
		f.read(reinterpret_cast<char*>(&magic), 4);
		f.read(reinterpret_cast<char*>(&version), 4);
		f.read(reinterpret_cast<char*>(&numVertices), 4);
		f.read(reinterpret_cast<char*>(&numIndices), 4);
		if (!f || magic != kMeshMagic || version != 1)
			return out;

		const size_t vertexBytes = numVertices * kVertexStride;
		const size_t indexBytes = numIndices * sizeof(uint32_t);
		out.vertices.resize(numVertices);
		out.indices.resize(numIndices);
		f.read(reinterpret_cast<char*>(out.vertices.data()), vertexBytes);
		f.read(reinterpret_cast<char*>(out.indices.data()), indexBytes);
		if (!f)
		{
			out.vertices.clear();
			out.indices.clear();
		}
		return out;
	}
}
