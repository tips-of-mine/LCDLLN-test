#include "engine/network/MasterShardClientFlow.h"
#include "engine/network/NetClient.h"
#include "engine/network/RequestResponseDispatcher.h"
#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/ServerListPayloads.h"
#include "engine/network/ShardTicketPayloads.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/network/PacketView.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace engine::network
{
	namespace
	{
		void ParseEndpoint(const std::string& endpoint, std::string& host, uint16_t& port)
		{
			size_t pos = endpoint.rfind(':');
			if (pos == std::string::npos)
			{
				host = endpoint.empty() ? "localhost" : endpoint;
				port = 3841;
				return;
			}
			host = endpoint.substr(0, pos);
			try { port = static_cast<uint16_t>(std::stoul(endpoint.substr(pos + 1))); }
			catch (...) { port = 3841; }
		}

		bool WaitConnected(NetClient* c, uint32_t timeoutMs)
		{
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (std::chrono::steady_clock::now() < deadline)
			{
				auto events = c->PollEvents();
				for (const auto& ev : events)
				{
					if (ev.type == NetClientEventType::Connected)
						return true;
					if (ev.type == NetClientEventType::Disconnected)
					{
						LOG_ERROR(Net, "[MasterShardClientFlow] Disconnected before connected: {}", ev.reason);
						return false;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			LOG_ERROR(Net, "[MasterShardClientFlow] Connect timeout ({} ms)", timeoutMs);
			return false;
		}

	}

	void MasterShardClientFlow::SetMasterAddress(std::string host, uint16_t port)
	{
		m_masterHost = std::move(host);
		m_masterPort = port;
	}

	void MasterShardClientFlow::SetCredentials(std::string login, std::string client_hash)
	{
		m_login = std::move(login);
		m_clientHash = std::move(client_hash);
	}

	MasterShardFlowResult MasterShardClientFlow::Run(NetClient* masterClient)
	{
		LOG_DEBUG(Net, "[FLOW] Run start host={} port={}", m_masterHost.c_str(), (unsigned)m_masterPort);
		MasterShardFlowResult result;
		if (!masterClient || m_masterHost.empty())
		{
			result.errorMessage = "Master address or client not set";
			LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}
		if (m_login.empty())
		{
			result.errorMessage = "Credentials not set";
			LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}

		LOG_INFO(Net, "[MasterShardClientFlow] Connecting to Master {}:{}", m_masterHost, m_masterPort);
		masterClient->Connect(m_masterHost, m_masterPort);
		bool connected = WaitConnected(masterClient, m_timeoutMs + 2000u);
		LOG_INFO(Net, "[FLOW] Master connected={}", (int)connected);
		if (!connected)
		{
			result.errorMessage = "Master connect timeout or disconnected";
			return result;
		}
		LOG_INFO(Net, "[MasterShardClientFlow] Master connected, sending AUTH");
		LOG_DEBUG(Net, "[FLOW] avant AUTH login='{}'", m_login.c_str());
		RequestResponseDispatcher disp(masterClient);
		std::vector<uint8_t> authResponsePayload;
		bool authTimeout = true;
		{
			auto authPayload = BuildAuthRequestPayload(m_login, m_clientHash);
			bool authDone = false;
			bool authOk = false;
			uint64_t sessionId = 0;
			if (!disp.SendRequest(kOpcodeAuthRequest, authPayload, [&](uint32_t, bool timeout, std::vector<uint8_t> payload) {
				authDone = true;
				authTimeout = timeout;
				if (!timeout && !payload.empty())
				{
					auto p = ParseAuthResponsePayload(payload.data(), payload.size());
					authOk = p && p->success != 0;
					if (authOk) sessionId = p->session_id;
				}
			}, m_timeoutMs))
			{
				result.errorMessage = "Send AUTH request failed";
				LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_timeoutMs + 500);
			while (!authDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!authDone || authTimeout || !authOk)
			{
				result.errorMessage = authTimeout ? "AUTH timeout" : "AUTH failed";
				LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			disp.SetSessionId(sessionId);
			LOG_INFO(Net, "[MasterShardClientFlow] AUTH OK (session_id={})", sessionId);
		}

		LOG_INFO(Net, "[MasterShardClientFlow] Requesting SERVER_LIST");
		std::vector<ServerListEntry> list;
		{
			bool listDone = false;
			bool listTimeout = true;
			if (!disp.SendRequest(kOpcodeServerListRequest, {}, [&](uint32_t, bool timeout, std::vector<uint8_t> payload) {
				listDone = true;
				listTimeout = timeout;
				if (!timeout && !payload.empty())
					list = ParseServerListResponsePayload(payload.data(), payload.size());
			}, m_timeoutMs))
			{
				result.errorMessage = "Send SERVER_LIST request failed";
				LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_timeoutMs + 500);
			while (!listDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!listDone || listTimeout || list.empty())
			{
				result.errorMessage = listTimeout ? "SERVER_LIST timeout" : "SERVER_LIST empty or failed";
				LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			LOG_INFO(Net, "[MasterShardClientFlow] SERVER_LIST OK ({} entries)", list.size());
		}

		const ServerListEntry* chosen = nullptr;
		for (const auto& e : list)
		{
			if (e.status == 1 && !e.endpoint.empty())
			{
				chosen = &e;
				break;
			}
		}
		if (!chosen)
		{
			result.errorMessage = "No Online shard with endpoint in list";
			LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}
		uint32_t targetShardId = chosen->shard_id;
		std::string shardEndpoint = chosen->endpoint;
		LOG_INFO(Net, "[MasterShardClientFlow] Requesting ticket for shard_id={}", targetShardId);

		std::vector<uint8_t> ticketPayload;
		{
			auto reqPayload = BuildRequestShardTicketPayload(targetShardId);
			bool ticketDone = false;
			bool ticketTimeout = true;
			if (!disp.SendRequest(kOpcodeRequestShardTicket, reqPayload, [&](uint32_t, bool timeout, std::vector<uint8_t> payload) {
				ticketDone = true;
				ticketTimeout = timeout;
				if (!timeout && !payload.empty())
					ticketPayload = std::move(payload);
			}, m_timeoutMs))
			{
				result.errorMessage = "Send REQUEST_SHARD_TICKET failed";
				LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_timeoutMs + 500);
			while (!ticketDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!ticketDone || ticketTimeout || ticketPayload.empty())
			{
				result.errorMessage = ticketTimeout ? "REQUEST_SHARD_TICKET timeout" : "REQUEST_SHARD_TICKET failed or empty";
				LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			LOG_INFO(Net, "[MasterShardClientFlow] Ticket received ({} bytes)", ticketPayload.size());
		}

		std::string shardHost;
		uint16_t shardPort = 3841;
		ParseEndpoint(shardEndpoint, shardHost, shardPort);
		LOG_INFO(Net, "[MasterShardClientFlow] Connecting to Shard {}:{}", shardHost, shardPort);

		NetClient shardClient;
		shardClient.SetAllowInsecureDev(true);
		shardClient.Connect(shardHost, shardPort);
		if (!WaitConnected(&shardClient, m_timeoutMs + 2000u))
		{
			result.errorMessage = "Shard connect timeout or disconnected";
			LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}
		auto presentPkt = BuildPresentShardTicketPacket(1u, ticketPayload);
		ticketPayload.clear();
		ticketPayload.shrink_to_fit();
		if (presentPkt.empty() || !shardClient.Send(std::span<const uint8_t>(presentPkt.data(), presentPkt.size())))
		{
			result.errorMessage = "Send PRESENT_SHARD_TICKET failed";
			LOG_ERROR(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}
		LOG_INFO(Net, "[MasterShardClientFlow] PRESENT_SHARD_TICKET sent, waiting for response");

		bool shardAccepted = false;
		auto shardDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_timeoutMs + 500);
		while (std::chrono::steady_clock::now() < shardDeadline)
		{
			auto events = shardClient.PollEvents();
			for (const auto& ev : events)
			{
				if (ev.type == NetClientEventType::Disconnected)
				{
					result.errorMessage = "Shard disconnected: " + ev.reason;
					LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
					return result;
				}
				if (ev.type == NetClientEventType::PacketReceived && ev.packet.size() >= kProtocolV1HeaderSize)
				{
					PacketView view;
					if (PacketView::Parse(ev.packet.data(), ev.packet.size(), view) == PacketParseResult::Ok)
					{
						if (view.Opcode() == kOpcodeShardTicketAccepted)
						{
							shardAccepted = true;
							LOG_INFO(Net, "[MasterShardClientFlow] TICKET_ACCEPTED");
							break;
						}
						if (view.Opcode() == kOpcodeShardTicketRejected)
						{
							result.errorMessage = "Shard rejected ticket";
							LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
							return result;
						}
					}
				}
			}
			if (shardAccepted)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		if (!shardAccepted)
		{
			result.errorMessage = "Shard response timeout or no TICKET_ACCEPTED";
			LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}
		LOG_INFO(Net, "[FLOW] PRESENT_SHARD_TICKET result={}", "ACCEPTED");

		result.success = true;
		result.shard_id = targetShardId;
		LOG_INFO(Net, "[MasterShardClientFlow] Flow complete (shard_id={})", result.shard_id);
		return result;
	}
}
