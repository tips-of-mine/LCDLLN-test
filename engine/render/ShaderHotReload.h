#pragma once

#include "engine/render/ShaderCache.h"
#include "engine/render/ShaderCompiler.h"

#include "engine/core/Config.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine::render
{
	/// One watched shader file (relative path + stage + defines).
	struct WatchedShader
	{
		std::string relativePath;
		ShaderStage stage = ShaderStage::Vertex;
		std::string defines;
		std::optional<std::filesystem::file_time_type> lastWriteTime;
	};

	/// Pending reload result (success: SPIR-V; failure: error message, cache unchanged = fallback).
	struct PendingReloadResult
	{
		std::string cacheKey;
		std::optional<std::vector<uint32_t>> spirv;
		std::string errorMessage;
	};

	/// File watcher + hot-reload: poll mtime, compile in worker thread, apply to cache without frame freeze.
	class ShaderHotReload final
	{
	public:
		ShaderHotReload();
		~ShaderHotReload();

		ShaderHotReload(const ShaderHotReload&) = delete;
		ShaderHotReload& operator=(const ShaderHotReload&) = delete;

		/// Registers a shader to watch. Path is relative to paths.content (e.g. "shaders/foo.vert").
		void Watch(std::string_view relativePath, ShaderStage stage, std::string_view defines = {});

		/// Call each frame from main thread. Checks timestamps and enqueues changed shaders for compile.
		void Poll(const engine::core::Config& config);

		/// Applies pending compile results to the cache and logs success/failure. Call from main thread after Poll.
		void ApplyPending(ShaderCache& cache);

		/// Returns the compiler (e.g. to read GetLastErrors() after ApplyPending).
		ShaderCompiler& GetCompiler() { return m_compiler; }
		const ShaderCompiler& GetCompiler() const { return m_compiler; }

	private:
		void WorkerThread();

		struct CompileRequest
		{
			std::filesystem::path fullPath;
			std::string relativePath;
			ShaderStage stage = ShaderStage::Vertex;
			std::string defines;
		};

		ShaderCompiler m_compiler;
		std::vector<WatchedShader> m_watched;
		std::mutex m_queueMutex;
		std::queue<CompileRequest> m_compileQueue;
		std::mutex m_pendingMutex;
		std::vector<PendingReloadResult> m_pending;
		std::thread m_worker;
		std::atomic<bool> m_workerRunning{ true };
	};
}
