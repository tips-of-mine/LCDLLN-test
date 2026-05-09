#pragma once

#include <vulkan/vulkan_core.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core
{
	/// One recorded CPU profiling scope sample for the current frame.
	struct CpuProfileSample
	{
		std::string name;
		uint32_t threadId = 0;
		double durationMs = 0.0;
	};

	/// One recorded GPU profiling sample for one frame-graph pass.
	struct GpuProfileSample
	{
		std::string name;
		double durationMs = 0.0;
		bool valid = false;
	};

	/// Immutable snapshot consumed by the profiler HUD/debug overlay.
	struct ProfilerFrameSnapshot
	{
		uint64_t frameIndex = 0;
		double cpuTotalMs = 0.0;
		double gpuTotalMs = 0.0;
		std::vector<CpuProfileSample> cpuScopes;
		std::vector<GpuProfileSample> gpuPasses;
	};

	/// CPU + GPU profiler storing a ring buffer of frame snapshots.
	class Profiler final
	{
	public:
		/// Construct an uninitialized profiler.
		Profiler() = default;

		/// Shutdown the profiler on destruction.
		~Profiler();

		/// Initialize the profiler and create the GPU timestamp query pool.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight, uint32_t maxGpuPasses = 64);

		/// Release the GPU query pool and clear retained profiling history.
		void Shutdown(VkDevice device);

		/// Begin recording CPU samples for one frame into the ring buffer.
		bool BeginFrame(uint64_t frameIndex);

		/// Finalize CPU total time for the current frame.
		void EndFrame();

		/// Reset GPU queries for one frame slot before recording command buffer work.
		bool BeginGpuFrame(VkCommandBuffer cmd, uint32_t frameSlot);

		/// Write the start timestamp for one named GPU pass.
		bool BeginGpuPass(VkCommandBuffer cmd, uint32_t frameSlot, std::string_view passName);

		/// Write the end timestamp of the most recent GPU pass.
		bool EndGpuPass(VkCommandBuffer cmd, uint32_t frameSlot);

		/// Resolve finished GPU timestamps for one frame slot after its fence has signaled.
		bool ResolveGpuFrame(VkDevice device, uint32_t frameSlot);

		/// Record one CPU scope sample into the current frame snapshot.
		void RecordCpuScope(std::string_view name, double durationMs);

		/// Return true when the profiler has been initialized successfully.
		bool IsInitialized() const { return m_initialized; }

		/// Return the latest completed frame snapshot.
		const ProfilerFrameSnapshot& GetLatestSnapshot() const { return m_latestSnapshot; }

		/// Return the active profiler used by PROFILE_SCOPE macros, or nullptr.
		static Profiler* GetActiveProfiler();

	private:
		struct GpuPassRecord
		{
			std::string name;
			uint32_t startQuery = 0;
			uint32_t endQuery = 0;
		};

		struct GpuFrameSlot
		{
			std::vector<GpuPassRecord> passes;
			uint32_t usedQueryCount = 0;
			uint32_t historyIndex = 0;
			uint64_t frameIndex = 0;
			bool frameOpen = false;
		};

		std::vector<ProfilerFrameSnapshot> m_history;
		ProfilerFrameSnapshot m_latestSnapshot{};
		std::vector<GpuFrameSlot> m_gpuFrames;
		std::chrono::steady_clock::time_point m_frameStartTime{};
		VkQueryPool m_queryPool = VK_NULL_HANDLE;
		double m_timestampPeriodNs = 0.0;
		uint32_t m_historyIndex = 0;
		uint32_t m_framesInFlight = 0;
		uint32_t m_maxGpuPasses = 0;
		bool m_frameOpen = false;
		bool m_initialized = false;
	};

	/// RAII helper used by PROFILE_SCOPE macros to record one CPU scope duration.
	class ProfilerScope final
	{
	public:
		/// Start one CPU profiling scope using the active global profiler when available.
		explicit ProfilerScope(std::string_view name);

		/// Record the elapsed time into the active profiler on scope exit.
		~ProfilerScope();

		ProfilerScope(const ProfilerScope&) = delete;
		ProfilerScope& operator=(const ProfilerScope&) = delete;

	private:
		std::string m_name;
		std::chrono::steady_clock::time_point m_startTime{};
	};
}

#define PROFILE_SCOPE(name) ::engine::core::ProfilerScope profileScope_##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)
