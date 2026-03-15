#pragma once

#include "engine/network/NetErrorCode.h"
#include "engine/network/ProtocolV1Constants.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::network
{
	class NetClient;

	/// Callback for a request response. Invoked on the thread that calls Pump() (main thread). \a timeout is true on 5s timeout; \a payload is empty then.
	using RequestResponseCallback = std::function<void(uint32_t requestId, bool timeout, std::vector<uint8_t> payload)>;

	/// Callback for server push (request_id == 0). Invoked on the thread that calls Pump() (main thread).
	using PushHandler = std::function<void(uint16_t opcode, const uint8_t* payload, size_t payloadSize)>;

	/// Callback for ERROR packet (opcode ERROR). Invoked on the thread that calls Pump(). Optional; used for log + future UI hook.
	using ErrorHandler = std::function<void(uint32_t requestId, NetErrorCode errorCode, std::string_view message)>;

	/// Request/response dispatcher: matches responses by request_id, routes server push (request_id == 0) to push handler.
	/// Pending requests have configurable timeout (default 5s). Call Pump() from main thread only; callbacks are dispatched there (not on IO thread).
	class RequestResponseDispatcher
	{
	public:
		/// \param client Non-null. Must outlive the dispatcher.
		explicit RequestResponseDispatcher(NetClient* client);
		~RequestResponseDispatcher();

		/// Set handler for server push (request_id == 0). Optional.
		void SetPushHandler(PushHandler handler);
		/// Set handler for ERROR packets (log + future UI). Optional.
		void SetErrorHandler(ErrorHandler handler);

		/// Send a request; \a onResponse is invoked from Pump() with the response payload or with timeout=true after \a timeoutMs (default 5000). Returns false if send failed.
		bool SendRequest(uint16_t opcode, std::span<const uint8_t> payload, RequestResponseCallback onResponse, uint32_t timeoutMs = 5000u);

		/// Process received packets and dispatch callbacks. Call from main thread only. Cleans expired pending (TIMEOUT).
		void Pump();

	private:
		NetClient* m_client;
		std::atomic<uint32_t> m_nextRequestId{ 1u };
		std::mutex m_mutex;
		struct PendingEntry
		{
			std::chrono::steady_clock::time_point deadline;
			RequestResponseCallback callback;
		};
		std::unordered_map<uint32_t, PendingEntry> m_pending;
		PushHandler m_pushHandler;
		ErrorHandler m_errorHandler;
	};
}
