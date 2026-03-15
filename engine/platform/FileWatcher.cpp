#include "engine/platform/FileWatcher.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#endif

namespace engine::platform
{
#if defined(_WIN32)
	struct FileWatcher::Impl
	{
		HANDLE hDir = INVALID_HANDLE_VALUE;
		HANDLE hEvent = nullptr;   // OVERLAPPED completion event
		HANDLE hStopEvent = nullptr;
		OVERLAPPED overlapped{};
		std::vector<uint8_t> buffer;
		bool pending = false;
		bool stopSignalled = false;

		~Impl()
		{
			Destroy();
		}

		void Destroy()
		{
			std::fprintf(stderr, "[FW] Impl::Destroy enter hDir=%p pending=%d\n", (void*)hDir, (int)pending); std::fflush(stderr);
			stopSignalled = true;
			if (hStopEvent)
			{
				SetEvent(hStopEvent);
			}
			if (hDir != INVALID_HANDLE_VALUE)
			{
				// Passer nullptr annule toutes les I/O en cours sur ce handle.
				// Ne passer &overlapped que si une I/O est réellement en cours (pending==true),
				// sinon UB sur certaines versions de Windows.
				CancelIoEx(hDir, pending ? &overlapped : nullptr);
				std::fprintf(stderr, "[FW] Impl::Destroy CancelIoEx done\n"); std::fflush(stderr);
				CloseHandle(hDir);
				std::fprintf(stderr, "[FW] Impl::Destroy hDir closed\n"); std::fflush(stderr);
				hDir = INVALID_HANDLE_VALUE;
			}
			if (hEvent)
			{
				CloseHandle(hEvent);
				std::fprintf(stderr, "[FW] Impl::Destroy hEvent closed\n"); std::fflush(stderr);
				hEvent = nullptr;
			}
			if (hStopEvent)
			{
				CloseHandle(hStopEvent);
				std::fprintf(stderr, "[FW] Impl::Destroy hStopEvent closed\n"); std::fflush(stderr);
				hStopEvent = nullptr;
			}
			pending = false;
		}
	};

	namespace
	{
		std::wstring Utf8ToWide(const std::string& utf8)
		{
			if (utf8.empty())
			{
				return {};
			}
			int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
			if (n <= 0)
			{
				return {};
			}
			std::wstring out(static_cast<size_t>(n), 0);
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), n);
			return out;
		}
	}

	bool FileWatcher::StartReadDirectoryChanges(Impl* impl)
	{
		std::fprintf(stderr, "[FW] StartRDC enter hDir=%p\n", (void*)impl->hDir); std::fflush(stderr);
		if (impl->hDir == INVALID_HANDLE_VALUE || impl->stopSignalled)
		{
			return false;
		}
		impl->overlapped = OVERLAPPED{};
		impl->overlapped.hEvent = impl->hEvent;
		impl->pending = true;
		BOOL ok = ReadDirectoryChangesW(
			impl->hDir,
			impl->buffer.data(),
			static_cast<DWORD>(impl->buffer.size()),
			FALSE,
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
			nullptr,
			&impl->overlapped,
			nullptr
		);
		std::fprintf(stderr, "[FW] StartRDC ReadDirectoryChangesW ok=%d\n", (int)ok); std::fflush(stderr);
		if (!ok)
		{
			impl->pending = false;
			return false;
		}
		return true;
	}
