#pragma once

#include "engine/core/Profiler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	/// One text line shown in the MVP profiler overlay.
	struct ProfilerHudLine
	{
		std::string label;
		double durationMs = 0.0;
		bool gpu = false;
	};

	/// Fully resolved profiler HUD state ready for a debug overlay renderer.
	struct ProfilerHudState
	{
		double cpuTotalMs = 0.0;
		double gpuTotalMs = 0.0;
		std::vector<ProfilerHudLine> lines;
		std::string debugText;
		bool visible = false;
	};

	/// Builds a minimal profiler overlay from CPU/GPU frame snapshots.
	class ProfilerHudPresenter final
	{
	public:
		/// Construct an uninitialized presenter.
		ProfilerHudPresenter() = default;

		/// Release presenter resources.
		~ProfilerHudPresenter();

		/// Initialize the profiler HUD presenter.
		bool Init();

		/// Shutdown the presenter and clear retained overlay state.
		void Shutdown();

		/// Rebuild overlay lines from one profiler frame snapshot.
		bool ApplySnapshot(const engine::core::ProfilerFrameSnapshot& snapshot);

		/// Return the current immutable overlay state.
		const ProfilerHudState& GetState() const { return m_state; }

		/// Return true when the presenter has been initialized successfully.
		bool IsInitialized() const { return m_initialized; }

	private:
		/// Rebuild the multi-line text dump used by the debug overlay.
		void RebuildDebugText();

		ProfilerHudState m_state{};
		bool m_initialized = false;
	};
}
