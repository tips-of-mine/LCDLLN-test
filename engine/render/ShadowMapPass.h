#pragma once

#include "engine/render/FrameGraph.h"
#include "engine/render/AssetRegistry.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace engine::render
{
	/// ShadowMapPass: depth-only rendering of opaque geometry into a single shadow map
	/// (D32) for one cascade.
	///
	/// The pass owns a render pass and pipeline configured with:
	/// - one depth attachment (no colour attachments),
	/// - depth test/write enabled,
	/// - optional front-face culling to reduce shadow acne,
	/// - optional depth bias (constant + slope) set dynamically per draw.
	///
	/// The vertex shader is expected to take position (location = 0) and use a
	/// push-constant mat4 (lightViewProj) to transform vertices to clip space.
	class ShadowMapPass
	{
	public:
		ShadowMapPass() = default;
		ShadowMapPass(const ShadowMapPass&) = delete;
		ShadowMapPass& operator=(const ShadowMapPass&) = delete;

		/// Initialises the depth-only render pass and graphics pipeline.
		/// \param depthFormat Depth image format (typically VK_FORMAT_D32_SFLOAT).
		/// \param resolution  Shadow map width/height in pixels (square).
		/// \param vertSpirv   Vertex shader SPIR-V words.
		/// \param fragSpirv   Optional fragment shader SPIR-V words (may be a trivial shader).
		/// \return true on success.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat depthFormat,
			uint32_t resolution,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount);

		/// Records the shadow pass:
		/// - creates a temporary framebuffer using the given shadow map image view,
		/// - begins the depth-only render pass and clears depth,
		/// - sets viewport/scissor to the shadow map resolution,
		/// - binds pipeline and depth bias state,
		/// - draws the opaque mesh if provided,
		/// - ends the pass and destroys the framebuffer.
		/// The depth image is written with the provided lightViewProj matrix.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			ResourceId shadowMapId,
			const float* lightViewProjMat4,
			const MeshAsset* mesh,
			float depthBiasConstant,
			float depthBiasSlope,
			bool cullFrontFaces);

		/// Destroys the Vulkan objects owned by this pass. Safe to call multiple times.
		void Destroy(VkDevice device);

		/// Destroys all cached framebuffers and clears the cache (e.g. on resize when FG resources are recreated).
		void InvalidateFramebufferCache(VkDevice device);

		/// Returns true if the pass has been successfully initialised.
		bool IsValid() const { return m_renderPass != VK_NULL_HANDLE && m_pipeline != VK_NULL_HANDLE; }

	private:
		struct FramebufferKey
		{
			VkRenderPass renderPass = VK_NULL_HANDLE;
			VkImageView  depthView  = VK_NULL_HANDLE;
			uint32_t     width      = 0;
			uint32_t     height     = 0;
			bool operator==(const FramebufferKey& o) const;
		};
		struct FramebufferKeyHash
		{
			size_t operator()(const FramebufferKey& k) const;
		};
		std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_fbCache;

		VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline       m_pipeline       = VK_NULL_HANDLE;
		uint32_t         m_resolution     = 0;
	};
}