#endif

	FileWatcher::~FileWatcher()
	{
		Destroy();
#if defined(_WIN32)
		delete m_impl;
		m_impl = nullptr;
#endif
	}

	void FileWatcher::Init(const std::string& directory)
	{
		std::fprintf(stderr, "[FW] Init enter dir='%s'\n", directory.c_str()); std::fflush(stderr);
#if defined(_WIN32)
		if (m_impl && m_impl->hDir != INVALID_HANDLE_VALUE)
		{
			return;
		}
		if (!m_impl)
		{
			m_impl = new Impl();
		}
		m_impl->Destroy();

		LOG_INFO(Core, "[FileWatcher] Init: opening directory '{}'", directory);

		std::wstring wpath = Utf8ToWide(directory);
		if (wpath.empty())
		{
			return;
		}

		m_impl->hDir = CreateFileW(
			wpath.c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			nullptr
		);
		std::fprintf(stderr, "[FW] CreateFileW r=%p\n", (void*)m_impl->hDir); std::fflush(stderr);
		if (m_impl->hDir == INVALID_HANDLE_VALUE)
		{
			LOG_ERROR(Core, "[FileWatcher] Init FAILED: CreateFileW returned INVALID_HANDLE_VALUE"
				" (error={})", static_cast<uint32_t>(GetLastError()));
			return;
		}
		LOG_INFO(Core, "[FileWatcher] Init: directory opened OK (hDir={})",
			reinterpret_cast<uintptr_t>(m_impl->hDir));

		// Auto-reset : le kernel remet l'événement à non-signalé après WaitForMultipleObjects.
		m_impl->hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		std::fprintf(stderr, "[FW] CreateEventW hEvent r=%p\n", (void*)m_impl->hEvent); std::fflush(stderr);
		if (!m_impl->hEvent)
		{
			LOG_ERROR(Core, "[FileWatcher] Init FAILED: CreateEventW(hEvent) failed"
				" (error={})", static_cast<uint32_t>(GetLastError()));
			m_impl->Destroy();
			return;
		}

		m_impl->hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		std::fprintf(stderr, "[FW] CreateEventW hStopEvent r=%p\n", (void*)m_impl->hStopEvent); std::fflush(stderr);
		if (!m_impl->hStopEvent)
		{
			LOG_ERROR(Core, "[FileWatcher] Init FAILED: CreateEventW(hStopEvent) failed"
				" (error={})", static_cast<uint32_t>(GetLastError()));
			m_impl->Destroy();
			return;
		}

		m_impl->buffer.resize(4096);
		m_impl->stopSignalled = false;
		LOG_INFO(Core, "[FileWatcher] Init OK (dir='{}', hDir={}, hEvent={}, hStopEvent={})",
			directory,
			reinterpret_cast<uintptr_t>(m_impl->hDir),
			reinterpret_cast<uintptr_t>(m_impl->hEvent),
			reinterpret_cast<uintptr_t>(m_impl->hStopEvent));
		std::fprintf(stderr, "[FW] Init OK\n"); std::fflush(stderr);
#else
		(void)directory;
#endif
	}

	bool FileWatcher::WaitForChange(uint32_t timeoutMs)
	{
#if defined(_WIN32)
		std::fprintf(stderr, "[FW] WaitForChange timeout=%u pending=%d\n", timeoutMs, m_impl ? (int)m_impl->pending : -1); std::fflush(stderr);
		if (!m_impl || m_impl->hDir == INVALID_HANDLE_VALUE)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			return false;
		}

		if (!m_impl->pending)
		{
			if (!StartReadDirectoryChanges(m_impl))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				return false;
			}
		}

		HANDLE handles[2] = { m_impl->hStopEvent, m_impl->hEvent };
		DWORD wait = WaitForMultipleObjectsEx(2, handles, FALSE, timeoutMs, FALSE);
		std::fprintf(stderr, "[FW] WaitForMultipleObjects wait=%lu\n", (unsigned long)wait); std::fflush(stderr);

		if (wait == WAIT_OBJECT_0)
		{
			return false;
		}
		if (wait == WAIT_OBJECT_0 + 1)
		{
			m_impl->pending = false;
			ResetEvent(m_impl->hEvent);          // défense en profondeur
			StartReadDirectoryChanges(m_impl);
			return true;
		}
		return false;
#else
		(void)timeoutMs;
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
		return false;
#endif
	}

	void FileWatcher::Destroy()
	{
		std::fprintf(stderr, "[FW] Destroy enter\n"); std::fflush(stderr);
#if defined(_WIN32)
		if (m_impl)
		{
			m_impl->Destroy();
		}
#endif
		std::fprintf(stderr, "[FW] Destroy OK\n"); std::fflush(stderr);
	}
}
