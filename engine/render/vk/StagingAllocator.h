#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <cstddef>

namespace engine::core { class Config; }

namespace engine::render
{
	/// Persistent staging buffer ring for GPU uploads; sub-allocates from current ring slot per frame (M10.4).
	/// Ring size = 2 (frames in flight). Each buffer is host-visible, TRANSFER_SRC; size = upload budget.
	class StagingAllocator
	{
	public:
		StagingAllocator() = default;

		/// Creates the staging buffer ring. budgetBytesPerFrame = size of each buffer (e.g. 32MB).
		/// \param device Vulkan device.
		/// \param vmaAllocator VMA allocator (cast to VmaAllocator in impl).
		/// \return true on success.
		bool Init(VkDevice device, void* vmaAllocator, size_t budgetBytesPerFrame);

		/// Allocates a region from the current frame's staging buffer. Returns buffer and offset; 0 on failure (over budget).
		/// \param sizeBytes Size to allocate (will be aligned for transfer).
		/// \param outOffset Offset in the returned buffer.
		/// \return The current frame's staging buffer, or VK_NULL_HANDLE if no space.
		VkBuffer Allocate(size_t sizeBytes, VkDeviceSize& outOffset);

		/// Switches to the next ring slot and resets offset. Call once per frame (e.g. after fence wait).
		void BeginFrame(uint32_t frameIndex);

		/// Returns the current frame's staging buffer (for mapping/copy).
		VkBuffer GetCurrentBuffer() const;

		/// Destroys all staging buffers. Safe to call multiple times.
		void Destroy(VkDevice device);

	private:
		static constexpr uint32_t kRingSize = 2;
		struct Slot
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			void* vmaAllocation = nullptr;
			VkDeviceSize sizeBytes = 0;
		};
		std::array<Slot, kRingSize> m_ring{};
		void* m_vmaAllocator = nullptr;
		uint32_t m_currentSlot = 0;
		VkDeviceSize m_currentOffset = 0;
		VkDevice m_device = VK_NULL_HANDLE;
	};
}
