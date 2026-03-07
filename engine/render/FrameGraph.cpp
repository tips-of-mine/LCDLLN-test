#include "engine/render/FrameGraph.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>

namespace engine::render
{
	// --- Registry ---

	void Registry::bindImage(ResourceId id, VkImage image, VkImageView view)
	{
		if (id == kInvalidResourceId) return;
		m_images[id] = { image, view };
	}

	void Registry::bindBuffer(ResourceId id, VkBuffer buffer)
	{
		if (id == kInvalidResourceId) return;
		m_buffers[id] = buffer;
	}

	VkImage Registry::getImage(ResourceId id) const
	{
		auto it = m_images.find(id);
		return it != m_images.end() ? it->second.image : VK_NULL_HANDLE;
	}

	VkImageView Registry::getImageView(ResourceId id) const
	{
		auto it = m_images.find(id);
		return it != m_images.end() ? it->second.view : VK_NULL_HANDLE;
	}

	VkBuffer Registry::getBuffer(ResourceId id) const
	{
		auto it = m_buffers.find(id);
		return it != m_buffers.end() ? it->second : VK_NULL_HANDLE;
	}

	void Registry::clear()
	{
		m_images.clear();
		m_buffers.clear();
	}

	// --- ImageUsage -> layout/stage/access ---

	ResourceUsageState GetUsageState(ImageUsage usage)
	{
		ResourceUsageState s{};
		switch (usage)
		{
		case ImageUsage::ColorWrite:
			s.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			s.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			s.accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case ImageUsage::DepthWrite:
			s.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			s.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			s.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case ImageUsage::SampledRead:
			s.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			s.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			s.accessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case ImageUsage::TransferSrc:
			s.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			s.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			s.accessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case ImageUsage::TransferDst:
			s.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			s.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			s.accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		}
		return s;
	}

	// --- PassBuilder ---

	PassBuilder& PassBuilder::read(ResourceId id, ImageUsage usage)
	{
		if (id != kInvalidResourceId)
		{
			m_reads.emplace_back(id, usage);
		}
		return *this;
	}

	PassBuilder& PassBuilder::write(ResourceId id, ImageUsage usage)
	{
		if (id != kInvalidResourceId)
		{
			m_writes.emplace_back(id, usage);
		}
		return *this;
	}

	// --- FrameGraph (helpers) ---

	namespace
	{
		uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
		{
			VkPhysicalDeviceMemoryProperties memProps;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((typeFilter & (1u << i)) != 0
					&& (memProps.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}
			return UINT32_MAX;
		}
	}

	// --- FrameGraph ---

	ResourceId FrameGraph::createImage(std::string_view name, const ImageDesc& desc)
	{
		ResourceId id = m_nextId++;
		ImageResource r;
		r.id = id;
		r.name = std::string(name);
		r.desc = desc;
		r.external = false;
		m_imageResources.push_back(std::move(r));
		m_perFrameImageHandles.emplace_back();
		return id;
	}

	ResourceId FrameGraph::createBuffer(std::string_view name, const BufferDesc& desc)
	{
		ResourceId id = m_nextId++;
		BufferResource r;
		r.id = id;
		r.name = std::string(name);
		r.desc = desc;
		r.external = false;
		m_bufferResources.push_back(std::move(r));
		m_perFrameBufferHandles.emplace_back();
		return id;
	}

	ResourceId FrameGraph::createExternalImage(std::string_view name)
	{
		ResourceId id = m_nextId++;
		ImageResource r;
		r.id = id;
		r.name = std::string(name);
		r.external = true;
		m_imageResources.push_back(std::move(r));
		m_perFrameImageHandles.emplace_back();
		return id;
	}

	void FrameGraph::addPass(std::string_view name, PassSetupFn setup, PassExecuteFn execute)
	{
		PassBuilder builder;
		if (setup)
		{
			setup(builder);
		}
		Pass pass;
		pass.name = std::string(name);
		pass.reads = builder.getReads();
		pass.writes = builder.getWrites();
		pass.setup = std::move(setup);
		pass.execute = std::move(execute);
		m_passes.push_back(std::move(pass));
		m_compiled = false;
	}

