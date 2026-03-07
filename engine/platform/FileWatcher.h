#pragma once

#include <cstdint>
#include <string>

namespace engine::platform
{
	/// Event-driven directory file watcher. Used by ShaderHotReload to avoid polling.
	/// Windows: ReadDirectoryChangesW + OVERLAPPED; non-Windows: fallback sleep (no native events).
	class FileWatcher final
	{
	public:
		FileWatcher() = default;
		~FileWatcher();

		FileWatcher(const FileWatcher&) = delete;
		FileWatcher& operator=(const FileWatcher&) = delete;

		/// Opens the directory for watching. Idempotent; safe to call when already inited.
		void Init(const std::string& directory);

		/// Blocks up to timeoutMs. Returns true if a change was detected, false on timeout or stop.
		/// If not inited, blocks briefly and returns false.
		bool WaitForChange(uint32_t timeoutMs);

		/// Releases resources and signals any waiting WaitForChange to return false.
		void Destroy();

	private:
		struct Impl;
		Impl* m_impl = nullptr;
	};

}
