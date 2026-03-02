#include "engine/render/ShaderHotReload.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <filesystem>

namespace engine::render
{
	ShaderHotReload::ShaderHotReload()
	{
		m_compiler.LocateCompiler();
		m_worker = std::thread(&ShaderHotReload::WorkerThread, this);
	}

	ShaderHotReload::~ShaderHotReload()
	{
		m_workerRunning = false;
		if (m_worker.joinable())
		{
			m_worker.join();
		}
	}

	void ShaderHotReload::Watch(std::string_view relativePath, ShaderStage stage, std::string_view defines)
	{
		WatchedShader w;
		w.relativePath = std::string(relativePath);
		w.stage = stage;
		w.defines = std::string(defines);
		m_watched.push_back(std::move(w));
	}

	void ShaderHotReload::Poll(const engine::core::Config& config)
	{
		for (WatchedShader& w : m_watched)
		{
			std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(config, w.relativePath);
			std::error_code ec;
			if (!engine::platform::FileSystem::Exists(fullPath))
			{
				continue;
			}
			auto writeTime = std::filesystem::last_write_time(fullPath, ec);
			if (ec)
			{
				continue;
			}
			if (!w.lastWriteTime.has_value())
			{
				w.lastWriteTime = writeTime;
				continue;
			}
			if (writeTime > w.lastWriteTime.value())
			{
				w.lastWriteTime = writeTime;
				CompileRequest req;
				req.fullPath = fullPath;
				req.relativePath = w.relativePath;
				req.stage = w.stage;
				req.defines = w.defines;
				{
					std::lock_guard lock(m_queueMutex);
					m_compileQueue.push(std::move(req));
				}
			}
		}
	}

	void ShaderHotReload::ApplyPending(ShaderCache& cache)
	{
		std::vector<PendingReloadResult> results;
		{
			std::lock_guard lock(m_pendingMutex);
			results.swap(m_pending);
		}
		for (PendingReloadResult& r : results)
		{
			if (r.spirv.has_value())
			{
				cache.Set(r.cacheKey, std::move(r.spirv.value()));
				LOG_INFO(Render, "Shader hot-reload OK: {}", r.cacheKey);
			}
			else
			{
				LOG_WARN(Render, "Shader hot-reload failed (fallback kept): {} - {}", r.cacheKey, r.errorMessage);
			}
		}
	}

	void ShaderHotReload::WorkerThread()
	{
		while (m_workerRunning)
		{
			CompileRequest req;
			{
				std::lock_guard lock(m_queueMutex);
				if (m_compileQueue.empty())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(16));
					continue;
				}
				req = std::move(m_compileQueue.front());
				m_compileQueue.pop();
			}
			std::string key = ShaderCache::MakeKey(req.relativePath, req.defines);
			auto result = m_compiler.CompileGlslToSpirv(req.fullPath, req.stage);
			PendingReloadResult pending;
			pending.cacheKey = key;
			if (result.has_value())
			{
				pending.spirv = std::move(result);
			}
			else
			{
				pending.errorMessage = std::string(m_compiler.GetLastErrors());
			}
			{
				std::lock_guard lock(m_pendingMutex);
				m_pending.push_back(std::move(pending));
			}
		}
	}
}
