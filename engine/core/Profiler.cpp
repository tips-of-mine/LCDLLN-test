#include "engine/core/Profiler.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <functional>
#include <thread>

namespace engine::core
{
	namespace
	{
		inline constexpr uint32_t kProfilerHistoryFrames = 120;
		thread_local uint32_t g_threadId = 0;
		Profiler* g_activeProfiler = nullptr;

		uint32_t GetCurrentThreadId()
		{
			if (g_threadId != 0)
			{
				return g_threadId;
			}

			const size_t hashedId = std::hash<std::thread::id>{}(std::this_thread::get_id());
			g_threadId = static_cast<uint32_t>((hashedId & 0xffffffffu) | 1u);
			return g_threadId;
		}
	}

	Profiler::~Profiler()
	{
	}

	bool Profiler::Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight, uint32_t maxGpuPasses)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[Profiler] Init ignored: already initialized");
			return true;
		}

		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || framesInFlight == 0 || maxGpuPasses == 0)
		{
			LOG_ERROR(Core, "[Profiler] Init FAILED: invalid Vulkan args (frames_in_flight={}, max_gpu_passes={})",
				framesInFlight,
				maxGpuPasses);
			return false;
		}

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physicalDevice, &props);
		m_timestampPeriodNs = static_cast<double>(props.limits.timestampPeriod);
		m_framesInFlight = framesInFlight;
		m_maxGpuPasses = maxGpuPasses;
		m_history.resize(kProfilerHistoryFrames);
		m_gpuFrames.resize(framesInFlight);
		for (GpuFrameSlot& slot : m_gpuFrames)
		{
			slot.passes.reserve(maxGpuPasses);
		}

		VkQueryPoolCreateInfo queryPoolInfo{};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryPoolInfo.queryCount = framesInFlight * maxGpuPasses * 2u;
		if (vkCreateQueryPool(device, &queryPoolInfo, nullptr, &m_queryPool) != VK_SUCCESS)
		{
			LOG_ERROR(Core, "[Profiler] Init FAILED: vkCreateQueryPool failed");
			m_gpuFrames.clear();
			m_history.clear();
			m_framesInFlight = 0;
			m_maxGpuPasses = 0;
			return false;
		}

		g_activeProfiler = this;
		m_initialized = true;
		LOG_INFO(Core, "[Profiler] Init OK (frames_in_flight={}, gpu_pass_budget={}, timestamp_period_ns={:.3f})",
			m_framesInFlight,
			m_maxGpuPasses,
			m_timestampPeriodNs);
		return true;
	}

	void Profiler::Shutdown(VkDevice device)
	{
		if (!m_initialized)
		{
			return;
		}

		if (g_activeProfiler == this)
		{
			g_activeProfiler = nullptr;
		}

		if (device != VK_NULL_HANDLE && m_queryPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(device, m_queryPool, nullptr);
		}

		m_queryPool = VK_NULL_HANDLE;
		m_gpuFrames.clear();
		m_history.clear();
		m_latestSnapshot = {};
		m_timestampPeriodNs = 0.0;
		m_historyIndex = 0;
		m_framesInFlight = 0;
		m_maxGpuPasses = 0;
		m_frameOpen = false;
		m_initialized = false;
		LOG_INFO(Core, "[Profiler] Destroyed");
	}

	bool Profiler::BeginFrame(uint64_t frameIndex)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[Profiler] BeginFrame FAILED: profiler not initialized");
			return false;
		}

		std::scoped_lock lock(m_mutex);
		m_historyIndex = (m_historyIndex + 1u) % static_cast<uint32_t>(m_history.size());
		ProfilerFrameSnapshot& frame = m_history[m_historyIndex];
		frame = {};
		frame.frameIndex = frameIndex;
		frame.cpuScopes.reserve(64);
		m_frameStartTime = std::chrono::steady_clock::now();
		m_frameOpen = true;
		return true;
	}

	void Profiler::EndFrame()
	{
		if (!m_initialized || !m_frameOpen)
		{
			return;
		}

		std::scoped_lock lock(m_mutex);
		ProfilerFrameSnapshot& frame = m_history[m_historyIndex];
		const auto now = std::chrono::steady_clock::now();
		frame.cpuTotalMs = std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();
		m_frameOpen = false;
	}

	bool Profiler::BeginGpuFrame(VkCommandBuffer cmd, uint32_t frameSlot)
	{
		if (!m_initialized || cmd == VK_NULL_HANDLE || frameSlot >= m_gpuFrames.size() || m_queryPool == VK_NULL_HANDLE)
		{
			return false;
		}

		GpuFrameSlot& slot = m_gpuFrames[frameSlot];
		slot.passes.clear();
		slot.usedQueryCount = 0;
		slot.historyIndex = m_historyIndex;
		slot.frameIndex = m_history[m_historyIndex].frameIndex;
		slot.frameOpen = true;

		const uint32_t firstQuery = frameSlot * m_maxGpuPasses * 2u;
		vkCmdResetQueryPool(cmd, m_queryPool, firstQuery, m_maxGpuPasses * 2u);
		return true;
	}

	bool Profiler::BeginGpuPass(VkCommandBuffer cmd, uint32_t frameSlot, std::string_view passName)
	{
		if (!m_initialized || cmd == VK_NULL_HANDLE || frameSlot >= m_gpuFrames.size() || m_queryPool == VK_NULL_HANDLE)
		{
			return false;
		}

		GpuFrameSlot& slot = m_gpuFrames[frameSlot];
		if (!slot.frameOpen)
		{
			return false;
		}

		if (slot.passes.size() >= m_maxGpuPasses)
		{
			LOG_WARN(Core, "[Profiler] GPU pass budget exhausted; '{}' skipped", passName);
			return false;
		}

		const uint32_t firstQuery = frameSlot * m_maxGpuPasses * 2u;
		const uint32_t startQuery = firstQuery + slot.usedQueryCount;
		const uint32_t endQuery = startQuery + 1u;
		slot.usedQueryCount += 2u;
		slot.passes.push_back({ std::string(passName), startQuery, endQuery });
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, startQuery);
		return true;
	}

	bool Profiler::EndGpuPass(VkCommandBuffer cmd, uint32_t frameSlot)
	{
		if (!m_initialized || cmd == VK_NULL_HANDLE || frameSlot >= m_gpuFrames.size() || m_queryPool == VK_NULL_HANDLE)
		{
			return false;
		}

		GpuFrameSlot& slot = m_gpuFrames[frameSlot];
		if (slot.passes.empty())
		{
			return false;
		}

		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, slot.passes.back().endQuery);
		return true;
	}

	bool Profiler::ResolveGpuFrame(VkDevice device, uint32_t frameSlot)
	{
		if (!m_initialized || device == VK_NULL_HANDLE || frameSlot >= m_gpuFrames.size() || m_queryPool == VK_NULL_HANDLE)
		{
			return false;
		}

		GpuFrameSlot& slot = m_gpuFrames[frameSlot];
		if (slot.usedQueryCount == 0)
		{
			std::scoped_lock lock(m_mutex);
			ProfilerFrameSnapshot& frame = m_history[slot.historyIndex];
			frame.gpuPasses.clear();
			frame.gpuTotalMs = 0.0;
			m_latestSnapshot = frame;
			return true;
		}

		std::vector<uint64_t> timestamps(slot.usedQueryCount, 0ull);
		const uint32_t firstQuery = frameSlot * m_maxGpuPasses * 2u;
		const VkResult result = vkGetQueryPoolResults(
			device,
			m_queryPool,
			firstQuery,
			slot.usedQueryCount,
			sizeof(uint64_t) * timestamps.size(),
			timestamps.data(),
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);
		if (result != VK_SUCCESS)
		{
			LOG_WARN(Core, "[Profiler] ResolveGpuFrame incomplete (slot={}, result={})", frameSlot, static_cast<int>(result));
			return false;
		}

		std::scoped_lock lock(m_mutex);
		ProfilerFrameSnapshot& frame = m_history[slot.historyIndex];
		frame.gpuPasses.clear();
		frame.gpuPasses.reserve(slot.passes.size());
		frame.gpuTotalMs = 0.0;

		for (const GpuPassRecord& pass : slot.passes)
		{
			const uint32_t localStart = pass.startQuery - firstQuery;
			const uint32_t localEnd = pass.endQuery - firstQuery;
			if (localEnd >= timestamps.size())
			{
				continue;
			}

			GpuProfileSample sample{};
			sample.name = pass.name;
			if (timestamps[localEnd] >= timestamps[localStart])
			{
				sample.valid = true;
				sample.durationMs = (static_cast<double>(timestamps[localEnd] - timestamps[localStart]) * m_timestampPeriodNs) / 1000000.0;
				frame.gpuTotalMs += sample.durationMs;
			}
			frame.gpuPasses.push_back(std::move(sample));
		}

		m_latestSnapshot = frame;
		slot.frameOpen = false;
		return true;
	}

	void Profiler::RecordCpuScope(std::string_view name, double durationMs)
	{
		if (!m_initialized || !m_frameOpen)
		{
			return;
		}

		std::scoped_lock lock(m_mutex);
		ProfilerFrameSnapshot& frame = m_history[m_historyIndex];
		frame.cpuScopes.push_back({ std::string(name), GetCurrentThreadId(), durationMs });
	}

	Profiler* Profiler::GetActiveProfiler()
	{
		return g_activeProfiler;
	}

	ProfilerScope::ProfilerScope(std::string_view name)
		: m_name(name)
		, m_startTime(std::chrono::steady_clock::now())
	{
	}

	ProfilerScope::~ProfilerScope()
	{
		Profiler* profiler = Profiler::GetActiveProfiler();
		if (profiler == nullptr)
		{
			return;
		}

		const auto endTime = std::chrono::steady_clock::now();
		const double durationMs = std::chrono::duration<double, std::milli>(endTime - m_startTime).count();
		profiler->RecordCpuScope(m_name, durationMs);
	}
}
