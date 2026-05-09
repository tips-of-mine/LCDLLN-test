// engine/render/WaterMeshGpu.cpp
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterMeshBuilder.h"
#include "engine/core/Log.h"

#include <vk_mem_alloc.h>

#include <cassert>
#include <cmath>
#include <cstring>

namespace engine::render
{
	namespace
	{
		static constexpr size_t kFloatsPerVertex = 7;  // pos3 + uv2 + flowDir2

		static_assert(sizeof(engine::render::WaterVertex) == kFloatsPerVertex * sizeof(float),
			"WaterVertex layout must match kFloatsPerVertex");

		// UV pour un lac : projection top-down (XZ) normalisee par la BBox du polygone.
		// Cela garantit des UV [0..1] coherents quelle que soit la taille du lac.
		void EmitLakeVertices(const engine::world::water::LakeInstance& lake,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			// Calcule la BBox XZ du polygone.
			float minX = lake.polygon[0].x, maxX = lake.polygon[0].x;
			float minZ = lake.polygon[0].z, maxZ = lake.polygon[0].z;
			for (const auto& p : lake.polygon)
			{
				minX = std::fmin(minX, p.x); maxX = std::fmax(maxX, p.x);
				minZ = std::fmin(minZ, p.z); maxZ = std::fmax(maxZ, p.z);
			}
			const float dx = std::fmax(1e-3f, maxX - minX);
			const float dz = std::fmax(1e-3f, maxZ - minZ);

			for (const auto& v : cpuMesh.vertices)
			{
				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back((v.position.x - minX) / dx);  // u
				outVerts.push_back((v.position.z - minZ) / dz);  // v
				outVerts.push_back(0.0f);                         // flowDir.x = 0 (lac)
				outVerts.push_back(0.0f);                         // flowDir.y = 0 (lac)
			}
		}

