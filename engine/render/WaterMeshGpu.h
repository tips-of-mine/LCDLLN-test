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
	/// Reconstruit a la demande depuis une WaterScene CPU via VMA staging.
	class WaterMeshGpu final
	{
	public:
		WaterMeshGpu() = default;
		WaterMeshGpu(const WaterMeshGpu&) = delete;
		WaterMeshGpu& operator=(const WaterMeshGpu&) = delete;

		/// Initialise l'objet. Pas d'allocation buffer ici — Rebuild s'en charge.
		/// \param vmaAllocator Handle VmaAllocator opaque (evite de polluer le header avec vk_mem_alloc.h).
		bool Init(VkDevice device, void* vmaAllocator);

		/// Reconstruit VBO/IBO depuis la scene. Realloue si capacite insuffisante,
		/// reutilise l'allocation existante sinon. Retourne false en cas d'erreur
		/// (logue ERROR, garde l'etat precedent intact).
		///
		/// \param transferPool  Command pool sur la queue transfer (ou graphics).
		/// \param transferQueue Queue de soumission (sera attendue via fence).
		/// \return true si l'upload a reussi.
		bool Rebuild(VkCommandPool transferPool, VkQueue transferQueue,
		             const engine::world::water::WaterScene& scene);

		/// Detruit toutes les ressources Vulkan. Safe sur etat non-init.
		void Destroy();

		VkBuffer GetVertexBuffer() const { return m_vbo; }
		VkBuffer GetIndexBuffer()  const { return m_ibo; }
		const std::vector<WaterInstanceDrawInfo>& GetDrawInfos() const { return m_drawInfos; }
		size_t GetInstanceCount() const { return m_drawInfos.size(); }
		bool IsValid() const { return m_vbo != VK_NULL_HANDLE && m_ibo != VK_NULL_HANDLE; }

	private:
		/// Alloue ou realloue VBO/IBO si la nouvelle taille depasse la capacite actuelle.
		/// Reutilise les buffers existants si la capacite est suffisante.
		/// \return false si vmaCreateBuffer echoue (logue ERROR).
		bool EnsureCapacity(VkDeviceSize newVboSize, VkDeviceSize newIboSize);

		VkDevice         m_device         = VK_NULL_HANDLE;
		void*            m_vmaAllocator   = nullptr;  // VmaAllocator (opaque)

		VkBuffer       m_vbo            = VK_NULL_HANDLE;
		void*          m_vboAllocation  = nullptr;  // VmaAllocation
		VkDeviceSize   m_vboCapacity    = 0;

		VkBuffer       m_ibo            = VK_NULL_HANDLE;
		void*          m_iboAllocation  = nullptr;  // VmaAllocation
		VkDeviceSize   m_iboCapacity    = 0;

		std::vector<WaterInstanceDrawInfo> m_drawInfos;
	};
}
