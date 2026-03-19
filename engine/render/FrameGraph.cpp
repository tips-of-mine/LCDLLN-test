#include "engine/render/FrameGraph.h"
#include "engine/core/Log.h"
#include "engine/core/Profiler.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstdio>

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

	// --- ImageUsage -> layout/stage/access (sync2 mapping) ---

	ResourceUsageState GetUsageState(ImageUsage usage)
	{
		ResourceUsageState s{};
		switch (usage)
		{
		case ImageUsage::ColorWrite:
			s.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			s.stageMask2 = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			s.accessMask2 = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case ImageUsage::DepthWrite:
			s.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			s.stageMask2 = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			s.accessMask2 = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case ImageUsage::SampledRead:
			s.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			s.stageMask2 = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			s.accessMask2 = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			break;
		case ImageUsage::TransferSrc:
			s.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			s.stageMask2 = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			s.accessMask2 = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		case ImageUsage::TransferDst:
			s.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			s.stageMask2 = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			s.accessMask2 = VK_ACCESS_2_TRANSFER_WRITE_BIT;
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
		LOG_DEBUG(Render, "[FG-COMPILE] debut passes={}", m_passes.size());
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
					//LOG_FATAL(Render, "FrameGraph: multi-writer on resource {} (passes '{}' and '{}'); MVP forbids multi-writer",	rid, m_passes[it->second].name, m_passes[i].name);
					LOG_ERROR(Render, "FrameGraph: multi-writer on resource {} (passes '{}' and '{}'); MVP forbids multi-writer", rid, m_passes[it->second].name, m_passes[i].name);
				}
				lastWriter[rid] = i;
			}
		}
		LOG_INFO(Render, "[FG-COMPILE] topo sort");
		
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
			LOG_ERROR(Render, "FrameGraph: cycle detected in pass dependencies (topological sort produced {} of {} passes)", m_compiledOrder.size(), n);
		}
		LOG_INFO(Render, "[FG-COMPILE] done order={}", m_compiledOrder.size());
		m_compiled = true;
	}

	namespace
	{
		/// Converts sync2 stage flags to legacy VkPipelineStageFlags for fallback barrier.
		VkPipelineStageFlags ToLegacyStage(VkPipelineStageFlags2 stage2)
		{
			VkPipelineStageFlags out = 0;
			if (stage2 & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT) out |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			if (stage2 & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) out |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if (stage2 & VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT) out |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			if (stage2 & VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) out |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			if (stage2 & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) out |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			if (stage2 & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) out |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			if (stage2 & VK_PIPELINE_STAGE_2_TRANSFER_BIT) out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			return out;
		}
		/// Converts sync2 access flags to legacy VkAccessFlags for fallback barrier.
		VkAccessFlags ToLegacyAccess(VkAccessFlags2 access2)
		{
			VkAccessFlags out = 0;
			if (access2 & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) out |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			if (access2 & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) out |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			if (access2 & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) out |= VK_ACCESS_SHADER_READ_BIT;
			if (access2 & VK_ACCESS_2_TRANSFER_READ_BIT) out |= VK_ACCESS_TRANSFER_READ_BIT;
			if (access2 & VK_ACCESS_2_TRANSFER_WRITE_BIT) out |= VK_ACCESS_TRANSFER_WRITE_BIT;
			return out;
		}
	}

	void FrameGraph::emitBarriersBeforePass(VkCommandBuffer cmd, const Pass& pass, Registry& registry,
		std::unordered_map<ResourceId, ResourceUsageState>& lastUsage, bool sync2Supported, VkDevice device)
	{
		std::unordered_map<ResourceId, ResourceUsageState> updates;

		auto getAspect = [this](ResourceId rid) {
			for (const auto& res : m_imageResources)
				if (res.id == rid) return res.desc.isDepthAttachment ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
			return VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
		};
		auto getImageDesc = [this](ResourceId rid) -> const ImageDesc* {
			for (const auto& res : m_imageResources)
				if (res.id == rid) return &res.desc;
			return nullptr;
		};

		struct PendingSync2
		{
			VkImageMemoryBarrier2 bar2;
		};
		struct PendingLegacy
		{
			VkImageMemoryBarrier barrier;
			VkPipelineStageFlags srcStage;
			VkPipelineStageFlags dstStage;
		};
		std::vector<PendingSync2> pendingSync2;
		std::vector<PendingLegacy> pendingLegacy;

		auto emitFor = [&](ResourceId rid, ImageUsage usage) {
			VkImage image = registry.getImage(rid);
			if (image == VK_NULL_HANDLE) return;
			ResourceUsageState cur = lastUsage[rid];
			ResourceUsageState target = GetUsageState(usage);
			if (cur.layout == target.layout && cur.accessMask2 == target.accessMask2)
			{
				updates[rid] = target;
				return;
			}
			const ImageDesc* desc = getImageDesc(rid);
			const uint32_t levelCount = desc ? desc->mipLevels : 1u;
			const uint32_t layerCount = desc ? desc->layers : 1u;
			VkImageAspectFlags aspect = getAspect(rid);

			VkPipelineStageFlags2 srcStage2 = (cur.layout == VK_IMAGE_LAYOUT_UNDEFINED)
				? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : cur.stageMask2;

			if (sync2Supported)
			{
				VkImageMemoryBarrier2 bar2{};
#if defined(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2)
				bar2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
#else
				bar2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
#endif
				bar2.srcStageMask = srcStage2;
				bar2.srcAccessMask = cur.accessMask2;
				bar2.dstStageMask = target.stageMask2;
				bar2.dstAccessMask = target.accessMask2;
				bar2.oldLayout = cur.layout;
				bar2.newLayout = target.layout;
				bar2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bar2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bar2.image = image;
				bar2.subresourceRange.aspectMask = aspect;
				bar2.subresourceRange.baseMipLevel = 0;
				bar2.subresourceRange.levelCount = levelCount;
				bar2.subresourceRange.baseArrayLayer = 0;
				bar2.subresourceRange.layerCount = layerCount;
				pendingSync2.push_back({ bar2 });
			}
			else
			{
				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.srcAccessMask = ToLegacyAccess(cur.accessMask2);
				barrier.dstAccessMask = ToLegacyAccess(target.accessMask2);
				barrier.oldLayout = cur.layout;
				barrier.newLayout = target.layout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = image;
				barrier.subresourceRange.aspectMask = aspect;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = levelCount;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = layerCount;
				pendingLegacy.push_back({ barrier, ToLegacyStage(srcStage2), ToLegacyStage(target.stageMask2) });
			}
			updates[rid] = target;
		};

		for (const auto& p : pass.reads)
			emitFor(p.first, p.second);
		for (const auto& p : pass.writes)
			emitFor(p.first, p.second);

		if (sync2Supported && !pendingSync2.empty())
		{
			using PFN_barrier2 = void(VKAPI_PTR*)(VkCommandBuffer, const VkDependencyInfo*);
			PFN_barrier2 pfnBarrier2 = reinterpret_cast<PFN_barrier2>(
				vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2"));
			if (!pfnBarrier2)
			{
				pfnBarrier2 = reinterpret_cast<PFN_barrier2>(
					vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
			}
			if (pfnBarrier2)
			{
				std::vector<VkImageMemoryBarrier2> bars;
				bars.reserve(pendingSync2.size());
				for (auto& p : pendingSync2)
					bars.push_back(p.bar2);
				VkDependencyInfo dep{};
#if defined(VK_STRUCTURE_TYPE_DEPENDENCY_INFO)
				dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
#else
				dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
#endif
				dep.imageMemoryBarrierCount = static_cast<uint32_t>(bars.size());
				dep.pImageMemoryBarriers = bars.data();
				pfnBarrier2(cmd, &dep);
			}
			else
			{
				for (auto& p : pendingSync2)
				{
					VkImageMemoryBarrier leg{};
					leg.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					leg.srcAccessMask = ToLegacyAccess(p.bar2.srcAccessMask);
					leg.dstAccessMask = ToLegacyAccess(p.bar2.dstAccessMask);
					leg.oldLayout = p.bar2.oldLayout;
					leg.newLayout = p.bar2.newLayout;
					leg.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					leg.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					leg.image = p.bar2.image;
					leg.subresourceRange = p.bar2.subresourceRange;
					VkPipelineStageFlags src = ToLegacyStage(p.bar2.srcStageMask);
					VkPipelineStageFlags dst = ToLegacyStage(p.bar2.dstStageMask);
					vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &leg);
				}
			}
		}
		else if (!sync2Supported && !pendingLegacy.empty())
		{
			VkPipelineStageFlags srcStage = 0;
			VkPipelineStageFlags dstStage = 0;
			std::vector<VkImageMemoryBarrier> barriers;
			barriers.reserve(pendingLegacy.size());
			for (auto& pb : pendingLegacy)
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

	void FrameGraph::execute(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator, VkCommandBuffer cmd,
	    Registry& registry, uint32_t frameIndex, VkExtent2D extent, uint32_t framesInFlight, bool sync2Supported,
		engine::core::Profiler* profiler)
	{
	    engine::core::ProfilerScope scope("FrameGraph::execute");
	    LOG_DEBUG(Render, "[FG] execute debut");
	    if (!m_compiled) { compile(); }
	    LOG_DEBUG(Render, "[FG] ensureImageResources");
	    ensureImageResources(device, physicalDevice, vmaAllocator, frameIndex, extent, framesInFlight);
	    LOG_DEBUG(Render, "[FG] ensureBufferResources");
	    ensureBufferResources(device, vmaAllocator, frameIndex, framesInFlight);
	    LOG_DEBUG(Render, "[FG] fillRegistry");
	    fillRegistry(registry, frameIndex);
	
	    std::unordered_map<ResourceId, ResourceUsageState> lastUsage;
	    LOG_DEBUG(Render, "[FG] loop passes={}", m_compiledOrder.size());
	    for (size_t passIdx : m_compiledOrder)
	    {
	        const Pass& pass = m_passes[passIdx];
	        engine::core::ProfilerScope passScope(pass.name);
	        LOG_DEBUG(Render, "[FG] pass[{}]='{}' barriers", passIdx, pass.name.c_str());
	        emitBarriersBeforePass(cmd, pass, registry, lastUsage, sync2Supported, device);
	        LOG_DEBUG(Render, "[FG] pass[{}]='{}' execute", passIdx, pass.name.c_str());
	        const bool gpuPassRecorded = profiler != nullptr && profiler->BeginGpuPass(cmd, frameIndex, pass.name);
	        if (pass.execute)
	            pass.execute(cmd, registry);
	        if (gpuPassRecorded)
	        {
	            profiler->EndGpuPass(cmd, frameIndex);
	        }
	        LOG_INFO(Render, "[FG] pass[{}]='{}' done", passIdx, pass.name.c_str());
	    }
	    LOG_INFO(Render, "[FG] execute done");
	}

	void FrameGraph::destroy(VkDevice device, void* vmaAllocator)
	{
		if (device == VK_NULL_HANDLE)
			return;
		// vmaAllocator peut être nullptr quand on utilise un bypass "raw Vulkan" pour les images.
		// Dans ce cas on ne peut pas appeler vmaDestroy* (buffers/images VMA), mais on doit quand même
		// pouvoir libérer les images allouées "raw" afin que le resize suive le nouvel extent.
		VmaAllocator alloc = (vmaAllocator != nullptr) ? static_cast<VmaAllocator>(vmaAllocator) : VK_NULL_HANDLE;

		for (auto& perFrame : m_perFrameImageHandles)
		{
			for (PerFrameImageHandles& h : perFrame)
			{
				if (h.view != VK_NULL_HANDLE)
				{
					vkDestroyImageView(device, h.view, nullptr);
					h.view = VK_NULL_HANDLE;
				}
				if (h.image != VK_NULL_HANDLE && h.allocation != nullptr)
				{
					if (h.allocatedWithVma)
					{
						// Si alloc==VK_NULL_HANDLE, on ne détruit pas (le handle sera recréé en raw),
						// pour éviter un crash côté VMA.
						if (alloc != VK_NULL_HANDLE)
							vmaDestroyImage(alloc, h.image, static_cast<VmaAllocation>(h.allocation));
					}
					else
					{
						VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(h.allocation);
						vkDestroyImage(device, h.image, nullptr);
						vkFreeMemory(device, mem, nullptr);
					}
					h.image = VK_NULL_HANDLE;
					h.allocation = nullptr;
					h.allocatedWithVma = false;
				}
			}
		}
		// Buffers : le FrameGraph ne possède pas de bypass raw Vulkan ici (les buffers sont gérés via VMA).
		// Donc si VMA n'est pas disponible, on skip la destruction des buffers pour ne pas invalider
		// des allocations potentiellement actives.
		if (alloc != VK_NULL_HANDLE)
		{
			for (auto& perFrame : m_perFrameBufferHandles)
			{
				for (PerFrameBufferHandles& h : perFrame)
				{
					if (h.buffer != VK_NULL_HANDLE && h.allocation != nullptr)
					{
						vmaDestroyBuffer(alloc, h.buffer, static_cast<VmaAllocation>(h.allocation));
						h.buffer = VK_NULL_HANDLE;
						h.allocation = nullptr;
					}
				}
			}
		}
		m_lastExtent = { 0, 0 };
		m_lastFramesInFlight = 0;
		m_loggedImageVmaBypass = false;
	}

	void FrameGraph::ensureImageResources(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator,
    	uint32_t frameIndex, VkExtent2D extent, uint32_t framesInFlight)
	{
		// vmaAllocator peut être nullptr si on n'a pas réussi à initialiser VMA.
		// Le bypass "raw Vulkan" est justement là pour continuer les alloc/recreate d'images.
		VmaAllocator alloc = (vmaAllocator != nullptr) ? static_cast<VmaAllocator>(vmaAllocator) : VK_NULL_HANDLE;

LOG_DEBUG(Render, "[EIR] debut nImages={} extent={}x{} framesInFlight={} lastExtent={}x{}", m_imageResources.size(), extent.width, extent.height, framesInFlight, m_lastExtent.width, m_lastExtent.height);
		
		const bool extentChanged = m_lastExtent.width != extent.width || m_lastExtent.height != extent.height;
		const bool framesChanged = m_lastFramesInFlight != framesInFlight;
		if (extentChanged || framesChanged)
		{
			{
				char msg[256]{};
				std::snprintf(msg, sizeof(msg),
					"FG ensureImageResources extentChanged=%d framesChanged=%d extent=%ux%u lastExtent=%ux%u vmaAllocatorNull=%d",
					extentChanged ? 1 : 0,
					framesChanged ? 1 : 0,
					extent.width, extent.height,
					m_lastExtent.width, m_lastExtent.height,
					(vmaAllocator == nullptr) ? 1 : 0);
				::engine::core::Log::WriteLine(::engine::core::LogLevel::Warn, "Render", msg);
			}

			destroy(device, vmaAllocator);
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

		if (m_perFrameImageHandles.size() != m_imageResources.size())
		{
LOG_DEBUG(Render, "[EIR] FATAL size mismatch m_perFrameImageHandles={} m_imageResources={}", m_perFrameImageHandles.size(), m_imageResources.size());
			return;
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
			if (width < 1) width = 1;
			if (height < 1) height = 1;

			std::vector<PerFrameImageHandles>& perFrame = m_perFrameImageHandles[resIdx];
			if (frameIndex >= perFrame.size()) perFrame.resize(frameIndex + 1);
			PerFrameImageHandles& h = perFrame[frameIndex];

			if (h.image != VK_NULL_HANDLE) continue;

LOG_DEBUG(Render, "[EIR] image[{}]='{}' fmt={} w={} h={} mips={} layers={}", resIdx, res.name.c_str(), (int)res.desc.format, width, height, res.desc.mipLevels, res.desc.layers);

			// Trace log ciblée pour confirmer que le TAA history suit bien le resize.
			if (res.name == "HistoryA" || res.name == "HistoryB")
			{
				char msg[160]{};
				std::snprintf(msg, sizeof(msg),
					"FG alloc %s w=%u h=%u (frameIndex=%u)",
					res.name.c_str(),
					width, height,
					frameIndex);
				::engine::core::Log::WriteLine(::engine::core::LogLevel::Info, "Render", msg);
			}
			
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
			//imageInfo.mipLevels = res.desc.mipLevels;
			imageInfo.mipLevels  = res.desc.mipLevels > 0  ? res.desc.mipLevels : 1u;
			//imageInfo.arrayLayers = res.desc.layers;
			imageInfo.arrayLayers = res.desc.layers > 0     ? res.desc.layers    : 1u;
			//imageInfo.samples = res.desc.samples;
			imageInfo.samples    = res.desc.samples != 0    ? res.desc.samples   : VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = usage;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			// [STAB.7] Skip vmaCreateImage for FrameGraph images: it causes SEH 0xC0000005 on Windows Release
			// (e.g. ABI/CRT mismatch or invalid allocator context). Use raw Vulkan allocation path only.
			(void)alloc;
			if (!m_loggedImageVmaBypass)
			{
				LOG_WARN(Render, "[FrameGraph] Using raw Vulkan allocation for images (STAB.7, VMA skipped)");
				m_loggedImageVmaBypass = true;
			}
			h.image = VK_NULL_HANDLE;
			h.allocation = nullptr;
			h.allocatedWithVma = false;

			// [STAB.7] VMA BYPASS — Raw Vulkan allocation
				// Raison : vmaCreateImage échoue sur cette configuration (SDK/MSVC/VMA version mismatch ?).
				// Toutes les approches VMA testées (static/dynamic/manual function pointers) ont échoué.
				// Ce bypass utilise vkAllocateMemory + vkBindImageMemory directement.
				// TODO STAB.7 : Investiguer la cause racine et restaurer VMA si possible.
				VkImage newImage = VK_NULL_HANDLE;
				VkResult r1 = vkCreateImage(device, &imageInfo, nullptr, &newImage);
				LOG_DEBUG(Render, "[EIR] step1 vkCreateImage r1={} img={}", (int)r1, (void*)newImage);

				VkMemoryRequirements memReq{};
				vkGetImageMemoryRequirements(device, newImage, &memReq);
LOG_DEBUG(Render, "[EIR] step2 memReq size={} align={} bits=0x{}", (unsigned long long)memReq.size, (unsigned long long)memReq.alignment, memReq.memoryTypeBits);

				VkPhysicalDeviceMemoryProperties memProps{};
				vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
				uint32_t memTypeIndex = UINT32_MAX;
				for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
				{
					if ((memReq.memoryTypeBits & (1u << i)) &&
						(memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
					{
						memTypeIndex = i;
						break;
					}
				}
				LOG_DEBUG(Render, "[EIR] step3 memTypeIndex={}", memTypeIndex);

				VkMemoryAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				allocInfo.allocationSize = memReq.size;
				allocInfo.memoryTypeIndex = memTypeIndex;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkResult r2 = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
				LOG_INFO(Render, "[EIR] step4 vkAllocateMemory r2={} mem={}", (int)r2, (void*)memory);

				VkResult r3 = VK_SUCCESS;
				if (memory != VK_NULL_HANDLE)
				{
					r3 = vkBindImageMemory(device, newImage, memory, 0);
					LOG_DEBUG(Render, "[EIR] step5 vkBindImageMemory r3={}", (int)r3);
				}
				h.image = newImage;
				h.allocation = reinterpret_cast<void*>(memory);
				h.allocatedWithVma = false;

				if (r1 != VK_SUCCESS || r2 != VK_SUCCESS || r3 != VK_SUCCESS)
				{
					if (memory != VK_NULL_HANDLE)
					{
						vkFreeMemory(device, memory, nullptr);
					}
					if (newImage != VK_NULL_HANDLE)
					{
						vkDestroyImage(device, newImage, nullptr);
					}
					h.image = VK_NULL_HANDLE;
					h.allocation = nullptr;
					h.allocatedWithVma = false;
					LOG_ERROR(Render, "FrameGraph: image alloc failed for '{}': r1={} r2={} r3={}",
						res.name,
						static_cast<int>(r1),
						static_cast<int>(r2),
						static_cast<int>(r3));
					continue;
				}

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
			viewInfo.subresourceRange.levelCount = res.desc.mipLevels;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = res.desc.layers;

			//result = vkCreateImageView(device, &viewInfo, nullptr, &h.view);
			VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &h.view);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vkCreateImageView failed for '{}': {}", res.name, static_cast<int>(result));
				if (h.allocatedWithVma)
				{
					vmaDestroyImage(alloc, h.image, static_cast<VmaAllocation>(h.allocation));
				}
				else if (h.image != VK_NULL_HANDLE && h.allocation != nullptr)
				{
					VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(h.allocation);
					vkDestroyImage(device, h.image, nullptr);
					vkFreeMemory(device, mem, nullptr);
				}
				h.image = VK_NULL_HANDLE;
				h.allocation = nullptr;
				h.allocatedWithVma = false;
			}
		}
	}

	void FrameGraph::ensureBufferResources(VkDevice device, void* vmaAllocator,
		uint32_t frameIndex, uint32_t framesInFlight)
	{
		if (vmaAllocator == nullptr) return;
		VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator);
		const bool framesChanged = m_lastFramesInFlight != framesInFlight;
		if (framesChanged && m_lastFramesInFlight != 0 && vmaAllocator != nullptr)
		{
			for (auto& perFrame : m_perFrameBufferHandles)
			{
				for (PerFrameBufferHandles& h : perFrame)
				{
					if (h.buffer != VK_NULL_HANDLE && h.allocation != nullptr)
					{
						vmaDestroyBuffer(alloc, h.buffer, static_cast<VmaAllocation>(h.allocation));
						h.buffer = VK_NULL_HANDLE;
						h.allocation = nullptr;
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

			VmaAllocationCreateInfo allocCreateInfo{};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			VmaAllocation allocation = VK_NULL_HANDLE;
			VkResult result = vmaCreateBuffer(alloc, &bufInfo, &allocCreateInfo, &h.buffer, &allocation, nullptr);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "FrameGraph: vmaCreateBuffer failed for '{}': {}", res.name, static_cast<int>(result));
				continue;
			}
			h.allocation = allocation;
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