	void FrameGraph::compile()
	{
		m_compiledOrder.clear();
		const size_t n = m_passes.size();
		if (n == 0) { m_compiled = true; return; }

		// Per resource: single writer pass index (MVP: multi-writer forbidden).
		std::unordered_map<ResourceId, size_t> lastWriter;
		for (size_t i = 0; i < n; ++i)
		{
			for (const auto& p : m_passes[i].writes)
			{
				ResourceId rid = p.first;
				if (rid == kInvalidResourceId) continue;
				auto it = lastWriter.find(rid);
				if (it != lastWriter.end())
				{
					LOG_FATAL(Render, "FrameGraph: multi-writer on resource {} (passes '{}' and '{}'); MVP forbids multi-writer",
						rid, m_passes[it->second].name, m_passes[i].name);
				}
				lastWriter[rid] = i;
			}
		}

		// Edges: writer -> reader (writer must run before reader). Adjacency: successors[i] = passes that must run after i.
		std::vector<std::vector<size_t>> successors(n);
		for (size_t readerIdx = 0; readerIdx < n; ++readerIdx)
		{
			for (const auto& p : m_passes[readerIdx].reads)
			{
				ResourceId rid = p.first;
				if (rid == kInvalidResourceId) continue;
				auto it = lastWriter.find(rid);
				if (it == lastWriter.end()) continue;
				size_t writerIdx = it->second;
				if (writerIdx != readerIdx)
				{
					successors[writerIdx].push_back(readerIdx);
				}
			}
		}

		// Kahn: in-degree count.
		std::vector<size_t> inDegree(n, 0);
		for (size_t i = 0; i < n; ++i)
		{
			for (size_t j : successors[i])
			{
				++inDegree[j];
			}
		}
		std::vector<size_t> queue;
		queue.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			if (inDegree[i] == 0) queue.push_back(i);
		}
		m_compiledOrder.reserve(n);
		for (size_t q = 0; q < queue.size(); ++q)
		{
			size_t u = queue[q];
			m_compiledOrder.push_back(u);
			for (size_t v : successors[u])
			{
				--inDegree[v];
				if (inDegree[v] == 0) queue.push_back(v);
			}
		}
		if (m_compiledOrder.size() != n)
		{
			LOG_FATAL(Render, "FrameGraph: cycle detected in pass dependencies (topological sort produced {} of {} passes)", m_compiledOrder.size(), n);
		}
		m_compiled = true;
	}

