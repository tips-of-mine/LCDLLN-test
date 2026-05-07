#pragma once

// engine/render/terrain_chunk/TerrainChunkRenderer.h (Task 11 — M100)
//
// Orchestrateur principal du runtime terrain chunk-based : agrège
// `TerrainChunkPipeline`, le `ChunkRuntime` (LRU + budget), les caches GPU
// (mesh + splat) et le `LayerArrayLoader` pour rendre tous les chunks
// visibles fournis par le caller.
//
// Cycle de vie :
//   - `Init` au boot du Engine, après que la GBuffer renderPass et la
//     `cameraSetLayout` (set 0) sont disponibles. Charge la palette,
//     uploade les 3 arrays PBR + 2 samplers (synchrones via queue).
//   - `RenderVisibleChunks` est appelé chaque frame depuis la passe geometry,
//     INSIDE le render pass, après bind du viewport/scissor. Itère les
//     chunks fournis, lazy-uploade mesh+splat à la première visite, alloue
//     un VkDescriptorSet, écrit les 6 bindings, puis émet le draw via
//     `TerrainChunkPipeline::RecordChunkDraw`.
//   - `Tick` est appelé entre frames (avant `BeginFrame` du staging).
//     Évince les chunks Far excédant le budget, reset le pool descriptor.
//   - `Shutdown` libère toutes les ressources GPU dans l'ordre inverse de
//     `Init`.
//
// Pas de branche `m_editorEnabled` : critère M100.5/.9 — l'orchestrateur
// est utilisé identiquement par le client et l'éditeur monde.

#include "engine/render/TerrainChunkPipeline.h"
#include "engine/render/terrain_chunk/ChunkRuntime.h"
#include "engine/render/terrain_chunk/DescriptorSetPool.h"
#include "engine/render/terrain_chunk/LayerArrayLoader.h"
#include "engine/render/terrain_chunk/SplatMapGpuCache.h"
#include "engine/render/terrain_chunk/TerrainMeshGpuCache.h"
#include "engine/world/WorldModel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace engine::core { class Config; }
namespace engine::render { class AssetRegistry; }
namespace engine::render::vk { class StagingAllocator; }
namespace engine::world { class StreamCache; }

namespace engine::render::terrain_chunk
{
	/// Implémentation interne d'`IGpuBufferAllocator` qui crée des VkBuffer
	/// DEVICE_LOCAL (vertex/index) et upload via staging buffers one-shot.
	/// Synchronisation : submit + waitForFence par buffer (acceptable car
	/// premier touch d'un chunk = ~258 Ko, fréquence faible).
	class VulkanBufferAllocator final : public IGpuBufferAllocator
	{
	public:
		/// Init avec le device + queue + queueFamily du engine. Doit être
		/// appelée avant le premier `CreateAndUploadXxxBuffer`.
		void Init(VkDevice device, VkPhysicalDevice physDev,
			VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex);

		VkBuffer CreateAndUploadVertexBuffer(const void* srcBytes, size_t sizeBytes) override;
		VkBuffer CreateAndUploadIndexBuffer(const void* srcBytes, size_t sizeBytes) override;
		void DestroyBuffer(VkBuffer buffer) override;

	private:
		struct BufferEntry
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;
		};

		VkBuffer CreateAndUploadBuffer(const void* srcBytes, size_t sizeBytes,
			VkBufferUsageFlags usage);

