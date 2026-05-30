// src/client/render/WaterMeshGpu.cpp
#include "src/client/render/WaterMeshGpu.h"
#include "src/client/world/water/WaterMeshBuilder.h"
#include "src/shared/core/Log.h"

#include <vulkan/vulkan_core.h>

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
		void EmitLakeVertices(const engine::world::water::LakeInstance& lake,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
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
				outVerts.push_back((v.position.x - minX) / dx);
				outVerts.push_back((v.position.z - minZ) / dz);
				outVerts.push_back(0.0f);
				outVerts.push_back(0.0f);
			}
		}

		// UV pour une riviere : u le long du flow, v perpendiculaire.
		void EmitRiverVertices(const engine::world::water::RiverInstance& river,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			const auto flows = engine::world::water::ComputeFlowDirections(river);
			const size_t nNodes = river.nodes.size();
			for (size_t vi = 0; vi < cpuMesh.vertices.size(); ++vi)
			{
				const auto& v = cpuMesh.vertices[vi];
				const size_t nodeIdx = vi / 2;
				const bool isLeft = (vi % 2) == 0;
				const float u = (nNodes > 1)
					? static_cast<float>(nodeIdx) / static_cast<float>(nNodes - 1) : 0.0f;
				const float vCoord = isLeft ? 0.0f : 1.0f;
				assert(!flows.empty());
				const size_t segIdx = (nodeIdx < flows.size()) ? nodeIdx : flows.size() - 1;
				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back(u);
				outVerts.push_back(vCoord);
				outVerts.push_back(flows[segIdx].x);
				outVerts.push_back(flows[segIdx].z);
			}
		}

		/// Recherche un type memoire satisfaisant \p requiredFlags dans \p memProps.
		/// \return Index du type memoire, ou UINT32_MAX si aucun.
		static uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& memProps,
			uint32_t memoryTypeBits, VkMemoryPropertyFlags requiredFlags)
		{
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((memoryTypeBits & (1u << i)) &&
				    (memProps.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
					return i;
			}
			return UINT32_MAX;
		}

		/// Alloue un VkBuffer + VkDeviceMemory de type DEVICE_LOCAL.
		/// \return false si une etape Vulkan echoue (outBuf/outMem laisses inchanges).
		static bool AllocDeviceLocalBuffer(
			VkDevice device, VkPhysicalDevice physDev,
			VkDeviceSize size, VkBufferUsageFlags usage,
			VkBuffer& outBuf, VkDeviceMemory& outMem)
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = size;
			bi.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS) return false;

			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(device, outBuf, &req);

			VkPhysicalDeviceMemoryProperties props{};
			vkGetPhysicalDeviceMemoryProperties(physDev, &props);
			const uint32_t memType = FindMemoryType(props, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memType == UINT32_MAX)
			{
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = memType;
			if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}
			vkBindBufferMemory(device, outBuf, outMem, 0);
			return true;
		}

		/// Alloue un VkBuffer + VkDeviceMemory HOST_VISIBLE | HOST_COHERENT (staging).
		static bool AllocHostVisibleBuffer(
			VkDevice device, VkPhysicalDevice physDev,
			VkDeviceSize size,
			VkBuffer& outBuf, VkDeviceMemory& outMem)
		{
			VkBufferCreateInfo bi{};
			bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size        = size;
			bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS) return false;

			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(device, outBuf, &req);

			VkPhysicalDeviceMemoryProperties props{};
			vkGetPhysicalDeviceMemoryProperties(physDev, &props);
			constexpr VkMemoryPropertyFlags kHostFlags =
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			const uint32_t memType = FindMemoryType(props, req.memoryTypeBits, kHostFlags);
			if (memType == UINT32_MAX)
			{
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo ai{};
			ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			ai.allocationSize  = req.size;
			ai.memoryTypeIndex = memType;
			if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
			{
				vkDestroyBuffer(device, outBuf, nullptr);
				outBuf = VK_NULL_HANDLE;
				return false;
			}
			vkBindBufferMemory(device, outBuf, outMem, 0);
			return true;
		}

	} // namespace

	// -----------------------------------------------------------------------
	// BuildDrawInfos (inchange — aucune dependance VMA)
	// -----------------------------------------------------------------------

	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos)
	{
		outVertices.clear();
		outIndices.clear();
		outDrawInfos.clear();

		uint32_t globalParamIdx = 0;

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

			const int32_t  baseVertex  = static_cast<int32_t>(outVertices.size() / kFloatsPerVertex);
			const uint32_t firstIndex  = static_cast<uint32_t>(outIndices.size());
			EmitLakeVertices(lake, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex  = firstIndex;
			info.indexCount  = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);
			++globalParamIdx;
		}

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

			const int32_t  baseVertex  = static_cast<int32_t>(outVertices.size() / kFloatsPerVertex);
			const uint32_t firstIndex  = static_cast<uint32_t>(outIndices.size());
			EmitRiverVertices(river, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex  = firstIndex;
			info.indexCount  = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);
			++globalParamIdx;
		}
	}

	// -----------------------------------------------------------------------
	// WaterMeshGpu — raw Vulkan (STAB.7 compatible, sans VMA)
	// -----------------------------------------------------------------------

	bool WaterMeshGpu::Init(VkDevice device, VkPhysicalDevice physicalDevice)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] Init FAILED: invalid arguments");
			return false;
		}
		m_device         = device;
		m_physicalDevice = physicalDevice;
		LOG_INFO(Render, "[WaterMeshGpu] Init OK (raw Vulkan, STAB.7 compatible)");
		return true;
	}

	void WaterMeshGpu::Destroy()
	{
		if (m_device == VK_NULL_HANDLE) return;

		if (m_vbo != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_device, m_vbo, nullptr);
			m_vbo = VK_NULL_HANDLE;
		}
		if (m_vboMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_device, m_vboMemory, nullptr);
			m_vboMemory = VK_NULL_HANDLE;
		}
		m_vboCapacity = 0;

		if (m_ibo != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_device, m_ibo, nullptr);
			m_ibo = VK_NULL_HANDLE;
		}
		if (m_iboMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_device, m_iboMemory, nullptr);
			m_iboMemory = VK_NULL_HANDLE;
		}
		m_iboCapacity = 0;

		m_drawInfos.clear();
		m_device         = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		LOG_INFO(Render, "[WaterMeshGpu] Destroyed");
	}

	bool WaterMeshGpu::EnsureCapacity(VkDeviceSize newVboSize, VkDeviceSize newIboSize)
	{
		if (newVboSize > m_vboCapacity)
		{
			if (m_vbo != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(m_device, m_vbo, nullptr);
				m_vbo = VK_NULL_HANDLE;
			}
			if (m_vboMemory != VK_NULL_HANDLE)
			{
				vkFreeMemory(m_device, m_vboMemory, nullptr);
				m_vboMemory = VK_NULL_HANDLE;
			}
			if (!AllocDeviceLocalBuffer(m_device, m_physicalDevice,
				newVboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vbo, m_vboMemory))
			{
				LOG_ERROR(Render, "[WaterMeshGpu] VBO allocation failed (size={})", newVboSize);
				m_vboCapacity = 0;
				return false;
			}
			m_vboCapacity = newVboSize;
		}

		if (newIboSize > m_iboCapacity)
		{
			if (m_ibo != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(m_device, m_ibo, nullptr);
				m_ibo = VK_NULL_HANDLE;
			}
			if (m_iboMemory != VK_NULL_HANDLE)
			{
				vkFreeMemory(m_device, m_iboMemory, nullptr);
				m_iboMemory = VK_NULL_HANDLE;
			}
			if (!AllocDeviceLocalBuffer(m_device, m_physicalDevice,
				newIboSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_ibo, m_iboMemory))
			{
				LOG_ERROR(Render, "[WaterMeshGpu] IBO allocation failed (size={})", newIboSize);
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
		if (m_device == VK_NULL_HANDLE || m_physicalDevice == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] Rebuild FAILED: not initialised");
			return false;
		}

		std::vector<float>               verts;
		std::vector<uint32_t>            idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		if (verts.empty() || idx.empty())
		{
			m_drawInfos.clear();
			return true;
		}

		const VkDeviceSize vboBytes    = verts.size() * sizeof(float);
		const VkDeviceSize iboBytes    = idx.size() * sizeof(uint32_t);
		const VkDeviceSize stagingSize = vboBytes + iboBytes;

		if (!EnsureCapacity(vboBytes, iboBytes))
			return false;

		// Staging buffer HOST_VISIBLE + HOST_COHERENT.
		VkBuffer       staging    = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		if (!AllocHostVisibleBuffer(m_device, m_physicalDevice, stagingSize, staging, stagingMem))
		{
			LOG_ERROR(Render, "[WaterMeshGpu] staging buffer allocation failed (size={})", stagingSize);
			return false;
		}

		// Copie CPU → staging.
		{
			void* mapped = nullptr;
			vkMapMemory(m_device, stagingMem, 0, stagingSize, 0, &mapped);
			std::memcpy(mapped, verts.data(), vboBytes);
			std::memcpy(static_cast<char*>(mapped) + vboBytes, idx.data(), iboBytes);
			vkUnmapMemory(m_device, stagingMem);
		}

		// Command buffer one-shot.
		VkCommandBufferAllocateInfo cmdAlloc{};
		cmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAlloc.commandPool        = transferPool;
		cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdAlloc.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd) != VK_SUCCESS)
		{
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			LOG_ERROR(Render, "[WaterMeshGpu] vkAllocateCommandBuffers failed");
			return false;
		}

		VkCommandBufferBeginInfo beg{};
		beg.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beg.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(cmd, &beg) != VK_SUCCESS)
		{
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			LOG_ERROR(Render, "[WaterMeshGpu] vkBeginCommandBuffer failed");
			return false;
		}

		VkBufferCopy copyVbo{0, 0, vboBytes};
		vkCmdCopyBuffer(cmd, staging, m_vbo, 1, &copyVbo);

		VkBufferCopy copyIbo{vboBytes, 0, iboBytes};
		vkCmdCopyBuffer(cmd, staging, m_ibo, 1, &copyIbo);

		VkBufferMemoryBarrier barriers[2]{};
		barriers[0].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barriers[0].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
		barriers[0].dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].buffer              = m_vbo;
		barriers[0].offset              = 0;
		barriers[0].size                = VK_WHOLE_SIZE;
		barriers[1]               = barriers[0];
		barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
		barriers[1].buffer        = m_ibo;
		vkCmdPipelineBarrier(cmd,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		    0, 0, nullptr, 2, barriers, 0, nullptr);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			LOG_ERROR(Render, "[WaterMeshGpu] vkEndCommandBuffer failed");
			return false;
		}

		VkFenceCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VkFence fence = VK_NULL_HANDLE;
		if (vkCreateFence(m_device, &fci, nullptr, &fence) != VK_SUCCESS)
		{
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			LOG_ERROR(Render, "[WaterMeshGpu] vkCreateFence failed");
			return false;
		}

		VkSubmitInfo sub{};
		sub.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		sub.commandBufferCount = 1;
		sub.pCommandBuffers    = &cmd;
		if (vkQueueSubmit(transferQueue, 1, &sub, fence) != VK_SUCCESS)
		{
			vkDestroyFence(m_device, fence, nullptr);
			vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
			vkDestroyBuffer(m_device, staging, nullptr);
			vkFreeMemory(m_device, stagingMem, nullptr);
			LOG_ERROR(Render, "[WaterMeshGpu] vkQueueSubmit failed");
			return false;
		}
		vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

		vkDestroyFence(m_device, fence, nullptr);
		vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
		vkDestroyBuffer(m_device, staging, nullptr);
		vkFreeMemory(m_device, stagingMem, nullptr);

		m_drawInfos = std::move(infos);
		LOG_INFO(Render, "[WaterMeshGpu] Rebuilt ({} instance(s), vbo={} B, ibo={} B)",
		         m_drawInfos.size(), static_cast<long long>(vboBytes), static_cast<long long>(iboBytes));
		return true;
	}
}
