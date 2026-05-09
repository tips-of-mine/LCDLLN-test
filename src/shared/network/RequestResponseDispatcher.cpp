// Implémentation du dispatcher requête/réponse (voir RequestResponseDispatcher.h).
// Pump() est le point central : il dépile les événements NetClient, identifie chaque paquet
// par son request_id, résout les requêtes en attente ou route les push, puis expire les timeouts.
// Les callbacks sont toujours appelés depuis Pump() — jamais depuis le thread IO.
#include "src/shared/network/RequestResponseDispatcher.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include "src/shared/core/Log.h"

#include <chrono>
#include <mutex>

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
		if (requestId == 0u) // 0 est réservé aux push serveur ; on saute cette valeur.
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
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		{
			std::lock_guard lock(m_mutex);
			m_pending[requestId] = PendingEntry{ deadline, std::move(onResponse) };
		}
		if (!m_client->Send(std::span<const uint8_t>(builder.DataPtr(), builder.DataSize())))
		{
			{
				std::lock_guard lock(m_mutex);
				m_pending.erase(requestId);
			}
			LOG_WARN(Net, "[RequestResponseDispatcher] SendRequest FAILED: send");
			return false;
		}
		return true;
	}

	/// Consomme tous les paquets reçus depuis le dernier appel, dispatche les callbacks et expire les timeouts.
	/// Ordre de traitement pour chaque paquet :
	///   1. Paquet ERROR  → ErrorHandler puis callback pending avec payload vide (jamais timeout=true).
	///   2. request_id > 0 → résout la requête en attente avec le payload de réponse.
	///   3. request_id == 0 → push serveur routé vers PushHandler.
	/// Après le traitement des paquets, les entrées dont la deadline est dépassée reçoivent timeout=true.
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
					// Ne pas transmettre le corps du paquet ERROR comme réponse valide (ex. ticket shard).
					if (cb)
						cb(requestId, false, {});
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

		// Expire les requêtes dont la deadline est dépassée : callback avec timeout=true, payload vide.
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
