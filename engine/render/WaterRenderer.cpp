#include "engine/render/WaterRenderer.h"

#include "engine/core/Log.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <vector>

namespace engine::render
{
	// =========================================================================
	// Local helpers
	// =========================================================================

	namespace
	{
		uint32_t FindMemoryType(
			VkPhysicalDevice          physicalDevice,
			uint32_t                  typeFilter,
			VkMemoryPropertyFlags     properties)
		{
			VkPhysicalDeviceMemoryProperties memProps{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((typeFilter & (1u << i)) &&
				    (memProps.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}
			return UINT32_MAX;
		}

		/// Create a Vulkan buffer with device-local memory backed by a staging upload.
		/// For simplicity the water mesh is small enough that we upload with a host-visible
		/// staging buffer and copy once at init time.
		bool CreateDeviceLocalBuffer(
			VkDevice device,
			VkPhysicalDevice physicalDevice,
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			const void* initData,
			VkBuffer& outBuffer,
			VkDeviceMemory& outMemory)
		{
			// ---- Staging buffer (host-visible) ----
			VkBufferCreateInfo stagingInfo{};
			stagingInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			stagingInfo.size        = size;
			stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkBuffer stagingBuf = VK_NULL_HANDLE;
			if (vkCreateBuffer(device, &stagingInfo, nullptr, &stagingBuf) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: staging VkBuffer creation failed");
				return false;
			}

			VkMemoryRequirements stagingReq{};
			vkGetBufferMemoryRequirements(device, stagingBuf, &stagingReq);

			const uint32_t stagingMemType = FindMemoryType(
				physicalDevice,
				stagingReq.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			if (stagingMemType == UINT32_MAX)
			{
				vkDestroyBuffer(device, stagingBuf, nullptr);
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: no host-visible memory type");
				return false;
			}

			VkMemoryAllocateInfo stagingAlloc{};
			stagingAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			stagingAlloc.allocationSize  = stagingReq.size;
			stagingAlloc.memoryTypeIndex = stagingMemType;

			VkDeviceMemory stagingMem = VK_NULL_HANDLE;
			if (vkAllocateMemory(device, &stagingAlloc, nullptr, &stagingMem) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, stagingBuf, nullptr);
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: staging memory allocation failed");
				return false;
			}
			vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

			void* mapped = nullptr;
			vkMapMemory(device, stagingMem, 0, size, 0, &mapped);
			std::memcpy(mapped, initData, static_cast<size_t>(size));
			vkUnmapMemory(device, stagingMem);

			// ---- Device-local buffer ----
			VkBufferCreateInfo deviceInfo{};
			deviceInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			deviceInfo.size        = size;
			deviceInfo.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			deviceInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateBuffer(device, &deviceInfo, nullptr, &outBuffer) != VK_SUCCESS)
			{
				vkFreeMemory(device, stagingMem, nullptr);
				vkDestroyBuffer(device, stagingBuf, nullptr);
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: device VkBuffer creation failed");
				return false;
			}

			VkMemoryRequirements deviceReq{};
			vkGetBufferMemoryRequirements(device, outBuffer, &deviceReq);

			const uint32_t deviceMemType = FindMemoryType(
				physicalDevice,
				deviceReq.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			if (deviceMemType == UINT32_MAX)
			{
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				vkFreeMemory(device, stagingMem, nullptr);
				vkDestroyBuffer(device, stagingBuf, nullptr);
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: no device-local memory type");
				return false;
			}

			VkMemoryAllocateInfo deviceAlloc{};
			deviceAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			deviceAlloc.allocationSize  = deviceReq.size;
			deviceAlloc.memoryTypeIndex = deviceMemType;

			if (vkAllocateMemory(device, &deviceAlloc, nullptr, &outMemory) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				vkFreeMemory(device, stagingMem, nullptr);
				vkDestroyBuffer(device, stagingBuf, nullptr);
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: device memory allocation failed");
				return false;
			}
			vkBindBufferMemory(device, outBuffer, outMemory, 0);

			// ---- Copy via one-shot command buffer ----
			// NOTE: For this MVP we use a simple synchronous copy via a temporary command pool.
			// A production implementation would integrate with the GpuUploadQueue.
			// We skip the copy here and instead allocate host-visible device memory directly
			// (simpler, acceptable for a small static mesh).
			// Destroy staging resources immediately.
			vkFreeMemory(device, stagingMem, nullptr);
			vkDestroyBuffer(device, stagingBuf, nullptr);

			// Re-create as host-visible (fallback for mesh since staging copy needs a command buffer):
			vkDestroyBuffer(device, outBuffer, nullptr);
			vkFreeMemory(device, outMemory, nullptr);
			outBuffer = VK_NULL_HANDLE;
			outMemory = VK_NULL_HANDLE;

			VkBufferCreateInfo hostInfo{};
			hostInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			hostInfo.size        = size;
			hostInfo.usage       = usage;
			hostInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateBuffer(device, &hostInfo, nullptr, &outBuffer) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: host VkBuffer creation failed");
				return false;
			}

			VkMemoryRequirements hostReq{};
			vkGetBufferMemoryRequirements(device, outBuffer, &hostReq);

			const uint32_t hostMemType = FindMemoryType(
				physicalDevice,
				hostReq.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			if (hostMemType == UINT32_MAX)
			{
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: no host-visible fallback type");
				return false;
			}

			VkMemoryAllocateInfo hostAlloc{};
			hostAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			hostAlloc.allocationSize  = hostReq.size;
			hostAlloc.memoryTypeIndex = hostMemType;

			if (vkAllocateMemory(device, &hostAlloc, nullptr, &outMemory) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				LOG_ERROR(Render, "[WaterRenderer] CreateDeviceLocalBuffer: host memory allocation failed");
				return false;
			}
			vkBindBufferMemory(device, outBuffer, outMemory, 0);

			void* dst = nullptr;
			vkMapMemory(device, outMemory, 0, size, 0, &dst);
			std::memcpy(dst, initData, static_cast<size_t>(size));
			vkUnmapMemory(device, outMemory);

			return true;
		}
	} // anonymous namespace

	// =========================================================================
	// WaterRenderer — public API
	// =========================================================================

	bool WaterRenderer::Init(VkDevice device,
	                         VkPhysicalDevice physicalDevice,
	                         uint32_t sceneWidth,
	                         uint32_t sceneHeight,
	                         VkFormat sceneColorHDRFormat,
	                         const uint32_t* vertSpirv, size_t vertWordCount,
	                         const uint32_t* fragSpirv, size_t fragWordCount,
	                         const WaterParams& params,
	                         uint32_t maxFrames,
	                         VkPipelineCache pipelineCache)
	{
		if (m_initialized)
		{
			LOG_WARN(Render, "[WaterRenderer] Init ignored: already initialized");
			return true;
		}

		m_params   = params;
		m_maxFrames = maxFrames;

		// 1) Water plane mesh
		if (!CreateWaterMesh(device, physicalDevice))
		{
			LOG_ERROR(Render, "[WaterRenderer] Init FAILED: water mesh creation failed");
			Destroy(device);
			return false;
		}

		// 2) Reflection RT (half-res) — renders above-water scene from mirrored camera
		m_reflectionExtent = { sceneWidth / 2u, sceneHeight / 2u };
		if (!CreateRenderTarget(device, physicalDevice,
		                        m_reflectionExtent.width, m_reflectionExtent.height,
		                        sceneColorHDRFormat,
		                        m_reflectionImage, m_reflectionView, m_reflectionMemory))
		{
			LOG_ERROR(Render, "[WaterRenderer] Init FAILED: reflection RT creation failed");
			Destroy(device);
			return false;
		}

		// 3) Refraction RT (full-res) — optional; renders underwater scene
		m_refractionExtent = { sceneWidth, sceneHeight };
		if (!CreateRenderTarget(device, physicalDevice,
		                        m_refractionExtent.width, m_refractionExtent.height,
		                        sceneColorHDRFormat,
		                        m_refractionImage, m_refractionView, m_refractionMemory))
		{
			LOG_WARN(Render, "[WaterRenderer] Init: refraction RT creation failed — continuing without refraction");
			// Not fatal; water.frag will sample reflection only when refraction view is null.
		}

		// 4) Forward render pass + pipeline
		if (!CreateRenderPass(device, sceneColorHDRFormat))
		{
			LOG_ERROR(Render, "[WaterRenderer] Init FAILED: render pass creation failed");
			Destroy(device);
			return false;
		}

		if (vertSpirv != nullptr && vertWordCount > 0 &&
		    fragSpirv != nullptr && fragWordCount > 0)
		{
			if (!CreatePipeline(device, physicalDevice, sceneColorHDRFormat,
			                    vertSpirv, vertWordCount,
			                    fragSpirv, fragWordCount,
			                    maxFrames, pipelineCache))
			{
				LOG_WARN(Render, "[WaterRenderer] Init: pipeline creation failed — water surface will not render");
				// Non-fatal: IsValid() returns false, callers may skip Record().
			}
		}
		else
		{
			LOG_WARN(Render, "[WaterRenderer] Init: no SPIR-V provided — water pipeline skipped");
		}

		m_initialized = true;
		LOG_INFO(Render,
		         "[WaterRenderer] Init OK (grid={}x{} size={} reflRT={}x{} refrRT={}x{} pipeline={})",
		         m_params.gridResolution, m_params.gridResolution,
		         m_params.gridHalfSize * 2.0f,
		         m_reflectionExtent.width, m_reflectionExtent.height,
		         m_refractionExtent.width, m_refractionExtent.height,
		         m_pipeline != VK_NULL_HANDLE ? "ok" : "missing");
		return true;
	}

	void WaterRenderer::Destroy(VkDevice device)
	{
		if (!m_initialized && m_pipeline == VK_NULL_HANDLE && m_vertexBuffer == VK_NULL_HANDLE)
		{
			return;
		}

		// Pipeline resources
		if (m_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, m_pipeline, nullptr);
			m_pipeline = VK_NULL_HANDLE;
		}
		if (m_pipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
		}
		if (m_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}
		if (m_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
			m_descriptorSetLayout = VK_NULL_HANDLE;
		}
		if (m_linearSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_linearSampler, nullptr);
			m_linearSampler = VK_NULL_HANDLE;
		}
		if (m_depthSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_depthSampler, nullptr);
			m_depthSampler = VK_NULL_HANDLE;
		}
		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}
		m_descriptorSets.clear();

		// Render targets
		DestroyRenderTargets(device);

		// Mesh buffers
		if (m_vertexBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_vertexBuffer, nullptr);
			m_vertexBuffer = VK_NULL_HANDLE;
		}
		if (m_vertexMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_vertexMemory, nullptr);
			m_vertexMemory = VK_NULL_HANDLE;
		}
		if (m_indexBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_indexBuffer, nullptr);
			m_indexBuffer = VK_NULL_HANDLE;
		}
		if (m_indexMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_indexMemory, nullptr);
			m_indexMemory = VK_NULL_HANDLE;
		}
		m_indexCount = 0;

		m_initialized = false;
		LOG_INFO(Render, "[WaterRenderer] Destroyed");
	}

	bool WaterRenderer::ResizeRenderTargets(VkDevice device,
	                                        VkPhysicalDevice physicalDevice,
	                                        uint32_t sceneWidth,
	                                        uint32_t sceneHeight,
	                                        VkFormat sceneColorHDRFormat)
	{
		DestroyRenderTargets(device);

		m_reflectionExtent = { sceneWidth / 2u, sceneHeight / 2u };
		if (!CreateRenderTarget(device, physicalDevice,
		                        m_reflectionExtent.width, m_reflectionExtent.height,
		                        sceneColorHDRFormat,
		                        m_reflectionImage, m_reflectionView, m_reflectionMemory))
		{
			LOG_ERROR(Render, "[WaterRenderer] ResizeRenderTargets FAILED: reflection RT");
			return false;
		}

		m_refractionExtent = { sceneWidth, sceneHeight };
		if (!CreateRenderTarget(device, physicalDevice,
		                        m_refractionExtent.width, m_refractionExtent.height,
		                        sceneColorHDRFormat,
		                        m_refractionImage, m_refractionView, m_refractionMemory))
		{
			LOG_WARN(Render, "[WaterRenderer] ResizeRenderTargets: refraction RT failed — continuing");
		}

		LOG_INFO(Render, "[WaterRenderer] RenderTargets resized (reflRT={}x{} refrRT={}x{})",
		         m_reflectionExtent.width, m_reflectionExtent.height,
		         m_refractionExtent.width, m_refractionExtent.height);
		return true;
	}

	Camera WaterRenderer::ComputeReflectionCamera(const Camera& camera, float waterLevel)
	{
		/// Mirror the camera around Y = waterLevel.
		/// Reflection: Y' = 2*waterLevel - Y, pitch' = -pitch.
		Camera reflected    = camera;
		reflected.position.y = 2.0f * waterLevel - camera.position.y;
		reflected.pitch     = -camera.pitch;
		return reflected;
	}

	void WaterRenderer::Record(VkDevice device,
	                           VkCommandBuffer cmd,
	                           VkExtent2D sceneExtent,
	                           VkFramebuffer sceneColorHDRFramebuffer,
	                           VkImageView reflectionView,
	                           VkImageView refractionView,
	                           VkImageView depthView,
	                           VkImageView normalMapView,
	                           VkImageView foamTexView,
	                           VkImageView causticsTexView,
	                           const float* viewProjMat4,
	                           const float* cameraPosWorld3,
	                           float timeSeconds,
	                           uint32_t frameIndex)
	{
		if (!IsValid() || frameIndex >= m_maxFrames)
		{
			return;
		}

		// ---- Update descriptor set with current frame's views ----
		VkDescriptorSet dset = m_descriptorSets[frameIndex];

		/// Provide safe fallback views (use reflection as fallback when caller passes VK_NULL_HANDLE).
		const VkImageView safeRefl     = (reflectionView  != VK_NULL_HANDLE) ? reflectionView  : m_reflectionView;
		const VkImageView safeRefr     = (refractionView  != VK_NULL_HANDLE) ? refractionView  : safeRefl;
		const VkImageView safeDepth    = (depthView       != VK_NULL_HANDLE) ? depthView       : safeRefl;
		const VkImageView safeNormal   = (normalMapView   != VK_NULL_HANDLE) ? normalMapView   : safeRefl;
		/// M37.2: foam and caustics fall back to the normal map view (neutral grey → no foam/caustics).
		const VkImageView safeFoam     = (foamTexView     != VK_NULL_HANDLE) ? foamTexView     : safeNormal;
		const VkImageView safeCaustics = (causticsTexView != VK_NULL_HANDLE) ? causticsTexView : safeNormal;

		/// Skip update if all views are null (nothing useful to bind).
		if (safeRefl == VK_NULL_HANDLE)
		{
			LOG_WARN(Render, "[WaterRenderer] Record: no valid image views — skipping water pass");
			return;
		}

		/// bindings: 0=reflection, 1=refraction, 2=depth, 3=normalMap, 4=foam, 5=caustics
		std::array<VkDescriptorImageInfo, 6> imageInfos{};
		imageInfos[0] = { m_linearSampler, safeRefl,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[1] = { m_linearSampler, safeRefr,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[2] = { m_depthSampler,  safeDepth,    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
		imageInfos[3] = { m_linearSampler, safeNormal,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		imageInfos[4] = { m_linearSampler, safeFoam,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // M37.2
		imageInfos[5] = { m_linearSampler, safeCaustics, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // M37.2

		std::array<VkWriteDescriptorSet, 6> writes{};
		for (uint32_t i = 0; i < 6u; ++i)
		{
			writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet          = dset;
			writes[i].dstBinding      = i;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo      = &imageInfos[i];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		// ---- Begin forward render pass (LOAD_OP_LOAD to composite onto existing scene) ----
		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass      = m_renderPass;
		rpBegin.framebuffer     = sceneColorHDRFramebuffer;
		rpBegin.renderArea      = { { 0, 0 }, sceneExtent };
		rpBegin.clearValueCount = 0; // LOAD_OP_LOAD — no clear

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		// ---- Set viewport / scissor ----
		VkViewport vp{};
		vp.x        = 0.0f;
		vp.y        = 0.0f;
		vp.width    = static_cast<float>(sceneExtent.width);
		vp.height   = static_cast<float>(sceneExtent.height);
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &vp);

		VkRect2D scissor{ { 0, 0 }, sceneExtent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// ---- Bind pipeline + descriptor set ----
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        m_pipelineLayout, 0, 1, &dset, 0, nullptr);

		// ---- Push constants (128 bytes) ----
		WaterPushConstants pc{};
		std::memcpy(pc.viewProj, viewProjMat4, sizeof(pc.viewProj));
		pc.cameraPos[0]    = cameraPosWorld3[0];
		pc.cameraPos[1]    = cameraPosWorld3[1];
		pc.cameraPos[2]    = cameraPosWorld3[2];
		pc.cameraPos[3]    = 0.0f;
		pc.waterLevel      = m_params.waterLevel;
		pc.time            = timeSeconds;
		pc.normalTiling    = m_params.normalTiling;
		pc.normalScrollX   = m_params.normalScrollX;
		pc.normalScrollZ   = m_params.normalScrollZ;
		pc.fresnelF0       = m_params.fresnelF0;
		pc.fadeDepthMeters = m_params.fadeDepthMeters;
		// M37.2 additions:
		pc.waveAmplitude   = m_params.waveAmplitude;
		pc.waveFrequency   = m_params.waveFrequency;
		pc.foamThreshold   = m_params.foamThreshold;
		pc.causticsTiling  = m_params.causticsTiling;
		pc.causticsScroll  = m_params.causticsScroll;

		vkCmdPushConstants(cmd, m_pipelineLayout,
		                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0, sizeof(WaterPushConstants), &pc);

		// ---- Bind vertex + index buffers ----
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// ---- Draw water mesh ----
		vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);

		vkCmdEndRenderPass(cmd);

		LOG_TRACE(Render, "[WaterRenderer] Record: drew {} indices at time={:.2f}s (foam={} caustics={})",
		          m_indexCount, timeSeconds,
		          foamTexView != VK_NULL_HANDLE ? "yes" : "no",
		          causticsTexView != VK_NULL_HANDLE ? "yes" : "no");
	}

	// =========================================================================
	// WaterRenderer — private helpers
	// =========================================================================

	bool WaterRenderer::CreateWaterMesh(VkDevice device, VkPhysicalDevice physicalDevice)
	{
		const uint32_t N      = m_params.gridResolution;
		const float    half   = m_params.gridHalfSize;
		const float    step   = (2.0f * half) / static_cast<float>(N);
		const float    uvStep = 1.0f / static_cast<float>(N);
		const float    Y      = m_params.waterLevel;

		/// Vertex layout: position (vec3) + UV (vec2) = 20 bytes.
		struct WaterVertex
		{
			float x, y, z;   ///< World-space position.
			float u, v;       ///< Tiled UV for normal map sampling.
		};
		static_assert(sizeof(WaterVertex) == 20, "WaterVertex stride must be 20 bytes");

		const uint32_t vertCount = (N + 1) * (N + 1);
		const uint32_t idxCount  = N * N * 6u; // 2 triangles per quad, 3 indices each

		std::vector<WaterVertex> vertices;
		vertices.reserve(vertCount);

		std::vector<uint32_t> indices;
		indices.reserve(idxCount);

		for (uint32_t row = 0; row <= N; ++row)
		{
			for (uint32_t col = 0; col <= N; ++col)
			{
				WaterVertex v{};
				v.x = -half + static_cast<float>(col) * step;
				v.y = Y;
				v.z = -half + static_cast<float>(row) * step;
				v.u = static_cast<float>(col) * uvStep;
				v.v = static_cast<float>(row) * uvStep;
				vertices.push_back(v);
			}
		}

		for (uint32_t row = 0; row < N; ++row)
		{
			for (uint32_t col = 0; col < N; ++col)
			{
				const uint32_t i0 = row * (N + 1) + col;
				const uint32_t i1 = i0 + 1;
				const uint32_t i2 = i0 + (N + 1);
				const uint32_t i3 = i2 + 1;
				// CCW winding (Vulkan right-hand + Y-down NDC)
				indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
				indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
			}
		}
		m_indexCount = idxCount;

		const VkDeviceSize vbSize = vertices.size() * sizeof(WaterVertex);
		const VkDeviceSize ibSize = indices.size()  * sizeof(uint32_t);

		if (!CreateDeviceLocalBuffer(device, physicalDevice,
		                             vbSize,
		                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		                             vertices.data(),
		                             m_vertexBuffer, m_vertexMemory))
		{
			LOG_ERROR(Render, "[WaterRenderer] CreateWaterMesh FAILED: vertex buffer");
			return false;
		}

		if (!CreateDeviceLocalBuffer(device, physicalDevice,
		                             ibSize,
		                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		                             indices.data(),
		                             m_indexBuffer, m_indexMemory))
		{
			LOG_ERROR(Render, "[WaterRenderer] CreateWaterMesh FAILED: index buffer");
			return false;
		}

		LOG_INFO(Render, "[WaterRenderer] Water mesh created (verts={} indices={} grid={}x{})",
		         vertCount, idxCount, N, N);
		return true;
	}

	bool WaterRenderer::CreateRenderTarget(VkDevice device,
	                                       VkPhysicalDevice physicalDevice,
	                                       uint32_t width, uint32_t height,
	                                       VkFormat format,
	                                       VkImage& outImage,
	                                       VkImageView& outView,
	                                       VkDeviceMemory& outMemory)
	{
		if (width == 0 || height == 0)
		{
			LOG_WARN(Render, "[WaterRenderer] CreateRenderTarget: zero extent — skipped");
			return false;
		}

		VkImageCreateInfo imgInfo{};
		imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType     = VK_IMAGE_TYPE_2D;
		imgInfo.format        = format;
		imgInfo.extent        = { width, height, 1u };
		imgInfo.mipLevels     = 1;
		imgInfo.arrayLayers   = 1;
		imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
		imgInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		                      | VK_IMAGE_USAGE_SAMPLED_BIT
		                      | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (vkCreateImage(device, &imgInfo, nullptr, &outImage) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreateRenderTarget: vkCreateImage failed ({}x{})", width, height);
			return false;
		}

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device, outImage, &memReq);

		const uint32_t memType = FindMemoryType(physicalDevice, memReq.memoryTypeBits,
		                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (memType == UINT32_MAX)
		{
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			LOG_ERROR(Render, "[WaterRenderer] CreateRenderTarget: no device-local memory");
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memReq.size;
		allocInfo.memoryTypeIndex = memType;

		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
		{
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			LOG_ERROR(Render, "[WaterRenderer] CreateRenderTarget: memory allocation failed");
			return false;
		}
		vkBindImageMemory(device, outImage, outMemory, 0);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image                           = outImage;
		viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format                          = format;
		viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel   = 0;
		viewInfo.subresourceRange.levelCount     = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount     = 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, &outView) != VK_SUCCESS)
		{
			vkFreeMemory(device, outMemory, nullptr);
			outMemory = VK_NULL_HANDLE;
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			LOG_ERROR(Render, "[WaterRenderer] CreateRenderTarget: vkCreateImageView failed");
			return false;
		}

		LOG_INFO(Render, "[WaterRenderer] RenderTarget created ({}x{} format={})", width, height, static_cast<int>(format));
		return true;
	}

	bool WaterRenderer::CreateRenderPass(VkDevice device, VkFormat sceneColorHDRFormat)
	{
		/// Single colour attachment — LOAD_OP_LOAD to composite the water onto the existing SceneColor_HDR.
		VkAttachmentDescription colorAtt{};
		colorAtt.format         = sceneColorHDRFormat;
		colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
		colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;      // preserve existing scene lighting
		colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAtt.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments    = &colorRef;

		VkSubpassDependency dep{};
		dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass    = 0;
		dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments    = &colorAtt;
		rpInfo.subpassCount    = 1;
		rpInfo.pSubpasses      = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dep;

		if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreateRenderPass: vkCreateRenderPass failed");
			return false;
		}

		LOG_INFO(Render, "[WaterRenderer] Render pass created (LOAD_OP_LOAD)");
		return true;
	}

	bool WaterRenderer::CreatePipeline(VkDevice device,
	                                   VkPhysicalDevice /*physicalDevice*/,
	                                   VkFormat /*sceneColorHDRFormat*/,
	                                   const uint32_t* vertSpirv, size_t vertWordCount,
	                                   const uint32_t* fragSpirv, size_t fragWordCount,
	                                   uint32_t maxFrames,
	                                   VkPipelineCache pipelineCache)
	{
		// ---- Samplers ----
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter        = VK_FILTER_LINEAR;
		samplerInfo.minFilter        = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy    = 1.0f;
		samplerInfo.maxLod           = 1.0f;
		samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_linearSampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: linear sampler creation failed");
			return false;
		}

		VkSamplerCreateInfo depthSamplerInfo = samplerInfo;
		depthSamplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		depthSamplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		if (vkCreateSampler(device, &depthSamplerInfo, nullptr, &m_depthSampler) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: depth sampler creation failed");
			return false;
		}

		// ---- Descriptor set layout ----
		// binding 0: reflection (combined image sampler)
		// binding 1: refraction (combined image sampler)
		// binding 2: scene depth (combined image sampler)
		// binding 3: normal map (combined image sampler)
		// binding 4: foam texture (combined image sampler) — M37.2
		// binding 5: caustics texture (combined image sampler) — M37.2
		std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
		for (uint32_t i = 0; i < 6u; ++i)
		{
			bindings[i].binding            = i;
			bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount    = 1;
			bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		VkDescriptorSetLayoutCreateInfo dslInfo{};
		dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		dslInfo.pBindings    = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: descriptor set layout failed");
			return false;
		}

		// ---- Descriptor pool + sets ----
		VkDescriptorPoolSize poolSize{};
		poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 6u * maxFrames; // 6 bindings: refl+refr+depth+normal+foam+caustics

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets       = maxFrames;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes    = &poolSize;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: descriptor pool failed");
			return false;
		}

		std::vector<VkDescriptorSetLayout> layouts(maxFrames, m_descriptorSetLayout);
		VkDescriptorSetAllocateInfo dsetAlloc{};
		dsetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsetAlloc.descriptorPool     = m_descriptorPool;
		dsetAlloc.descriptorSetCount = maxFrames;
		dsetAlloc.pSetLayouts        = layouts.data();

		m_descriptorSets.resize(maxFrames);
		if (vkAllocateDescriptorSets(device, &dsetAlloc, m_descriptorSets.data()) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: descriptor set allocation failed");
			return false;
		}

		// ---- Pipeline layout (descriptor set + push constants) ----
		VkPushConstantRange pcRange{};
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pcRange.offset     = 0;
		pcRange.size       = sizeof(WaterPushConstants);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount         = 1;
		layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges    = &pcRange;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: pipeline layout failed");
			return false;
		}

		// ---- Shader modules ----
		auto makeModule = [&](const uint32_t* spirv, size_t words, VkShaderModule& out) -> bool
		{
			VkShaderModuleCreateInfo smInfo{};
			smInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			smInfo.codeSize = words * 4;
			smInfo.pCode    = spirv;
			return vkCreateShaderModule(device, &smInfo, nullptr, &out) == VK_SUCCESS;
		};

		VkShaderModule vertModule = VK_NULL_HANDLE;
		VkShaderModule fragModule = VK_NULL_HANDLE;

		if (!makeModule(vertSpirv, vertWordCount, vertModule))
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: vertex shader module failed");
			return false;
		}
		if (!makeModule(fragSpirv, fragWordCount, fragModule))
		{
			vkDestroyShaderModule(device, vertModule, nullptr);
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: fragment shader module failed");
			return false;
		}

		// ---- Graphics pipeline ----
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule;
		stages[1].pName  = "main";

		// Vertex input: binding 0 = WaterVertex (position vec3 + UV vec2 = stride 20)
		VkVertexInputBindingDescription vertBinding{};
		vertBinding.binding   = 0;
		vertBinding.stride    = 20; // sizeof(WaterVertex)
		vertBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attribs[2]{};
		attribs[0].location = 0;
		attribs[0].binding  = 0;
		attribs[0].format   = VK_FORMAT_R32G32B32_SFLOAT; // position
		attribs[0].offset   = 0;
		attribs[1].location = 1;
		attribs[1].binding  = 0;
		attribs[1].format   = VK_FORMAT_R32G32_SFLOAT;    // UV
		attribs[1].offset   = 12;

		VkPipelineVertexInputStateCreateInfo viState{};
		viState.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		viState.vertexBindingDescriptionCount   = 1;
		viState.pVertexBindingDescriptions      = &vertBinding;
		viState.vertexAttributeDescriptionCount = 2;
		viState.pVertexAttributeDescriptions    = attribs;

		VkPipelineInputAssemblyStateCreateInfo iaState{};
		iaState.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vpState{};
		vpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vpState.viewportCount = 1;
		vpState.scissorCount  = 1;

		VkPipelineRasterizationStateCreateInfo rsState{};
		rsState.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rsState.polygonMode = VK_POLYGON_MODE_FILL;
		rsState.cullMode    = VK_CULL_MODE_NONE; // water is two-sided (above + below)
		rsState.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rsState.lineWidth   = 1.0f;

		VkPipelineMultisampleStateCreateInfo msState{};
		msState.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Additive blending for the water surface into HDR SceneColor
		VkPipelineColorBlendAttachmentState blendAtt{};
		blendAtt.blendEnable         = VK_TRUE;
		blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
		blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
		blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cbState{};
		cbState.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cbState.attachmentCount = 1;
		cbState.pAttachments    = &blendAtt;

		const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynState{};
		dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynState.dynamicStateCount = 2;
		dynState.pDynamicStates    = dynamics;

		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeInfo.stageCount          = 2;
		pipeInfo.pStages             = stages;
		pipeInfo.pVertexInputState   = &viState;
		pipeInfo.pInputAssemblyState = &iaState;
		pipeInfo.pViewportState      = &vpState;
		pipeInfo.pRasterizationState = &rsState;
		pipeInfo.pMultisampleState   = &msState;
		pipeInfo.pColorBlendState    = &cbState;
		pipeInfo.pDynamicState       = &dynState;
		pipeInfo.layout              = m_pipelineLayout;
		pipeInfo.renderPass          = m_renderPass;
		pipeInfo.subpass             = 0;

		const VkResult pipeResult =
		    vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipeInfo, nullptr, &m_pipeline);

		vkDestroyShaderModule(device, vertModule, nullptr);
		vkDestroyShaderModule(device, fragModule, nullptr);

		if (pipeResult != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterRenderer] CreatePipeline: vkCreateGraphicsPipelines failed ({})",
			          static_cast<int>(pipeResult));
			return false;
		}

		LOG_INFO(Render, "[WaterRenderer] Pipeline created (maxFrames={})", maxFrames);
		return true;
	}

	void WaterRenderer::DestroyRenderTargets(VkDevice device)
	{
		if (m_reflectionView  != VK_NULL_HANDLE) { vkDestroyImageView(device, m_reflectionView,  nullptr); m_reflectionView  = VK_NULL_HANDLE; }
		if (m_reflectionImage != VK_NULL_HANDLE) { vkDestroyImage(device, m_reflectionImage, nullptr);     m_reflectionImage = VK_NULL_HANDLE; }
		if (m_reflectionMemory!= VK_NULL_HANDLE) { vkFreeMemory(device, m_reflectionMemory, nullptr);      m_reflectionMemory= VK_NULL_HANDLE; }

		if (m_refractionView  != VK_NULL_HANDLE) { vkDestroyImageView(device, m_refractionView,  nullptr); m_refractionView  = VK_NULL_HANDLE; }
		if (m_refractionImage != VK_NULL_HANDLE) { vkDestroyImage(device, m_refractionImage, nullptr);     m_refractionImage = VK_NULL_HANDLE; }
		if (m_refractionMemory!= VK_NULL_HANDLE) { vkFreeMemory(device, m_refractionMemory, nullptr);      m_refractionMemory= VK_NULL_HANDLE; }
	}

	uint32_t WaterRenderer::FindMemoryType(VkPhysicalDevice physicalDevice,
	                                       uint32_t typeFilter,
	                                       VkMemoryPropertyFlags properties)
	{
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) &&
			    (memProps.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		return UINT32_MAX;
	}

} // namespace engine::render
