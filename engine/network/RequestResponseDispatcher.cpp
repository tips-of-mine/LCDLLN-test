#include "engine/network/RequestResponseDispatcher.h"
#include "engine/network/NetClient.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/PacketView.h"

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
		if (!builder.Finalize(opcode, 0, requestId, 0, payload.size()))
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
}