		VkDevice m_device = VK_NULL_HANDLE;
		VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
		VkQueue m_queue = VK_NULL_HANDLE;
		uint32_t m_queueFamily = 0u;
		std::unordered_map<VkBuffer, BufferEntry> m_owned;
	};

	/// Implémentation interne d'`IGpuImageAllocator` (splat-maps RGBA8 257²).
	class VulkanImageAllocator final : public IGpuImageAllocator
	{
	public:
		void Init(VkDevice device, VkPhysicalDevice physDev,
			VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex);

		void CreateAndUploadRGBA8Image(uint32_t width, uint32_t height,
			const void* srcBytes, VkImage& outImage, VkImageView& outView) override;
		void DestroyImage(VkImage image, VkImageView view) override;

	private:
		struct ImageEntry
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;
		};

		VkDevice m_device = VK_NULL_HANDLE;
		VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
		VkQueue m_queue = VK_NULL_HANDLE;
		uint32_t m_queueFamily = 0u;
		std::unordered_map<VkImage, ImageEntry> m_owned;
	};

	/// Implémentation interne d'`IGpuImageArrayAllocator` (3 arrays PBR
	/// `WxHx8` + samplers). Reutilise les helpers du `VulkanImageAllocator`
	/// modulo l'arrayLayers paramétrable.
	class VulkanImageArrayAllocator final : public IGpuImageArrayAllocator
	{
	public:
		void Init(VkDevice device, VkPhysicalDevice physDev,
			VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex);

		void CreateAndUploadRGBA8Array(uint32_t width, uint32_t height,
			uint32_t layerCount, const void* pixelData,
			VkImage& outImage, VkImageView& outView) override;
		VkSampler CreateSampler(bool linear) override;
		void DestroyImage(VkImage image, VkImageView view) override;
		void DestroySampler(VkSampler sampler) override;

	private:
		struct ImageEntry
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;
		};

		VkDevice m_device = VK_NULL_HANDLE;
		VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
		VkQueue m_queue = VK_NULL_HANDLE;
		uint32_t m_queueFamily = 0u;
		std::unordered_map<VkImage, ImageEntry> m_owned;
	};

	/// Orchestrateur principal du runtime terrain chunk-based (Task 11). Voir
	/// docstring fichier pour le cycle de vie.
	///
	/// Contraintes thread/timing : appelé uniquement depuis le main thread.
	/// `RenderVisibleChunks` doit être appelé entre `vkCmdBeginRenderPass` et
	/// `vkCmdEndRenderPass` du caller (typiquement `GeometryPass::Record`).
	class TerrainChunkRenderer
	{
	public:
		TerrainChunkRenderer() = default;
		TerrainChunkRenderer(const TerrainChunkRenderer&) = delete;
		TerrainChunkRenderer& operator=(const TerrainChunkRenderer&) = delete;

		/// Init pipeline + caches + arrays PBR. Appelé une fois au boot.
		///
		/// \param device Logical device Vulkan.
		/// \param physDev Physical device (memory queries + sampler caps).
		/// \param renderPass Render pass GBuffer du caller (4 color + depth).
		/// \param cameraSetLayout Set 0 du pipeline (UBO viewProj).
		/// \param graphicsQueue Queue graphics du engine, utilisée pour les
		///        uploads synchrones boot-time + premier touch chunk.
		/// \param graphicsQueueFamilyIndex Family index correspondant.
		/// \param staging Staging allocator partagé (réservé pour de futurs
		///        uploads ring-based ; non utilisé en M100 — uploads one-shot).
		/// \param assetRegistry AssetRegistry pour lookup éventuel des PBR
		///        (réservé ; M100 lit directement les PNG via stb_image).
		/// \param streamCache Cache disque pour `LoadTerrainChunk` /
		///        `LoadSplatMap`.
		/// \param config Config global pour `editor.world.terrain.gpu_budget_mb`.
		/// \param contentRoot Chemin disque du `paths.content` (ex. "game/data").
		///        Utilisé pour résoudre les `<contentRoot>/terrain/placeholders/*.png`.
		/// \param shaderRootPath Chemin disque des shaders SPIR-V/GLSL (ex.
		///        "game/data/shaders").
		/// \param outError Message d'erreur sur échec.
		/// \return true si toutes les ressources sont init, sinon false (et
		///         `outError` rempli ; `Shutdown` reste sûr à appeler).
		bool Init(VkDevice device, VkPhysicalDevice physDev,
			VkRenderPass renderPass, VkDescriptorSetLayout cameraSetLayout,
			VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex,
			engine::render::vk::StagingAllocator* staging,
			engine::render::AssetRegistry* assetRegistry,
			engine::world::StreamCache* streamCache,
			const engine::core::Config& config,
			const std::string& contentRoot,
			const std::string& shaderRootPath,
			std::string& outError);

		/// Libère pipeline + caches + arrays PBR (ordre inverse de Init).
		/// Idempotent : sûr d'appeler même si `Init` a échoué.
		void Shutdown(VkDevice device);

		/// Dessine tous les `visibleChunks` du frame courant. Pour chaque
		/// chunk : load CPU via `StreamCache`, upload mesh+splat (1ère fois),
		/// alloc descriptor set + écriture des 6 bindings, RecordChunkDraw.
		/// Skippe silencieusement les chunks dont `terrain.bin` ou `splat.bin`
		/// est absent (= legacy renderer les dessinera).
		///
		/// Pré-condition : `vkCmdBeginRenderPass` déjà émis ; viewport/scissor
		/// configurés par le caller.
		///
		/// Effet de bord : alloue des VkDescriptorSet via `m_descPool` (libérés
		/// au `Tick` suivant), peut allouer des VkBuffer/VkImage via les
		/// allocateurs internes lors du premier touch.
		void RenderVisibleChunks(VkCommandBuffer cmd,
			VkDescriptorSet cameraSet,
			const engine::world::World& world,
			const std::vector<engine::world::GlobalChunkCoord>& visibleChunks);

		/// Maintenance entre frames : eviction LRU des chunks Far excédant
		/// le budget GPU + reset du descriptor pool (sets stateless par frame).
		/// Appelé typiquement dans `Engine::OnFrameBoundary` ou avant le
		/// `BeginFrame` du staging.
		void Tick(VkDevice device);

		/// True si `Init` a réussi et le pipeline est valide. Sert au caller
		/// (Engine/GeometryPass) pour décider s'il route vers ce renderer ou
		/// vers le legacy.
		bool IsValid() const { return m_pipeline.IsValid(); }

	private:
		/// Charge la palette + uploade les 3 arrays PBR (8 layers chacun) +
		/// crée les 2 samplers. Délégué depuis `Init` après que le
		/// `LayerArrayLoader::Init` skeleton a été appelé. Modifie le
		/// `LayerArrayResources` interne du loader via accesseur ami.
		bool LoadAndUploadLayerArrays(const engine::world::terrain::LayerPalette& palette,
			const std::string& contentRoot, std::string& outError);

		/// Crée un VkBuffer UBO host-visible 128 octets (LayerParams.tilingScale[8]
		/// padded std140) et upload les valeurs depuis la palette. Le buffer
		/// reste résident pour toute la vie du renderer (relu par chaque
		/// descriptor set chunk via vkUpdateDescriptorSets).
		bool CreateLayerParamsUbo(VkDevice device, VkPhysicalDevice physDev,
			const engine::world::terrain::LayerPalette& palette,
			std::string& outError);

		/// Track slot → coord pour libérer les bonnes entrées caches lors des
		/// evictions (alternative à un getter ChunkRuntime::GetCoordForSlot).
		std::unordered_map<ChunkSlotId, engine::world::GlobalChunkCoord> m_slotToCoord;

		// Composants principaux.
		TerrainChunkPipeline m_pipeline;
		ChunkRuntime m_runtime;
		TerrainMeshGpuCache m_meshCache;
		SplatMapGpuCache m_splatCache;
		LayerArrayLoader m_layerLoader;
		DescriptorSetPool m_descPool;

		// Allocateurs Vulkan concrets.
		VulkanBufferAllocator m_bufferAlloc;
		VulkanImageAllocator m_imageAlloc;
		VulkanImageArrayAllocator m_imageArrayAlloc;

		// LayerParams UBO (tilingScale[8] padded std140).
		VkBuffer m_layerParamsUbo = VK_NULL_HANDLE;
		VkDeviceMemory m_layerParamsMem = VK_NULL_HANDLE;
		VkDeviceSize m_layerParamsSize = 0;

		// Refs externes (non-owning).
		VkDevice m_device = VK_NULL_HANDLE;
		VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
		VkQueue m_graphicsQueue = VK_NULL_HANDLE;
		uint32_t m_graphicsQueueFamily = 0u;
		engine::render::vk::StagingAllocator* m_staging = nullptr;
		engine::render::AssetRegistry* m_assetRegistry = nullptr;
		engine::world::StreamCache* m_streamCache = nullptr;
		const engine::core::Config* m_config = nullptr;
	};
}
