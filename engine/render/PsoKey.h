#pragma once

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// M18.5: PSO key for pipeline cache keying and warmup list.
	/// Hashes shaders (SPIR-V identity), pipeline state and formats.
	struct PsoKey
	{
		uint64_t hash = 0;

		/// Returns true if this key has a valid hash.
		bool Valid() const { return hash != 0; }
	};

	/// M18.5: Hash a graphics PSO key from render pass, subpass, layout and attachment formats.
	/// Used for warmup list and debug validation.
	PsoKey HashGraphicsPsoKey(VkRenderPass renderPass, uint32_t subpass,
		VkPipelineLayout layout,
		VkFormat colorFormat0, VkFormat depthFormat);

	/// M18.5: Hash a compute PSO key from pipeline layout and shader stage.
	PsoKey HashComputePsoKey(VkPipelineLayout layout, size_t spirvWordCount);
}
