// engine/render/WaterMeshGpu.h
#pragma once

#include "engine/world/water/WaterSurfaces.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Vertex format pour les meshes d'eau M100.14 (28 B = pos3 + uv2 + flowDir2).
	/// Distinct de `engine::world::water::WaterVertex` (M100.13, position seule, 12 B)
	/// qui est le format CPU-only produit par WaterMeshBuilder.
	struct WaterVertex
	{
		float position[3];
		float uv[2];
		float flowDir[2];
	};
	static_assert(sizeof(WaterVertex) == 28, "WaterVertex must be 28 bytes");

	/// Info de draw call par instance (lac OU riviere).
	/// `paramsIndex` est unifie :
	///   0..N_lakes-1                            = lacs (index dans `scene.lakes`)
	///   N_lakes..N_lakes+N_rivers-1             = rivieres (offset = paramsIndex - N_lakes dans `scene.rivers`)
	struct WaterInstanceDrawInfo
	{
		uint32_t firstIndex;   ///< Offset dans IBO global
		uint32_t indexCount;   ///< Nombre d'indices pour cette instance
		int32_t  vertexOffset; ///< Base vertex pour cette instance
		uint32_t paramsIndex;  ///< Voir doc struct
	};

	/// Helper CPU testable sans device : transforme une WaterScene en
	/// vertex + index arrays + drawInfos. Lacs en tete, rivieres ensuite.
	/// \param outVertices Concatene 7 floats par vertex (position3 + uv2 + flowDir2).
	/// \param outIndices  Indices uint32_t globaux ; chaque instance lit
	///                    [firstIndex, firstIndex+indexCount) en y ajoutant vertexOffset.
	/// \param outDrawInfos Une entree par lake puis par river.
	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos);

	/// Buffer GPU contenant tous les meshes d'eau (lakes + rivers concatenes).
	/// API GPU complete ajoutee en Task 3.
	class WaterMeshGpu final
	{
	public:
		WaterMeshGpu() = default;
		WaterMeshGpu(const WaterMeshGpu&) = delete;
		WaterMeshGpu& operator=(const WaterMeshGpu&) = delete;

		// API GPU (Init/Rebuild/Destroy) en Task 3.
		// Pour l'instant : accesseurs pour les drawInfos calcules CPU.
		const std::vector<WaterInstanceDrawInfo>& GetDrawInfos() const { return m_drawInfos; }
		size_t GetInstanceCount() const { return m_drawInfos.size(); }

	private:
		std::vector<WaterInstanceDrawInfo> m_drawInfos;
		// Champs Vulkan ajoutes en Task 3.
	};
}
