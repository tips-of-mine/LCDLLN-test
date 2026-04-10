#pragma once

/// STAB.14 / client network: sur MSVC, std::mutex repose sur SRWLOCK et peut provoquer un SEH
/// 0xC0000005 dans certaines configs CRT/ABI (voir AuthUi STAB.14, Engine STAB.7). CRITICAL_SECTION
/// évite ce chemin. Sur les autres plateformes, alias de std::mutex.

#include <mutex>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

namespace engine::platform
{
	class StableMutex final
	{
	public:
		StableMutex() { InitializeCriticalSection(&m_cs); }
		~StableMutex() { DeleteCriticalSection(&m_cs); }
		StableMutex(const StableMutex&) = delete;
		StableMutex& operator=(const StableMutex&) = delete;

		void lock() { EnterCriticalSection(&m_cs); }
		void unlock() { LeaveCriticalSection(&m_cs); }
		bool try_lock() { return TryEnterCriticalSection(&m_cs) != 0; }

	private:
		CRITICAL_SECTION m_cs{};
	};
}
#else
namespace engine::platform
{
	using StableMutex = std::mutex;
}
#endif
