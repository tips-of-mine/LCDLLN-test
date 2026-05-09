#include "engine/render/PsoKey.h"

#include <cstring>
#include <functional>

namespace engine::render
{
	namespace
	{
		inline uint64_t CombineHash(uint64_t a, uint64_t b)
		{
			return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6u) + (a >> 2u));
		}
	}

	PsoKey HashGraphicsPsoKey(VkRenderPass renderPass, uint32_t subpass,
		VkPipelineLayout layout,
		VkFormat colorFormat0, VkFormat depthFormat)
	{
		PsoKey key;
		key.hash = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(renderPass));
		key.hash = CombineHash(key.hash, static_cast<uint64_t>(subpass));
		key.hash = CombineHash(key.hash, std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(layout)));
		key.hash = CombineHash(key.hash, static_cast<uint64_t>(colorFormat0));
		key.hash = CombineHash(key.hash, static_cast<uint64_t>(depthFormat));
		return key;
	}

	PsoKey HashComputePsoKey(VkPipelineLayout layout, size_t spirvWordCount)
	{
		PsoKey key;
		key.hash = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(layout));
		key.hash = CombineHash(key.hash, static_cast<uint64_t>(spirvWordCount));
		return key;
	}
}
