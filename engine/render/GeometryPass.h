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
	///
	/// M03.3: pipeline layout now accepts an optional material descriptor set layout (set = 0,
	/// 3 combined-image-samplers: BaseColor/Normal/ORM). If materialLayout is valid, it is
	/// included in the pipeline layout and the descriptor set is bound before each draw call.
	class GeometryPass
	{
	public:
		GeometryPass() = default;
		GeometryPass(const GeometryPass&) = delete;
		GeometryPass& operator=(const GeometryPass&) = delete;

		/// Creates render pass and pipeline.
		/// Shader SPIR-V words must remain valid until Destroy() is called.
		/// \param formatVelocity  M07.3: velocity buffer format (e.g. R16G16_SFLOAT).
		/// \param materialLayout  Optional descriptor set layout for material textures (set = 0).
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          VkFormat formatA, VkFormat formatB, VkFormat formatC, VkFormat formatVelocity, VkFormat depthFormat,
		          const uint32_t* vertSpirv, size_t vertWordCount,
		          const uint32_t* fragSpirv, size_t fragWordCount,
		          VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE);

		/// Records the geometry pass: begin render pass, bind pipeline (and material descriptor
		/// set if provided), draw mesh, end pass.
		/// \param prevViewProjMat4  M07.3: previous frame view-projection (column-major 4×4).
		/// \param viewProjMat4      Current frame view-projection (column-major 4×4).
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry, VkExtent2D extent,
		            ResourceId idA, ResourceId idB, ResourceId idC, ResourceId idVelocity, ResourceId idDepth,
		            const float* prevViewProjMat4, const float* viewProjMat4, const MeshAsset* mesh,
		            VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE);

		/// Releases render pass and pipeline. Safe to call when not initialized.
		void Destroy(VkDevice device);

		bool IsValid() const { return m_renderPass != VK_NULL_HANDLE && m_pipeline != VK_NULL_HANDLE; }

		/// Returns true if this pass was initialised with a material descriptor set layout.
		bool HasMaterialLayout() const { return m_hasMaterialLayout; }

	private:
		VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline       m_pipeline       = VK_NULL_HANDLE;
		bool             m_hasMaterialLayout = false; ///< True if materialLayout was provided at Init.
	};

} // namespace engine::render
