#pragma once

// engine/render/TerrainChunkPipeline.h (M100.9 — Task 13)
//
// Pipeline Vulkan dédié au draw d'un `TerrainChunk` LOD0 avec son splat-map
// 8 layers (PR M100 Phase 3a — Splat Map System). Distinct du legacy
// `engine::render::terrain::TerrainRenderer` (single-zone démo plains 4-layer).
//
// Le pipeline accepte 3 descriptor sets :
//   set 0 — caméra (UBO viewProj fourni par le caller via `cameraSetLayout`).
//   set 1 — vide (réservé pour matériau futur, layout VK_NULL_HANDLE pour
//           l'instant ; voir « note set 1 » dans `Init`).
//   set 2 — splat-map du chunk (`m_splatSetLayout`) :
//             (0) sampler2D u_splatMap0 (4 premiers layers)
//             (1) sampler2D u_splatMap1 (4 derniers layers)
//             (2) sampler2DArray u_albedoArray (8 layers PBR)
//             (3) sampler2DArray u_normalArray
//             (4) sampler2DArray u_armArray (AO/Roughness/Metallic)
//             (5) UBO LayerParams.tilingScale[8] (32 octets utiles, padding
//                 std140 → 128 octets)
//
// Push constant (16 octets, vertex stage uniquement) :
//   vec3 chunkOriginWorld + 1 float pad (alignement vec4)
//
// Input vertex layout : `engine::world::terrain::TerrainVertex` (32 octets).
//   location 0 : pos float3
//   location 1 : normal float3
//   location 2 : uv float2
//
// Le fragment shader écrit 3 attachments (albedo / normal / arm) mais la
// passe de geometry du caller en a 4 (incluant velocity) ; le pipeline
// désactive `colorWriteMask` sur l'attachment velocity pour rester
// compatible avec le render pass GBuffer existant sans toucher aux shaders.
//
// Référence d'implémentation : `engine/render/GeometryPass.cpp` (boilerplate
// MRT 4 color + depth, render pass + pipeline) — copié-adapté minimalement.

#include <vulkan/vulkan_core.h>

#include <string>

namespace engine::world::terrain
{
	struct TerrainMeshGpu;
}

namespace engine::render
{
	/// Pipeline graphique terrain_chunk : vertex `TerrainVertex` 32 octets +
	/// fragment 8-layer splat blend. Le caller fournit `cameraSetLayout`
	/// (set 0) et le `renderPass` cible (typiquement la GBuffer geometry pass).
	/// Vie du pipeline : créé une fois au boot, détruit au shutdown engine.
	class TerrainChunkPipeline
	{
	public:
		TerrainChunkPipeline() = default;
		TerrainChunkPipeline(const TerrainChunkPipeline&) = delete;
		TerrainChunkPipeline& operator=(const TerrainChunkPipeline&) = delete;

		/// Compile les shaders SPIR-V depuis disque (`shaderRootPath`/terrain_chunk.{vert,frag})
		/// puis crée `m_splatSetLayout`, `m_pipelineLayout` et `m_pipeline`.
		///
		/// \param device Logical device Vulkan.
		/// \param physDev Physical device (ignoré pour l'instant, prévu pour
		///                requêtes capabilities futures — sampler anisotropy).
		/// \param renderPass Render pass GBuffer du caller (ex. `GeometryPass::m_renderPass`
		///                   ou `m_renderPassLoad`). Doit avoir 4 color attachments
		///                   (albedo/normal/arm/velocity) + 1 depth.
		/// \param cameraSetLayout Set 0 du caller (UBO `CameraUBO { mat4 viewProj; }`).
		/// \param shaderRootPath Racine où trouver `terrain_chunk.vert` / `.frag`
		///                       (ex. "game/data/shaders").
		/// \param outError Renseigné en cas d'échec (compilation shader, vkCreate*).
		/// \return true si tout est créé. false sinon (et `outError` non vide).
		///
		/// Effet de bord : utilise `engine::render::ShaderCompiler` pour invoquer
		/// glslangValidator (Vulkan SDK), donc nécessite glslangValidator dans
		/// PATH ou via VULKAN_SDK env var au moment de l'appel.
		bool Init(VkDevice device, VkPhysicalDevice physDev, VkRenderPass renderPass,
			VkDescriptorSetLayout cameraSetLayout,
			const std::string& shaderRootPath,
			std::string& outError);

		/// Détruit pipeline / pipeline layout / set layout / shader modules.
		/// Idempotent : sûr d'appeler même si `Init` a échoué.
		void Shutdown(VkDevice device);

		/// Bind le pipeline + sets (set 0 caméra, set 2 splat) + push constant
		/// chunkOrigin + vertex/index buffer du mesh, puis émet le draw indexed.
		/// Pré-condition : la passe `renderPass` du caller a déjà été démarrée
		/// (vkCmdBeginRenderPass) et le viewport / scissor sont déjà configurés
		/// (le caller possède la dynamic state).
		///
		/// \param mesh Mesh chunk uploadé GPU (vertex + index buffers non null).
		///             Si l'un des buffers est null, l'appel est no-op.
		/// \param chunkOriginX/Y/Z Coordonnées monde (mètres) du coin chunk-local
		///                         (0,0,0) du mesh. Pushed comme vec3+pad au vertex.
		void RecordChunkDraw(VkCommandBuffer cmd,
			VkDescriptorSet cameraSet, VkDescriptorSet splatSet,
			const engine::world::terrain::TerrainMeshGpu& mesh,
			float chunkOriginX, float chunkOriginY, float chunkOriginZ);

		/// Set layout pour le set 2 (splat resources). Le caller alloue ses
		/// `VkDescriptorSet` à partir de ce layout pour chaque chunk.
		VkDescriptorSetLayout GetSplatSetLayout() const { return m_splatSetLayout; }

		/// True si `Init` a réussi (pipeline + layout + render pass valides).
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkPipeline             m_pipeline       = VK_NULL_HANDLE;
		VkPipelineLayout       m_pipelineLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout  m_splatSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout  m_emptySet1Layout = VK_NULL_HANDLE; ///< Set 1 placeholder vide (Vulkan exige des sets contigus).
	};
}