		// UV pour une riviere : u le long du flow, v perpendiculaire.
		// Le ribbon mesh produit par BuildRiverMesh emet 2 vertices par node
		// (gauche/droite). On peut donc deriver u depuis l'index pair/impair.
		void EmitRiverVertices(const engine::world::water::RiverInstance& river,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			// Flow direction par segment, depuis ComputeFlowDirections (M100.13).
			const auto flows = engine::world::water::ComputeFlowDirections(river);

			// Le ribbon BuildRiverMesh emet vertices dans l'ordre :
			//   node 0 left, node 0 right, node 1 left, node 1 right, ..., node N-1 left, node N-1 right.
			// Soit 2 * N_nodes vertices, ou N_nodes = river.nodes.size().
			// Le vertex 2*i est "left" du node i, le vertex 2*i+1 est "right".
			const size_t nNodes = river.nodes.size();
			for (size_t vi = 0; vi < cpuMesh.vertices.size(); ++vi)
			{
				const auto& v = cpuMesh.vertices[vi];
				const size_t nodeIdx = vi / 2;
				const bool isLeft = (vi % 2) == 0;

				// u = nodeIdx / (nNodes - 1) (longueur le long du flow)
				const float u = (nNodes > 1) ? static_cast<float>(nodeIdx) / static_cast<float>(nNodes - 1) : 0.0f;
				const float vCoord = isLeft ? 0.0f : 1.0f;

				// flowDir : utilise le flow du segment courant (clamp au dernier pour le dernier node).
				assert(!flows.empty());  // Précondition BuildRiverMesh : nodes.size() >= 2
				const size_t segIdx = (nodeIdx < flows.size()) ? nodeIdx : flows.size() - 1;
				const float fx = flows[segIdx].x;
				const float fz = flows[segIdx].z;

				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back(u);
				outVerts.push_back(vCoord);
				outVerts.push_back(fx);
				outVerts.push_back(fz);
			}
		}
	} // namespace

	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos)
	{
		outVertices.clear();
		outIndices.clear();
		outDrawInfos.clear();

		uint32_t globalParamIdx = 0;

		// 1) Lacs en tete.
		for (const auto& lake : scene.lakes)
		{
			engine::world::water::WaterMeshCpu cpuMesh;
			std::string err;
			if (!engine::world::water::BuildLakeMesh(lake, cpuMesh, err))
			{
				LOG_WARN(Render, "[WaterMeshGpu] BuildLakeMesh failed for '{}': {}", lake.name, err);
				++globalParamIdx;
				continue;
			}
			if (cpuMesh.indices.empty()) { ++globalParamIdx; continue; }

			const int32_t baseVertex = static_cast<int32_t>(outVertices.size() / kFloatsPerVertex);
			const uint32_t firstIndex = static_cast<uint32_t>(outIndices.size());

			EmitLakeVertices(lake, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex = firstIndex;
			info.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);

			++globalParamIdx;
		}

		// 2) Rivieres ensuite.
		for (const auto& river : scene.rivers)
		{
			engine::world::water::WaterMeshCpu cpuMesh;
			std::string err;
			if (!engine::world::water::BuildRiverMesh(river, cpuMesh, err))
			{
				LOG_WARN(Render, "[WaterMeshGpu] BuildRiverMesh failed for '{}': {}", river.name, err);
				++globalParamIdx;
				continue;
			}
			if (cpuMesh.indices.empty()) { ++globalParamIdx; continue; }

			const int32_t baseVertex = static_cast<int32_t>(outVertices.size() / kFloatsPerVertex);
			const uint32_t firstIndex = static_cast<uint32_t>(outIndices.size());

			EmitRiverVertices(river, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex = firstIndex;
			info.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);

			++globalParamIdx;
		}
	}

	bool WaterMeshGpu::Init(VkDevice device, void* vmaAllocator)
	{
		if (device == VK_NULL_HANDLE || vmaAllocator == nullptr)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] Init FAILED: invalid arguments");
			return false;
		}
		m_device       = device;
		m_vmaAllocator = vmaAllocator;
		LOG_INFO(Render, "[WaterMeshGpu] Init OK");
		return true;
	}

	void WaterMeshGpu::Destroy()
	{
		if (m_vmaAllocator == nullptr) return;
		auto* allocator = static_cast<VmaAllocator>(m_vmaAllocator);

		if (m_vbo != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, m_vbo, static_cast<VmaAllocation>(m_vboAllocation));
			m_vbo           = VK_NULL_HANDLE;
			m_vboAllocation = nullptr;
			m_vboCapacity   = 0;
		}
		if (m_ibo != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, m_ibo, static_cast<VmaAllocation>(m_iboAllocation));
			m_ibo           = VK_NULL_HANDLE;
			m_iboAllocation = nullptr;
			m_iboCapacity   = 0;
		}
		m_drawInfos.clear();
		m_device       = VK_NULL_HANDLE;
		m_vmaAllocator = nullptr;
		LOG_INFO(Render, "[WaterMeshGpu] Destroyed");
	}

	bool WaterMeshGpu::EnsureCapacity(VkDeviceSize newVboSize, VkDeviceSize newIboSize)
	{
		auto* allocator = static_cast<VmaAllocator>(m_vmaAllocator);

		// Helper local : alloue un buffer device-local de taille donnee.
		auto allocBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
		                       VkBuffer& outBuf, void*& outAlloc) -> bool
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = size;
			bi.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo ai{};
			ai.usage = VMA_MEMORY_USAGE_AUTO;
			ai.flags = 0;  // Pas d'accès host pour buffers device-local
			VmaAllocation alloc = nullptr;
			VkResult r = vmaCreateBuffer(allocator, &bi, &ai, &outBuf, &alloc, nullptr);
			if (r != VK_SUCCESS) return false;
			outAlloc = alloc;
			return true;
		};

		if (newVboSize > m_vboCapacity)
		{
			if (m_vbo != VK_NULL_HANDLE)
				vmaDestroyBuffer(allocator, m_vbo, static_cast<VmaAllocation>(m_vboAllocation));
			m_vbo           = VK_NULL_HANDLE;
			m_vboAllocation = nullptr;
			if (!allocBuffer(newVboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vbo, m_vboAllocation))
			{
				LOG_ERROR(Render, "[WaterMeshGpu] vmaCreateBuffer (VBO) failed (size={})", newVboSize);
				m_vboCapacity = 0;
				return false;
			}
			m_vboCapacity = newVboSize;
		}
		if (newIboSize > m_iboCapacity)
		{
			if (m_ibo != VK_NULL_HANDLE)
				vmaDestroyBuffer(allocator, m_ibo, static_cast<VmaAllocation>(m_iboAllocation));
			m_ibo           = VK_NULL_HANDLE;
			m_iboAllocation = nullptr;
			if (!allocBuffer(newIboSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_ibo, m_iboAllocation))
			{
				LOG_ERROR(Render, "[WaterMeshGpu] vmaCreateBuffer (IBO) failed (size={})", newIboSize);
				m_iboCapacity = 0;
				return false;
			}
			m_iboCapacity = newIboSize;
		}
		return true;
	}

	bool WaterMeshGpu::Rebuild(VkCommandPool transferPool, VkQueue transferQueue,
	                           const engine::world::water::WaterScene& scene)
	{
		if (m_device == VK_NULL_HANDLE || m_vmaAllocator == nullptr)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] Rebuild FAILED: not initialised");
			return false;
		}

		std::vector<float>               verts;
		std::vector<uint32_t>            idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		// Cas vide : conserve les buffers existants mais drawInfos = empty.
		// Record() fera no-op si GetInstanceCount() == 0.
		if (verts.empty() || idx.empty())
		{
			m_drawInfos.clear();
			return true;
		}

		const VkDeviceSize vboBytes = verts.size() * sizeof(float);
		const VkDeviceSize iboBytes = idx.size() * sizeof(uint32_t);

		if (!EnsureCapacity(vboBytes, iboBytes))
			return false;

		auto* allocator = static_cast<VmaAllocator>(m_vmaAllocator);

		// Staging buffer host-visible mappe (CPU_ONLY + MAPPED).
		VkBuffer          staging      = VK_NULL_HANDLE;
		VmaAllocation     stagingAlloc = nullptr;
		const VkDeviceSize stagingSize = vboBytes + iboBytes;
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = stagingSize;
			bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo ai{};
			ai.usage = VMA_MEMORY_USAGE_AUTO;
			ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			         | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			VmaAllocationInfo allocInfo{};
			if (vmaCreateBuffer(allocator, &bi, &ai, &staging, &stagingAlloc, &allocInfo) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterMeshGpu] staging vmaCreateBuffer failed (size={})", stagingSize);
				return false;
			}
			std::memcpy(allocInfo.pMappedData, verts.data(), vboBytes);
			std::memcpy(static_cast<char*>(allocInfo.pMappedData) + vboBytes, idx.data(), iboBytes);
		}

		// Command buffer one-shot pour copier staging → buffers device-local.
		VkCommandBufferAllocateInfo cmdAlloc{};
		cmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAlloc.commandPool        = transferPool;
		cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdAlloc.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd) != VK_SUCCESS)
		{
			vmaDestroyBuffer(allocator, staging, stagingAlloc);
			LOG_ERROR(Render, "[WaterMeshGpu] vkAllocateCommandBuffers failed");
			return false;
		}

		VkCommandBufferBeginInfo beg{};
		beg.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beg.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(cmd, &beg) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] vkBeginCommandBuffer failed");
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vmaDestroyBuffer(allocator, staging, stagingAlloc);
			return false;
		}

		VkBufferCopy copyVbo{};
		copyVbo.srcOffset = 0;
		copyVbo.dstOffset = 0;
		copyVbo.size      = vboBytes;
		vkCmdCopyBuffer(cmd, staging, m_vbo, 1, &copyVbo);

		VkBufferCopy copyIbo{};
		copyIbo.srcOffset = vboBytes;
		copyIbo.dstOffset = 0;
		copyIbo.size      = iboBytes;
		vkCmdCopyBuffer(cmd, staging, m_ibo, 1, &copyIbo);

		// Pipeline barrier : visibility entre transfer write et vertex/index read.
		VkBufferMemoryBarrier barriers[2]{};
		barriers[0].sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].buffer        = m_vbo;
		barriers[0].offset        = 0;
		barriers[0].size          = VK_WHOLE_SIZE;
		barriers[1] = barriers[0];
		barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
		barriers[1].buffer        = m_ibo;
		vkCmdPipelineBarrier(cmd,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		    0, 0, nullptr, 2, barriers, 0, nullptr);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] vkEndCommandBuffer failed");
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vmaDestroyBuffer(allocator, staging, stagingAlloc);
			return false;
		}

		VkFenceCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VkFence fence = VK_NULL_HANDLE;
		if (vkCreateFence(m_device, &fci, nullptr, &fence) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] vkCreateFence failed");
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vmaDestroyBuffer(allocator, staging, stagingAlloc);
			return false;
		}

		VkSubmitInfo sub{};
		sub.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		sub.commandBufferCount = 1;
		sub.pCommandBuffers    = &cmd;
		if (vkQueueSubmit(transferQueue, 1, &sub, fence) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] vkQueueSubmit failed");
			vkDestroyFence(m_device, fence, nullptr);
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vmaDestroyBuffer(allocator, staging, stagingAlloc);
			return false;
		}
		vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

		vkDestroyFence(m_device, fence, nullptr);
		vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
		vmaDestroyBuffer(allocator, staging, stagingAlloc);

		m_drawInfos = std::move(infos);
		LOG_INFO(Render, "[WaterMeshGpu] Rebuilt ({} instances, vbo={} B, ibo={} B)",
		         m_drawInfos.size(), static_cast<long long>(vboBytes), static_cast<long long>(iboBytes));
		return true;
	}
}
