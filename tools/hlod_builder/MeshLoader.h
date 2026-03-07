#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::hlod_builder
{
	/// Vertex format matching engine .mesh: position(3) + normal(3) + uv(2) = 32 bytes.
	struct Vertex
	{
		float px = 0.0f, py = 0.0f, pz = 0.0f;
		float nx = 0.0f, ny = 0.0f, nz = 0.0f;
		float u = 0.0f, v = 0.0f;
	};

	/// In-memory mesh (CPU only, for merge). Vertices and indices.
	struct MeshData
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	/// Loads a .mesh file (engine format: magic MESH, version 1, numV, numI, vertex data, index data).
	/// \param contentDir Root path for resolving relativePath.
	/// \param relativePath Path relative to contentDir (e.g. "meshes/foo.mesh").
	/// \return Filled MeshData or empty on failure.
	MeshData LoadMesh(const std::string& contentDir, const std::string& relativePath);
}
