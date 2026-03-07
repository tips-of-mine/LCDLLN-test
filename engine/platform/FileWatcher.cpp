#include "engine/platform/FileWatcher.h"

#include <chrono>
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
			stopSignalled = true;
			if (hStopEvent)
			{
				SetEvent(hStopEvent);
			}
			if (hDir != INVALID_HANDLE_VALUE)
			{
				CancelIoEx(hDir, &overlapped);
				CloseHandle(hDir);
				hDir = INVALID_HANDLE_VALUE;
			}
			if (hEvent)
			{
				CloseHandle(hEvent);
				hEvent = nullptr;
			}
			if (hStopEvent)
			{
				CloseHandle(hStopEvent);
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
		if (m_impl->hDir == INVALID_HANDLE_VALUE)
		{
			return;
		}

		m_impl->hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		m_impl->hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!m_impl->hEvent || !m_impl->hStopEvent)
		{
			m_impl->Destroy();
			return;
		}

		m_impl->buffer.resize(4096);
		m_impl->stopSignalled = false;
#else
		(void)directory;
#endif
	}

	bool FileWatcher::WaitForChange(uint32_t timeoutMs)
	{
#if defined(_WIN32)
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

		if (wait == WAIT_OBJECT_0)
		{
			return false;
		}
		if (wait == WAIT_OBJECT_0 + 1)
		{
			m_impl->pending = false;
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
#if defined(_WIN32)
		if (m_impl)
		{
			m_impl->Destroy();
		}
#endif
	}
}
