#include "engine/render/ShaderHotReload.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <filesystem>

namespace engine::render
{
	ShaderHotReload::ShaderHotReload()
	{
		// Do not call LOG_* here: Engine constructs this before Log::Init() runs; logging may crash or be dropped.
		(void)m_compiler.LocateCompiler();
		// m_worker = std::thread(&ShaderHotReload::WorkerThread, this);
	}

	ShaderHotReload::~ShaderHotReload()
	{
		m_workerRunning = false;
		if (m_watcherInited)
		{
			m_watcher.Destroy();
			m_watcherInited = false;
		}
		if (m_worker.joinable())
		{
			m_worker.join();
		}
		// Avoid LOG in destructor: during stack unwinding the log system may be unusable.
		// LOG_INFO(Render, "[ShaderHotReload] Destroyed");
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
		LOG_INFO(Render, "[POLL] debut");

		if (!m_watcherInited)
		{
			LOG_WARN(Render, "[POLL] init watcher");
			std::filesystem::path contentPath(config.GetString("paths.content", "game/data"));
			std::error_code ec;
			contentPath = std::filesystem::absolute(contentPath, ec);
			LOG_DEBUG(Render, "[POLL] absolute path={} ec={}", contentPath.string().c_str(), (int)ec.value());

			if (!ec)
			{
				LOG_INFO(Render, "[POLL] avant watcher.Init");
				m_watcher.Init(contentPath.string());

				LOG_INFO(Render, "[POLL] watcher.Init retourne");
				m_watcherInited = true;
			}
		}
		LOG_DEBUG(Render, "[POLL] loop watched={}", m_watched.size());

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
		LOG_INFO(Render, "[POLL] done");
	}

	void ShaderHotReload::ApplyPending(ShaderCache& cache)
	{
		// Worker never started => nothing can be in m_pending; skip lock to avoid
		// potential crash on mutex (e.g. MSVC Release / SEH).
		if (!m_worker.joinable())
		{
			return;
		}
		std::vector<PendingReloadResult> results;
		LOG_DEBUG(Render, "[AP] avant lock pendingMutex");
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
#if !defined(NDEBUG)
			LOG_DEBUG(Render, "[Engine] ApplyPending: mutex acquired, {} pending ops", m_pending.size());
#endif
			if (m_pending.empty())
			{
				LOG_DEBUG(Render, "[AP] pending empty, return ({})", m_worker.joinable() ? "worker" : "no worker");
				return;
			}
			results.swap(m_pending);
		}

		for (PendingReloadResult& r : results)
		{
			if (r.spirv.has_value())
				cache.Set(r.cacheKey, std::move(r.spirv.value()));
		}
		LOG_INFO(Render, "[AP] done ({})", m_worker.joinable() ? "worker" : "no worker");
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
