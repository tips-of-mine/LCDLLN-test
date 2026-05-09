#include "src/client/world/terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <cmath>

namespace engine::world::terrain
{
	namespace
	{
		/// Calcule normale et UV pour la cellule `(x, z)` du chunk.
		/// Normale par gradient (différences finies sur voisins immédiats).
		/// Aux bords, on duplique le voisin existant (pas d'échantillonnage hors
		/// chunk : les coutures inter-chunks sont garanties par le sculpt
		/// command, pas par le mesh builder).
		void ComputeNormalAndUv(const TerrainChunk& c, uint32_t x, uint32_t z, TerrainVertex& v)
		{
			const uint32_t xL = (x == 0) ? 0u : x - 1u;
			const uint32_t xR = std::min(x + 1u, c.resolutionX - 1u);
			const uint32_t zL = (z == 0) ? 0u : z - 1u;
			const uint32_t zR = std::min(z + 1u, c.resolutionZ - 1u);
			const float hL = c.heights[z * c.resolutionX + xL];
			const float hR = c.heights[z * c.resolutionX + xR];
			const float hD = c.heights[zL * c.resolutionX + x];
			const float hU = c.heights[zR * c.resolutionX + x];
			// Au centre, dx couvre 2 cellules ; aux bords, 1 cellule. On scale par
			// le span effectif (xR - xL) pour rester correct dans les deux cas.
			const float spanX = static_cast<float>(xR - xL) * c.cellSizeMeters;
			const float spanZ = static_cast<float>(zR - zL) * c.cellSizeMeters;
			const float invSpanX = (spanX > 0.0f) ? (1.0f / spanX) : 0.0f;
			const float invSpanZ = (spanZ > 0.0f) ? (1.0f / spanZ) : 0.0f;
			const float dhdx = (hR - hL) * invSpanX;
			const float dhdz = (hU - hD) * invSpanZ;
			// Normale = (-dh/dx, 1, -dh/dz), normalisée.
			float nx = -dhdx;
			const float ny = 1.0f;
			float nz = -dhdz;
			const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
			v.normal[0] = nx * invLen;
			v.normal[1] = ny * invLen;
			v.normal[2] = nz * invLen;
			v.uv[0] = static_cast<float>(x) / static_cast<float>(c.resolutionX - 1u);
			v.uv[1] = static_cast<float>(z) / static_cast<float>(c.resolutionZ - 1u);
		}
	}

	TerrainMeshCpu BuildLod0Mesh(const TerrainChunk& chunk)
	{
		TerrainMeshCpu mesh;
		const uint32_t rx = chunk.resolutionX;
		const uint32_t rz = chunk.resolutionZ;
		mesh.vertices.resize(static_cast<size_t>(rx) * rz);
		for (uint32_t z = 0; z < rz; ++z)
		{
			for (uint32_t x = 0; x < rx; ++x)
			{
				TerrainVertex& v = mesh.vertices[z * rx + x];
				v.position[0] = static_cast<float>(x) * chunk.cellSizeMeters;
				v.position[1] = chunk.heights[z * rx + x];
				v.position[2] = static_cast<float>(z) * chunk.cellSizeMeters;
				ComputeNormalAndUv(chunk, x, z, v);
			}
		}
		// Indices : 2 triangles par quad, 6 indices par quad, (rx-1) * (rz-1) quads.
		mesh.indices.reserve(static_cast<size_t>(rx - 1u) * (rz - 1u) * 6u);
		for (uint32_t z = 0; z < rz - 1u; ++z)
		{
			for (uint32_t x = 0; x < rx - 1u; ++x)
			{
				const uint32_t i00 = z * rx + x;
				const uint32_t i10 = i00 + 1u;
				const uint32_t i01 = i00 + rx;
				const uint32_t i11 = i01 + 1u;
				// Triangle 1 : (i00, i01, i10) — winding CCW vu d'en haut (Y+).
				mesh.indices.push_back(i00);
				mesh.indices.push_back(i01);
				mesh.indices.push_back(i10);
				// Triangle 2 : (i10, i01, i11).
				mesh.indices.push_back(i10);
				mesh.indices.push_back(i01);
				mesh.indices.push_back(i11);
			}
		}
		return mesh;
	}