	void FrameGraph::emitBarriersBeforePass(VkCommandBuffer cmd, const Pass& pass, Registry& registry,
		std::unordered_map<ResourceId, ResourceUsageState>& lastUsage)
	{
		struct PendingBarrier
		{
			VkImageMemoryBarrier barrier;
			VkPipelineStageFlags srcStage;
			VkPipelineStageFlags dstStage;
		};
		std::vector<PendingBarrier> pending;
		std::unordered_map<ResourceId, ResourceUsageState> updates;

		auto getAspect = [this](ResourceId rid) {
			for (const auto& res : m_imageResources)
				if (res.id == rid) return res.desc.isDepthAttachment ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
			return VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
		};
		auto emitFor = [&](ResourceId rid, ImageUsage usage) {
			VkImage image = registry.getImage(rid);
			if (image == VK_NULL_HANDLE) return;
			ResourceUsageState cur = lastUsage[rid];
			ResourceUsageState target = GetUsageState(usage);
			if (cur.layout == target.layout && cur.accessMask == target.accessMask)
			{
				updates[rid] = target;
				return;
			}
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcAccessMask = cur.accessMask;
			barrier.dstAccessMask = target.accessMask;
			barrier.oldLayout = cur.layout;
			barrier.newLayout = target.layout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = getAspect(rid);
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			VkPipelineStageFlags srcStage = (cur.layout == VK_IMAGE_LAYOUT_UNDEFINED)
				? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : cur.stageMask;
			pending.push_back({ barrier, srcStage, target.stageMask });
			updates[rid] = target;
		};

		for (const auto& p : pass.reads)
			emitFor(p.first, p.second);
		for (const auto& p : pass.writes)
			emitFor(p.first, p.second);

		if (!pending.empty())
		{
			VkPipelineStageFlags srcStage = 0;
			VkPipelineStageFlags dstStage = 0;
			std::vector<VkImageMemoryBarrier> barriers;
			barriers.reserve(pending.size());
			for (auto& pb : pending)
			{
				srcStage |= pb.srcStage;
				dstStage |= pb.dstStage;
				barriers.push_back(pb.barrier);
			}
			vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr,
				static_cast<uint32_t>(barriers.size()), barriers.data());
		}
		for (const auto& u : updates)
			lastUsage[u.first] = u.second;
	}

	void FrameGraph::execute(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandBuffer cmd,
		Registry& registry, uint32_t frameIndex, VkExtent2D extent, uint32_t framesInFlight)
	{
		if (!m_compiled)
		{
			compile();
		}
		ensureImageResources(device, physicalDevice, frameIndex, extent, framesInFlight);
		ensureBufferResources(device, physicalDevice, frameIndex, framesInFlight);
		fillRegistry(registry, frameIndex);

		std::unordered_map<ResourceId, ResourceUsageState> lastUsage;
		for (size_t passIdx : m_compiledOrder)
		{
			const Pass& pass = m_passes[passIdx];
			emitBarriersBeforePass(cmd, pass, registry, lastUsage);
			if (pass.execute)
			{
				pass.execute(cmd, registry);
			}
		}
	}

	void FrameGraph::destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE) return;

		for (auto& perFrame : m_perFrameImageHandles)
		{
			for (PerFrameImageHandles& h : perFrame)
			{
				if (h.view != VK_NULL_HANDLE)
				{
					vkDestroyImageView(device, h.view, nullptr);
					h.view = VK_NULL_HANDLE;
				}
				if (h.image != VK_NULL_HANDLE)
				{
					vkDestroyImage(device, h.image, nullptr);
					h.image = VK_NULL_HANDLE;
				}
				if (h.memory != VK_NULL_HANDLE)
				{
					vkFreeMemory(device, h.memory, nullptr);
					h.memory = VK_NULL_HANDLE;
				}
			}
		}
		for (auto& perFrame : m_perFrameBufferHandles)
		{
			for (PerFrameBufferHandles& h : perFrame)
			{
				if (h.buffer != VK_NULL_HANDLE)
				{
					vkDestroyBuffer(device, h.buffer, nullptr);
					h.buffer = VK_NULL_HANDLE;
				}
				if (h.memory != VK_NULL_HANDLE)
				{
					vkFreeMemory(device, h.memory, nullptr);
					h.memory = VK_NULL_HANDLE;
				}
			}
		}
		m_lastExtent = { 0, 0 };
		m_lastFramesInFlight = 0;
	}

	void FrameGraph::ensureImageResources(VkDevice device, VkPhysicalDevice physicalDevice,
		uint32_t frameIndex, VkExtent2D extent, uint32_t framesInFlight)
	{
		const bool extentChanged = m_lastExtent.width != extent.width || m_lastExtent.height != extent.height;
		const bool framesChanged = m_lastFramesInFlight != framesInFlight;
		if (extentChanged || framesChanged)
		{
			destroy(device);
			m_lastExtent = extent;
			m_lastFramesInFlight = framesInFlight;
			for (auto& perFrame : m_perFrameImageHandles)
			{
				perFrame.resize(framesInFlight);
			}
			for (auto& perFrame : m_perFrameBufferHandles)
			{
				perFrame.resize(framesInFlight);
			}
		}

		for (size_t resIdx = 0; resIdx < m_imageResources.size(); ++resIdx)
		{
			const ImageResource& res = m_imageResources[resIdx];
			if (res.external) continue;
			uint32_t width = extent.width;
			uint32_t height = extent.height;
			if (res.desc.extentScalePower > 0)
			{
				width = extent.width >> res.desc.extentScalePower;
				height = extent.height >> res.desc.extentScalePower;
				if (width < 1) width = 1;
				if (height < 1) height = 1;
			}
			else if (res.desc.width != 0 || res.desc.height != 0)
			{
				width = res.desc.width != 0 ? res.desc.width : extent.width;
				height = res.desc.height != 0 ? res.desc.height : extent.height;
			}

			std::vector<PerFrameImageHandles>& perFrame = m_perFrameImageHandles[resIdx];
			if (frameIndex >= perFrame.size()) perFrame.resize(frameIndex + 1);
			PerFrameImageHandles& h = perFrame[frameIndex];

			if (h.image != VK_NULL_HANDLE) continue;

			VkImageUsageFlags usage = res.desc.usage;
			if (res.desc.isDepthAttachment)
				usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = res.desc.format;
			imageInfo.extent.width = width;
			imageInfo.extent.height = height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = res.desc.layers;
			imageInfo.samples = res.desc.samples;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = usage;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VkResult result = vkCreateImage(device, &imageInfo, nullptr, &h.image);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vkCreateImage failed for '{}': {}", res.name, static_cast<int>(result));
				continue;
			}

			VkMemoryRequirements memReq;
			vkGetImageMemoryRequirements(device, h.image, &memReq);
			uint32_t memTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memTypeIndex == UINT32_MAX)
			{
				LOG_ERROR(Render, "FrameGraph: no suitable memory type for image '{}'", res.name);
				vkDestroyImage(device, h.image, nullptr);
				h.image = VK_NULL_HANDLE;
				continue;
			}

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = memTypeIndex;
			result = vkAllocateMemory(device, &allocInfo, nullptr, &h.memory);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vkAllocateMemory failed for image '{}': {}", res.name, static_cast<int>(result));
				vkDestroyImage(device, h.image, nullptr);
				h.image = VK_NULL_HANDLE;
				continue;
			}
			vkBindImageMemory(device, h.image, h.memory, 0);

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = h.image;
			viewInfo.viewType = res.desc.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = res.desc.format;
			viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.subresourceRange.aspectMask = res.desc.isDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = res.desc.layers;

			result = vkCreateImageView(device, &viewInfo, nullptr, &h.view);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vkCreateImageView failed for '{}': {}", res.name, static_cast<int>(result));
				vkDestroyImage(device, h.image, nullptr);
				vkFreeMemory(device, h.memory, nullptr);
				h.image = VK_NULL_HANDLE;
				h.memory = VK_NULL_HANDLE;
			}
		}
	}

	void FrameGraph::ensureBufferResources(VkDevice device, VkPhysicalDevice physicalDevice,
		uint32_t frameIndex, uint32_t framesInFlight)
	{
		const bool framesChanged = m_lastFramesInFlight != framesInFlight;
		if (framesChanged && m_lastFramesInFlight != 0)
		{
			for (auto& perFrame : m_perFrameBufferHandles)
			{
				for (PerFrameBufferHandles& h : perFrame)
				{
					if (h.buffer != VK_NULL_HANDLE)
					{
						vkDestroyBuffer(device, h.buffer, nullptr);
						h.buffer = VK_NULL_HANDLE;
					}
					if (h.memory != VK_NULL_HANDLE)
					{
						vkFreeMemory(device, h.memory, nullptr);
						h.memory = VK_NULL_HANDLE;
					}
				}
			}
		}

		for (size_t resIdx = 0; resIdx < m_bufferResources.size(); ++resIdx)
		{
			const BufferResource& res = m_bufferResources[resIdx];
			if (res.external) continue;
			if (res.desc.size == 0) continue;

			std::vector<PerFrameBufferHandles>& perFrame = m_perFrameBufferHandles[resIdx];
			if (frameIndex >= perFrame.size()) perFrame.resize(frameIndex + 1);
			PerFrameBufferHandles& h = perFrame[frameIndex];

			if (h.buffer != VK_NULL_HANDLE) continue;

			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = res.desc.size;
			bufInfo.usage = res.desc.usage;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkResult result = vkCreateBuffer(device, &bufInfo, nullptr, &h.buffer);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vkCreateBuffer failed for '{}': {}", res.name, static_cast<int>(result));
				continue;
			}

			VkMemoryRequirements memReq;
			vkGetBufferMemoryRequirements(device, h.buffer, &memReq);
			uint32_t memTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memTypeIndex == UINT32_MAX)
			{
				LOG_ERROR(Render, "FrameGraph: no suitable memory type for buffer '{}'", res.name);
				vkDestroyBuffer(device, h.buffer, nullptr);
				h.buffer = VK_NULL_HANDLE;
				continue;
			}

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = memTypeIndex;
			result = vkAllocateMemory(device, &allocInfo, nullptr, &h.memory);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vkAllocateMemory failed for buffer '{}': {}", res.name, static_cast<int>(result));
				vkDestroyBuffer(device, h.buffer, nullptr);
				h.buffer = VK_NULL_HANDLE;
				continue;
			}
			vkBindBufferMemory(device, h.buffer, h.memory, 0);
		}
	}

	void FrameGraph::fillRegistry(Registry& registry, uint32_t frameIndex)
	{
		for (size_t i = 0; i < m_imageResources.size(); ++i)
		{
			const ImageResource& res = m_imageResources[i];
			if (res.external) continue;
			if (frameIndex >= m_perFrameImageHandles[i].size()) continue;
			const PerFrameImageHandles& h = m_perFrameImageHandles[i][frameIndex];
			if (h.image != VK_NULL_HANDLE && h.view != VK_NULL_HANDLE)
			{
				registry.bindImage(res.id, h.image, h.view);
			}
		}
		for (size_t i = 0; i < m_bufferResources.size(); ++i)
		{
			const BufferResource& res = m_bufferResources[i];
			if (frameIndex >= m_perFrameBufferHandles[i].size()) continue;
			const PerFrameBufferHandles& h = m_perFrameBufferHandles[i][frameIndex];
			if (h.buffer != VK_NULL_HANDLE)
			{
				registry.bindBuffer(res.id, h.buffer);
			}
		}
	}
}
