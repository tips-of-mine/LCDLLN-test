#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace engine::render
{
	namespace
	{
		// Vulkan pipeline cache header: length (4), version (4), vendor ID (4), device ID (4), UUID (16).
		constexpr uint32_t kPipelineCacheHeaderSize = 16u;
		constexpr uint32_t kPipelineCacheVersion = 1u;
	}

	static bool s_warmupPhase = false;
	static std::vector<PsoKey> s_warmupKeys;

	void PipelineCache::BeginWarmup()
	{
		s_warmupPhase = true;
		s_warmupKeys.clear();
	}

	void PipelineCache::EndWarmup()
	{
		s_warmupPhase = false;
		LOG_INFO(Render, "[PipelineCache] Warmup complete: {} PSO(s) created", s_warmupKeys.size());
		ClearWarmupList();
	}

	bool PipelineCache::IsWarmupPhase()
	{
		return s_warmupPhase;
	}

	void PipelineCache::RegisterWarmupKey(const PsoKey& key)
	{
		if (s_warmupPhase && key.Valid())
			s_warmupKeys.push_back(key);
	}

	size_t PipelineCache::GetWarmupKeyCount()
	{
		return s_warmupKeys.size();
	}

	void PipelineCache::ClearWarmupList()
	{
		s_warmupKeys.clear();
	}

	void AssertPipelineCreationAllowed()
	{
#if defined(_DEBUG) || defined(DEBUG) || (!defined(NDEBUG))
		assert(PipelineCache::IsWarmupPhase() && "Pipeline creation only allowed during boot warmup (M18.5)");
#else
		(void)0;
#endif
	}

	bool PipelineCache::Init(VkDevice device, const std::string& cacheFilePath)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[PipelineCache] Init FAILED: invalid device");
			return false;
		}

		m_cacheFilePath = cacheFilePath;
		std::vector<uint8_t> initialData;

		std::ifstream in(cacheFilePath, std::ios::binary);
		if (in)
		{
			in.seekg(0, std::ios::end);
			const std::streamsize size = in.tellg();
			in.seekg(0, std::ios::beg);
			if (size > 0 && size <= 64 * 1024 * 1024) // sane max 64 MB
			{
				initialData.resize(static_cast<size_t>(size));
				if (in.read(reinterpret_cast<char*>(initialData.data()), size))
				{
					if (initialData.size() < kPipelineCacheHeaderSize)
					{
						LOG_WARN(Render, "[PipelineCache] Cache file too small, ignoring");
						initialData.clear();
					}
					else
						LOG_INFO(Render, "[PipelineCache] Loaded {} bytes from {}", initialData.size(), cacheFilePath);
				}
				else
					initialData.clear();
			}
			else if (size > 0)
				LOG_WARN(Render, "[PipelineCache] Cache file size invalid ({}), ignoring", static_cast<long long>(size));
		}
		else
			LOG_DEBUG(Render, "[PipelineCache] No existing cache file at {}", cacheFilePath);

		VkPipelineCacheCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		createInfo.initialDataSize = initialData.size();
		createInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();

		VkResult res = vkCreatePipelineCache(device, &createInfo, nullptr, &m_cache);
		if (res != VK_SUCCESS)
		{
			if (!initialData.empty())
			{
				LOG_WARN(Render, "[PipelineCache] Cache load rejected by driver ({}), creating empty cache", static_cast<int>(res));
				createInfo.initialDataSize = 0;
				createInfo.pInitialData = nullptr;
				res = vkCreatePipelineCache(device, &createInfo, nullptr, &m_cache);
			}
			if (res != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[PipelineCache] Init FAILED: vkCreatePipelineCache {}", static_cast<int>(res));
				return false;
			}
		}

		LOG_INFO(Render, "[PipelineCache] Init OK (path={})", cacheFilePath);
		return true;
	}

	void PipelineCache::Destroy(VkDevice device)
	{
		if (device == VK_NULL_HANDLE)
		{
			LOG_INFO(Render, "[PipelineCache] Destroyed (was not initialised)");
			return;
		}
		if (m_cache == VK_NULL_HANDLE)
		{
			LOG_INFO(Render, "[PipelineCache] Destroyed (no cache handle)");
			return;
		}

		size_t dataSize = 0;
		VkResult res = vkGetPipelineCacheData(device, m_cache, &dataSize, nullptr);
		if (res == VK_SUCCESS && dataSize > 0 && !m_cacheFilePath.empty())
		{
			std::vector<uint8_t> data(dataSize);
			res = vkGetPipelineCacheData(device, m_cache, &dataSize, data.data());
			if (res == VK_SUCCESS)
			{
				std::error_code ec;
				std::filesystem::path p(m_cacheFilePath);
				std::filesystem::create_directories(p.parent_path(), ec);
				if (!ec)
				{
					std::ofstream out(m_cacheFilePath, std::ios::binary);
					if (out && out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))
						LOG_INFO(Render, "[PipelineCache] Saved {} bytes to {}", data.size(), m_cacheFilePath);
					else
						LOG_WARN(Render, "[PipelineCache] Failed to write cache file {}", m_cacheFilePath);
				}
				else
					LOG_WARN(Render, "[PipelineCache] Failed to create cache directory: {}", ec.message());
			}
		}

		vkDestroyPipelineCache(device, m_cache, nullptr);
		m_cache = VK_NULL_HANDLE;
		LOG_INFO(Render, "[PipelineCache] Destroyed");
	}
}
