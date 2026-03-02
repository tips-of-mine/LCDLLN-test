#pragma once

#include "engine/render/FrameGraph.h"
#include "engine/render/AssetRegistry.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

namespace engine::render
{
	/// Geometry pass: renders opaque geometry into GBuffer (A=albedo, B=normal, C=ORM) + depth.
	/// Uses a single pipeline (VS/PS), render pass with 3 color + 1 depth attachment, depth test enabled.
	class GeometryPass
	{
	public:
		GeometryPass() = default;
		GeometryPass(const GeometryPass&) = delete;
		GeometryPass& operator=(const GeometryPass&) = delete;

		/// Creates render pass and pipeline. Shader SPIR-V must remain valid until Destroy().
		/// \param vertSpirv Pointer to vertex shader SPIR-V code (uint32_t words).
		/// \param fragSpirv Pointer to fragment shader SPIR-V code.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat formatA, VkFormat formatB, VkFormat formatC, VkFormat depthFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount);

		/// Records the geometry pass: begin render pass, bind pipeline, draw mesh, end pass.
		/// Creates and destroys a temporary framebuffer each call. viewProjMat4 is column-major (16 floats).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
			ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idDepth,
			const float* viewProjMat4, const MeshAsset* mesh);

		/// Releases render pass and pipeline. Safe to call when not initialized.
		void Destroy(VkDevice device);

		bool IsValid() const { return m_renderPass != VK_NULL_HANDLE && m_pipeline != VK_NULL_HANDLE; }

	private:
		VkRenderPass m_renderPass = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
	};

}