	TerrainMeshCpu BuildLodMesh(const TerrainLod& lod, bool withSkirt)
	{
		TerrainMeshCpu mesh;
		const uint32_t r = lod.resolution;
		const float cs = lod.cellSizeMeters;
		mesh.vertices.resize(static_cast<size_t>(r) * r);
		for (uint32_t z = 0; z < r; ++z)
		{
			for (uint32_t x = 0; x < r; ++x)
			{
				TerrainVertex& v = mesh.vertices[z * r + x];
				v.position[0] = static_cast<float>(x) * cs;
				v.position[1] = lod.heights[z * r + x];
				v.position[2] = static_cast<float>(z) * cs;
				// Normale par diff finie sur ce niveau de LOD (approximation OK).
				const uint32_t xL = (x == 0u) ? 0u : x - 1u;
				const uint32_t xR = std::min(x + 1u, r - 1u);
				const uint32_t zL = (z == 0u) ? 0u : z - 1u;
				const uint32_t zR = std::min(z + 1u, r - 1u);
				const float hL = lod.heights[z * r + xL];
				const float hR = lod.heights[z * r + xR];
				const float hD = lod.heights[zL * r + x];
				const float hU = lod.heights[zR * r + x];
				const float spanX = static_cast<float>(xR - xL) * cs;
				const float spanZ = static_cast<float>(zR - zL) * cs;
				const float invSpanX = (spanX > 0.0f) ? (1.0f / spanX) : 0.0f;
				const float invSpanZ = (spanZ > 0.0f) ? (1.0f / spanZ) : 0.0f;
				const float dhdx = (hR - hL) * invSpanX;
				const float dhdz = (hU - hD) * invSpanZ;
				const float nx = -dhdx, ny = 1.0f, nz = -dhdz;
				const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
				v.normal[0] = nx * invLen;
				v.normal[1] = ny * invLen;
				v.normal[2] = nz * invLen;
				v.uv[0] = static_cast<float>(x) / static_cast<float>(r - 1u);
				v.uv[1] = static_cast<float>(z) / static_cast<float>(r - 1u);
			}
		}
		// Indices : grille principale.
		mesh.indices.reserve(static_cast<size_t>(r - 1u) * (r - 1u) * 6u);
		for (uint32_t z = 0; z < r - 1u; ++z)
		{
			for (uint32_t x = 0; x < r - 1u; ++x)
			{
				const uint32_t i00 = z * r + x;
				const uint32_t i10 = i00 + 1u;
				const uint32_t i01 = i00 + r;
				const uint32_t i11 = i01 + 1u;
				mesh.indices.push_back(i00);
				mesh.indices.push_back(i01);
				mesh.indices.push_back(i10);
				mesh.indices.push_back(i10);
				mesh.indices.push_back(i01);
				mesh.indices.push_back(i11);
			}
		}

		if (withSkirt)
		{
			constexpr float kSkirtDropMeters = 2.0f;
			// Helper : ajoute un vertex skirt (copie du vertex grille avec y - 2 m).
			auto AddSkirtVertex = [&](uint32_t x, uint32_t z) -> uint32_t
			{
				TerrainVertex v = mesh.vertices[z * r + x];
				v.position[1] -= kSkirtDropMeters;
				mesh.vertices.push_back(v);
				return static_cast<uint32_t>(mesh.vertices.size() - 1u);
			};
			// Bord nord (z = 0).
			for (uint32_t x = 0; x < r - 1u; ++x)
			{
				const uint32_t a = 0u * r + x;
				const uint32_t b = a + 1u;
				const uint32_t aS = AddSkirtVertex(x, 0u);
				const uint32_t bS = AddSkirtVertex(x + 1u, 0u);
				mesh.indices.push_back(a);  mesh.indices.push_back(aS); mesh.indices.push_back(b);
				mesh.indices.push_back(b);  mesh.indices.push_back(aS); mesh.indices.push_back(bS);
			}
			// Bord sud (z = r-1).
			for (uint32_t x = 0; x < r - 1u; ++x)
			{
				const uint32_t a = (r - 1u) * r + x;
				const uint32_t b = a + 1u;
				const uint32_t aS = AddSkirtVertex(x, r - 1u);
				const uint32_t bS = AddSkirtVertex(x + 1u, r - 1u);
				mesh.indices.push_back(a);  mesh.indices.push_back(b);  mesh.indices.push_back(aS);
				mesh.indices.push_back(b);  mesh.indices.push_back(bS); mesh.indices.push_back(aS);
			}
			// Bord ouest (x = 0).
			for (uint32_t z = 0; z < r - 1u; ++z)
			{
				const uint32_t a = z * r + 0u;
				const uint32_t b = (z + 1u) * r + 0u;
				const uint32_t aS = AddSkirtVertex(0u, z);
				const uint32_t bS = AddSkirtVertex(0u, z + 1u);
				mesh.indices.push_back(a);  mesh.indices.push_back(b);  mesh.indices.push_back(aS);
				mesh.indices.push_back(b);  mesh.indices.push_back(bS); mesh.indices.push_back(aS);
			}
			// Bord est (x = r-1).
			for (uint32_t z = 0; z < r - 1u; ++z)
			{
				const uint32_t a = z * r + (r - 1u);
				const uint32_t b = (z + 1u) * r + (r - 1u);
				const uint32_t aS = AddSkirtVertex(r - 1u, z);
				const uint32_t bS = AddSkirtVertex(r - 1u, z + 1u);
				mesh.indices.push_back(a);  mesh.indices.push_back(aS); mesh.indices.push_back(b);
				mesh.indices.push_back(b);  mesh.indices.push_back(aS); mesh.indices.push_back(bS);
			}
		}
		return mesh;
	}
}
