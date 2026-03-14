#pragma once

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// GPU draw item consumed by the frustum-culling compute pass.
	/// meshId/materialId are kept for future batching, but M18.2 currently uploads
	/// one mesh stream and writes indirect commands for the geometry pass.
	struct GpuDrawItem
	{
		uint32_t meshId = 0;
		uint32_t materialId = 0;
		uint32_t lodLevel = 0;
		uint32_t reserved0 = 0;
		float modelMatrix[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		float boundsCenter[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float boundsExtents[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
		uint32_t indexCount = 0;
		uint32_t firstIndex = 0;
		int32_t vertexOffset = 0;
		uint32_t firstInstance = 0;
	};

	/// Compute pass that uploads draw items, appends a visible list, and writes indexed
	/// indirect commands consumed by the geometry pass.
	class GpuDrivenCullingPass
	{
	public:
		static constexpr uint32_t kDefaultFramesInFlight = 2u;
		static constexpr uint32_t kDefaultMaxDrawItems = 256u;

		GpuDrivenCullingPass() = default;
		GpuDrivenCullingPass(const GpuDrivenCullingPass&) = delete;
		GpuDrivenCullingPass& operator=(const GpuDrivenCullingPass&) = delete;

		/// Creates descriptor sets, per-frame buffers, and the compute pipeline.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			const uint32_t* computeSpirv, size_t computeWordCount,
			uint32_t framesInFlight = kDefaultFramesInFlight,
			uint32_t maxDrawItems = kDefaultMaxDrawItems);

		/// Uploads the draw items for the current frame slot into the storage buffer.
		bool UploadDrawItems(VkDevice device, uint32_t frameIndex, const GpuDrawItem* items, uint32_t itemCount);

		/// Resets the visible counter, dispatches frustum culling, and makes indirect
		/// commands visible to subsequent draw-indirect reads on the same command buffer.
		/// When Hi-Z data is not available, the pass falls back to frustum-only culling.
		void Record(VkDevice device, VkCommandBuffer cmd, const float* viewProjMatrix4x4, uint32_t frameIndex,
			VkImageView hiZImageView, VkExtent2D hiZExtent, uint32_t hiZMipCount);

		/// Releases all GPU resources. Safe to call when not initialized.
		void Destroy(VkDevice device);

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }
		VkBuffer GetIndirectBuffer(uint32_t frameIndex) const;
		uint32_t GetDrawItemCount(uint32_t frameIndex) const;

	private:
		struct PushConstants
		{
			float viewProj[16] = {};
			uint32_t drawItemCount = 0;
			uint32_t maxDrawItems = 0;
			uint32_t hiZMipCount = 0;
			uint32_t occlusionEnabled = 0;
			float hiZWidth = 1.0f;
			float hiZHeight = 1.0f;
		};

		struct FrameSlot
		{
			VkBuffer drawItemBuffer = VK_NULL_HANDLE;
			VkDeviceMemory drawItemMemory = VK_NULL_HANDLE;
			VkBuffer visibleIndexBuffer = VK_NULL_HANDLE;
			VkDeviceMemory visibleIndexMemory = VK_NULL_HANDLE;
			VkBuffer visibleCountBuffer = VK_NULL_HANDLE;
			VkDeviceMemory visibleCountMemory = VK_NULL_HANDLE;
			VkBuffer indirectBuffer = VK_NULL_HANDLE;
			VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			uint32_t uploadedDrawItemCount = 0;
		};

		bool CreateFrameSlotBuffers(VkDevice device, VkPhysicalDevice physicalDevice, FrameSlot& slot);
		bool CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
			VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
			VkBuffer& outBuffer, VkDeviceMemory& outMemory);
		bool CreateFallbackImage(VkDevice device, VkPhysicalDevice physicalDevice);
		void DestroyFrameSlot(VkDevice device, FrameSlot& slot);

		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkSampler m_hiZSampler = VK_NULL_HANDLE;
		VkImage m_fallbackHiZImage = VK_NULL_HANDLE;
		VkDeviceMemory m_fallbackHiZMemory = VK_NULL_HANDLE;
		VkImageView m_fallbackHiZView = VK_NULL_HANDLE;
		bool m_fallbackHiZReady = false;
		uint32_t m_framesInFlight = 0;
		uint32_t m_maxDrawItems = 0;
		FrameSlot* m_slots = nullptr;
	};
} // namespace engine::render
