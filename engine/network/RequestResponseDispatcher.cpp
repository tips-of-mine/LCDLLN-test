#include "engine/network/RequestResponseDispatcher.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/NetClient.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"

#include "engine/core/Log.h"

#include <chrono>

namespace engine::network
{
	RequestResponseDispatcher::RequestResponseDispatcher(NetClient* client)
		: m_client(client)
	{
		if (m_client == nullptr)
		{
			LOG_ERROR(Net, "[RequestResponseDispatcher] Init FAILED: client is null");
			return;
		}
		LOG_INFO(Net, "[RequestResponseDispatcher] Created");
	}

	RequestResponseDispatcher::~RequestResponseDispatcher()
	{
		LOG_INFO(Net, "[RequestResponseDispatcher] Destroyed");
	}

	void RequestResponseDispatcher::SetPushHandler(PushHandler handler)
	{
		std::lock_guard lock(m_mutex);
		m_pushHandler = std::move(handler);
	}

	void RequestResponseDispatcher::SetErrorHandler(ErrorHandler handler)
	{
		std::lock_guard lock(m_mutex);
		m_errorHandler = std::move(handler);
	}

	bool RequestResponseDispatcher::SendRequest(uint16_t opcode, std::span<const uint8_t> payload, RequestResponseCallback onResponse, uint32_t timeoutMs)
	{
		if (m_client == nullptr)
		{
			LOG_WARN(Net, "[RequestResponseDispatcher] SendRequest ignored: client null");
			return false;
		}
		uint32_t requestId = m_nextRequestId.fetch_add(1);
		if (requestId == 0u)
			requestId = m_nextRequestId.fetch_add(1);

		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (w.Remaining() < payload.size())
		{
			LOG_WARN(Net, "[RequestResponseDispatcher] SendRequest FAILED: payload too large");
			return false;
		}
		if (!w.WriteBytes(payload.data(), payload.size()))
		{
			LOG_WARN(Net, "[RequestResponseDispatcher] SendRequest FAILED: write payload");
			return false;
		}
		const uint64_t sessionId = m_sessionId.load();
		if (!builder.Finalize(opcode, 0, requestId, sessionId, payload.size()))
		{
			LOG_WARN(Net, "[RequestResponseDispatcher] SendRequest FAILED: finalize");
			return false;
		}
		if (!m_client->Send(std::span<const uint8_t>(builder.DataPtr(), builder.DataSize())))
		{
			LOG_WARN(Net, "[RequestResponseDispatcher] SendRequest FAILED: send");
			return false;
		}
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		{
			std::lock_guard lock(m_mutex);
			m_pending[requestId] = PendingEntry{ deadline, std::move(onResponse) };
		}
		return true;
	}

	void RequestResponseDispatcher::Pump()
	{
		if (m_client == nullptr)
			return;

		std::vector<NetClientEvent> events = m_client->PollEvents();
		auto now = std::chrono::steady_clock::now();

		for (NetClientEvent& ev : events)
		{
			if (ev.type != NetClientEventType::PacketReceived || ev.packet.size() < kProtocolV1HeaderSize)
				continue;

			PacketView view;
			PacketParseResult res = PacketView::Parse(ev.packet.data(), ev.packet.size(), view);
			if (res != PacketParseResult::Ok)
				continue;

			uint32_t requestId = view.RequestId();
			std::vector<uint8_t> payload(view.Payload(), view.Payload() + view.PayloadSize());

			if (view.Opcode() == kOpcodeError)
			{
				auto parsed = ParseErrorPayload(view.Payload(), view.PayloadSize());
				if (parsed)
				{
					LOG_WARN(Net, "[RequestResponseDispatcher] ERROR packet (request_id={}, code={}, message={})",
						requestId, static_cast<uint32_t>(parsed->errorCode), parsed->message.empty() ? "(none)" : parsed->message);
					ErrorHandler h;
					{
						std::lock_guard lock(m_mutex);
						h = m_errorHandler;
					}
					if (h)
						h(requestId, parsed->errorCode, std::string_view(parsed->message));
				}
				if (requestId != 0u)
				{
					RequestResponseCallback cb;
					{
						std::lock_guard lock(m_mutex);
						auto it = m_pending.find(requestId);
						if (it != m_pending.end())
						{
							cb = std::move(it->second.callback);
							m_pending.erase(it);
						}
					}
					if (cb)
						cb(requestId, false, std::move(payload));
				}
				continue;
			}

			if (requestId != 0u)
			{
				RequestResponseCallback cb;
				{
					std::lock_guard lock(m_mutex);
					auto it = m_pending.find(requestId);
					if (it != m_pending.end())
					{
						cb = std::move(it->second.callback);
						m_pending.erase(it);
					}
				}
				if (cb)
					cb(requestId, false, std::move(payload));
			}
			else
			{
				PushHandler h;
				{
					std::lock_guard lock(m_mutex);
					h = m_pushHandler;
				}
				if (h)
					h(view.Opcode(), view.Payload(), view.PayloadSize());
			}
		}

		// Clean expired pending (timeout)
		std::vector<std::pair<uint32_t, RequestResponseCallback>> timedOut;
		{
			std::lock_guard lock(m_mutex);
			for (auto it = m_pending.begin(); it != m_pending.end(); )
			{
				if (now >= it->second.deadline)
				{
					timedOut.emplace_back(it->first, std::move(it->second.callback));
					it = m_pending.erase(it);
				}
				else
					++it;
			}
		}
		for (auto& p : timedOut)
		{
			if (p.second)
				p.second(p.first, true, {});
		}
	}

	void RequestResponseDispatcher::SetHeartbeatInterval(int64_t intervalSec)
	{
		m_heartbeatIntervalSec = (intervalSec > 0) ? intervalSec : 30;
	}

	void RequestResponseDispatcher::SetSessionId(uint64_t sessionId)
	{
		m_sessionId.store(sessionId);
	}

	void RequestResponseDispatcher::TickHeartbeat()
	{
		if (m_client == nullptr)
			return;
		uint64_t sid = m_sessionId.load();
		if (sid == 0)
			return;
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastHeartbeatSent).count();
		if (elapsed < m_heartbeatIntervalSec)
			return;
		std::vector<uint8_t> pkt = BuildHeartbeatPacket(sid);
		if (pkt.empty())
			return;
		if (m_client->Send(std::span<const uint8_t>(pkt.data(), pkt.size())))
		{
			m_lastHeartbeatSent = now;
			LOG_DEBUG(Net, "[RequestResponseDispatcher] Heartbeat sent (session_id={})", sid);
		}
	}
}
