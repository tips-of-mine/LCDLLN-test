#pragma once

#include "engine/render/AssetRegistry.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace engine::render
{
	/// Batch key for instancing: same mesh + same material (M09.3).
	struct InstanceBatchKey
	{
		AssetId meshId = kInvalidAssetId;
		AssetId materialId = kInvalidAssetId;

		bool operator==(const InstanceBatchKey& o) const
		{
			return meshId == o.meshId && materialId == o.materialId;
		}
	};

	/// One instanced draw batch: key (mesh+material), LOD level, instance count, and byte offset into instance buffer (M09.3).
	/// Caller uploads instance mat4s (column-major) to the instance buffer; this offset points to this batch's data.
	constexpr uint32_t kInstanceMatrixSizeBytes = 64u; ///< mat4 column-major

	struct InstanceBatch
	{
		InstanceBatchKey key;
		uint32_t lodLevel = 0;
		uint32_t instanceCount = 0;
		/// Byte offset into the instance buffer where this batch's instanceCount mat4s are stored.
		uint32_t instanceBufferOffset = 0;
		/// Mesh to draw (required for RecordInstanced). Set by caller when building batches from key.
		const MeshAsset* mesh = nullptr;
		/// Global bindless material descriptor set (set 0). May be VK_NULL_HANDLE when bindless is disabled.
		VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE;
		/// Global material buffer index consumed by bindless material shaders.
		uint32_t materialIndex = 0;
	};
}
