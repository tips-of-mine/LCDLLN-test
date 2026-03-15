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

				std::fprintf(stderr, "[POLL] watcher.Init retourne\n"); std::fflush(stderr);
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
		// Worker never started => nothing can be in m_pending; skip lock to avoid
		// potential crash on mutex (e.g. MSVC Release / SEH).
		if (!m_worker.joinable())
		{
			return;
		}
		std::vector<PendingReloadResult> results;
		std::fprintf(stderr, "[AP] avant lock pendingMutex\n"); std::fflush(stderr);
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			std::fprintf(stderr, "[AP] lock OK pending=%zu\n", m_pending.size()); std::fflush(stderr);
#if !defined(NDEBUG)
			LOG_DEBUG(Render, "[Engine] ApplyPending: mutex acquired, {} pending ops", m_pending.size());
#endif
			if (m_pending.empty())
			{
				std::fprintf(stderr, "[AP] pending empty, return (%s)\n", m_worker.joinable() ? "worker" : "no worker"); std::fflush(stderr);
				return;
			}
			results.swap(m_pending);
		}

		for (PendingReloadResult& r : results)
		{
			if (r.spirv.has_value())
				cache.Set(r.cacheKey, std::move(r.spirv.value()));
		}
		std::fprintf(stderr, "[AP] done (%s)\n", m_worker.joinable() ? "worker" : "no worker"); std::fflush(stderr);
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
