#pragma once

// engine/render/terrain_chunk/DescriptorSetPool.h
//
// Pool dédié au splat set du `TerrainChunkPipeline` (M100). Sized pour la
// taille max de Visible ring (7×7 = 49 sets résidents simultanés). Le set
// layout du pipeline a 6 bindings : 5 combined image samplers (splatMap0,
// splatMap1, albedoArray, normalArray, armArray) + 1 uniform buffer
// (LayerParams.tilingScale[8]).
//
// Reset au Tick (entre frames) : les sets sont stateless par frame — le
// caller alloue à chaque RenderVisibleChunks et écrit ses bindings.

#include <string>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	/// Pool VkDescriptorPool dédié au splat set. Init/Shutdown idempotent.
	/// Pas thread-safe.
	class DescriptorSetPool
	{
	public:
		/// Crée le VkDescriptorPool dimensionné pour `maxSets` sets, chacun
		/// avec 5 combined image samplers + 1 UBO. Pattern aligné sur
		/// `terrain_chunk.frag` (set 2).
		/// \param splatSetLayout Layout du set splat (fourni par
		///        `TerrainChunkPipeline::GetSplatSetLayout()`).
		/// \return true si succès, sinon `outError` renseigné.
		bool Init(VkDevice device, VkDescriptorSetLayout splatSetLayout,
			uint32_t maxSets, std::string& outError);

		/// Détruit le pool. Idempotent.
		void Shutdown(VkDevice device);

		/// Alloue un descriptor set du layout splat. Le caller est responsable
		/// de l'écriture (`vkUpdateDescriptorSets`). Retourne VK_NULL_HANDLE
		/// si le pool est saturé ou si Init n'a pas réussi.
		VkDescriptorSet Allocate(VkDevice device);

		/// Reset le pool (libère tous les sets alloués). Préserve `m_pool`
		/// lui-même — réutilisable directement après. Appelé chaque Tick.
		void Reset(VkDevice device);

		uint32_t GetMaxSets() const { return m_maxSets; }
		uint32_t GetAllocatedSets() const { return m_allocatedSets; }

	private:
		VkDescriptorPool m_pool = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
		uint32_t m_maxSets = 0;
		uint32_t m_allocatedSets = 0;
	};
}
