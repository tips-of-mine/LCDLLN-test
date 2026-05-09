#include "engine/client/ProfilerHud.h"

#include "engine/core/Log.h"

#include <algorithm>

namespace engine::client
{
	namespace
	{
		inline constexpr size_t kMaxGpuLines = 6;
		inline constexpr size_t kMaxCpuLines = 4;
	}

	ProfilerHudPresenter::~ProfilerHudPresenter()
	{
		Shutdown();
	}

	bool ProfilerHudPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ProfilerHudPresenter] Init ignored: already initialized");
			return true;
		}

		m_initialized = true;
		m_state = {};
		RebuildDebugText();
		LOG_INFO(Core, "[ProfilerHudPresenter] Init OK");
		return true;
	}

	void ProfilerHudPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[ProfilerHudPresenter] Destroyed");
	}

	bool ProfilerHudPresenter::ApplySnapshot(const engine::core::ProfilerFrameSnapshot& snapshot)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ProfilerHudPresenter] ApplySnapshot FAILED: presenter not initialized");
			return false;
		}

		m_state.cpuTotalMs = snapshot.cpuTotalMs;
		m_state.gpuTotalMs = snapshot.gpuTotalMs;
		m_state.lines.clear();

		std::vector<engine::core::GpuProfileSample> gpuPasses = snapshot.gpuPasses;
		std::sort(gpuPasses.begin(), gpuPasses.end(), [](const auto& lhs, const auto& rhs)
		{
			return lhs.durationMs > rhs.durationMs;
		});
		for (size_t i = 0; i < gpuPasses.size() && i < kMaxGpuLines; ++i)
		{
			if (!gpuPasses[i].valid)
			{
				continue;
			}
			m_state.lines.push_back({ gpuPasses[i].name, gpuPasses[i].durationMs, true });
		}

		std::vector<engine::core::CpuProfileSample> cpuScopes = snapshot.cpuScopes;
		std::sort(cpuScopes.begin(), cpuScopes.end(), [](const auto& lhs, const auto& rhs)
		{
			return lhs.durationMs > rhs.durationMs;
		});
		for (size_t i = 0; i < cpuScopes.size() && i < kMaxCpuLines; ++i)
		{
			m_state.lines.push_back({ cpuScopes[i].name, cpuScopes[i].durationMs, false });
		}

		m_state.visible = true;
		RebuildDebugText();
		return true;
	}

	void ProfilerHudPresenter::RebuildDebugText()
	{
		m_state.debugText.clear();
		m_state.debugText += "[Profiler]\n";
		m_state.debugText += "cpu_total_ms=" + std::to_string(m_state.cpuTotalMs);
		m_state.debugText += " gpu_total_ms=" + std::to_string(m_state.gpuTotalMs);
		m_state.debugText += "\n";
		for (const ProfilerHudLine& line : m_state.lines)
		{
			m_state.debugText += line.gpu ? "gpu " : "cpu ";
			m_state.debugText += line.label;
			m_state.debugText += " ";
			m_state.debugText += std::to_string(line.durationMs);
			m_state.debugText += " ms\n";
		}
	}
}
