#include "engine/world/terrain/TerrainMeshBuilder.h"

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
}
