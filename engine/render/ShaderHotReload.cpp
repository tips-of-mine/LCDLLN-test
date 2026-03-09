#include "engine/render/ShaderHotReload.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <filesystem>

namespace engine::render
{
	ShaderHotReload::ShaderHotReload()
	{
		std::fprintf(stderr, "[SHR] ctor this=%p mutex=%p\n", (void*)this, (void*)&m_pendingMutex); std::fflush(stderr);
		m_compiler.LocateCompiler();
		// m_worker = std::thread(&ShaderHotReload::WorkerThread, this);
	}

	ShaderHotReload::~ShaderHotReload()
	{
		m_workerRunning = false;
		m_watcher.Destroy();
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
		std::fprintf(stderr, "[POLL] debut\n"); std::fflush(stderr);

		if (!m_watcherInited)
		{
			std::fprintf(stderr, "[POLL] init watcher\n"); std::fflush(stderr);
			std::filesystem::path contentPath(config.GetString("paths.content", "game/data"));
			std::error_code ec;
			contentPath = std::filesystem::absolute(contentPath, ec);
			std::fprintf(stderr, "[POLL] absolute path=%s ec=%d\n", contentPath.string().c_str(), (int)ec.value()); std::fflush(stderr);

			if (!ec)
			{
				std::fprintf(stderr, "[POLL] avant watcher.Init\n"); std::fflush(stderr);
				m_watcher.Init(contentPath.string());

				std::fprintf(stderr, "[POLL] watcher.Init OK\n"); std::fflush(stderr);
				m_watcherInited = true;
			}
		}
		std::fprintf(stderr, "[POLL] loop watched=%zu\n", m_watched.size()); std::fflush(stderr);

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
		std::fprintf(stderr, "[POLL] done\n"); std::fflush(stderr);
	}

	void ShaderHotReload::ApplyPending(ShaderCache& cache)
	{
		std::fprintf(stderr, "[AP] debut\n"); std::fflush(stderr);
		std::vector<PendingReloadResult> results;

		std::fprintf(stderr, "[AP] avant lock, this=%p mutex=%p\n", (void*)this, (void*)&m_pendingMutex); std::fflush(stderr);
		{
			std::lock_guard lock(m_pendingMutex);
			std::fprintf(stderr, "[AP] lock OK\n"); std::fflush(stderr);

			results.swap(m_pending);
			std::fprintf(stderr, "[AP] swap OK size=%zu\n", results.size()); std::fflush(stderr);
		}
		std::fprintf(stderr, "[AP] loop\n"); std::fflush(stderr);
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
		std::fprintf(stderr, "[AP] done\n"); std::fflush(stderr);
	}

	void ShaderHotReload::WorkerThread()
	{
		while (m_workerRunning)
		{
			CompileRequest req;
			bool haveRequest = false;
			{
				std::lock_guard lock(m_queueMutex);
				if (!m_compileQueue.empty())
				{
					req = std::move(m_compileQueue.front());
					m_compileQueue.pop();
					haveRequest = true;
				}
			}
			if (!haveRequest)
			{
				if (m_watcherInited)           // ← AJOUTE CETTE VÉRIFICATION
					m_watcher.WaitForChange(500);
				else
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
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
