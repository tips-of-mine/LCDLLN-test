#include "src/shared/network/MasterShardClientFlow.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/RequestResponseDispatcher.h"
#include "src/shared/network/AuthRegisterPayloads.h"
#include "src/shared/network/ServerListPayloads.h"
#include "src/shared/network/ShardTicketPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/NetErrorCode.h"
#include "src/shared/core/Log.h"

#include <chrono>
#include <cctype>
#include <cstdio>
#include <string>
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

		bool IsLoopbackHost(const std::string& h)
		{
			std::string lower;
			lower.reserve(h.size());
			for (unsigned char c : h)
				lower.push_back(static_cast<char>(std::tolower(c)));
			return lower == "127.0.0.1" || lower == "localhost" || lower == "::1";
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
			result.session_id = sessionId;
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

		std::vector<const ServerListEntry*> eligible;
		eligible.reserve(list.size());
		for (const auto& e : list)
		{
			if (e.status == 1 && !e.endpoint.empty())
			{
				eligible.push_back(&e);
			}
		}
		if (eligible.empty())
		{
			result.errorMessage = "No Online shard with endpoint in list";
			LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
			return result;
		}

		const ServerListEntry* chosen = nullptr;
		if (m_shardIdOverride != 0)
		{
			for (const ServerListEntry* p : eligible)
			{
				if (p->shard_id == m_shardIdOverride)
				{
					chosen = p;
					break;
				}
			}
			if (!chosen)
			{
				result.errorMessage = "Selected shard is not available or unknown";
				LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
		}
		else if (m_shardPickWhenMultiple)
		{
			result.shard_choice_required = true;
			result.server_list_for_pick = list;
			result.success = false;
			LOG_INFO(Net,
				"[MasterShardClientFlow] {} online shard(s); returning shard_choice_required for UI",
				eligible.size());
			masterClient->Disconnect("shard_choice_required");
			return result;
		}
		else
		{
			chosen = eligible.front();
		}

		uint32_t targetShardId = chosen->shard_id;
		std::string shardEndpoint = chosen->endpoint;
		LOG_INFO(Net, "[MasterShardClientFlow] Requesting ticket for shard_id={}", targetShardId);

		std::vector<uint8_t> ticketPayload;
		{
			std::string shardTicketErrorDetail;
			disp.SetErrorHandler([&shardTicketErrorDetail](uint32_t /*requestId*/, NetErrorCode code, std::string_view msg) {
				if (!msg.empty())
					shardTicketErrorDetail.assign(msg.begin(), msg.end());
				else
					shardTicketErrorDetail = "code " + std::to_string(static_cast<uint32_t>(code));
			});
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
				if (ticketTimeout)
					result.errorMessage = "REQUEST_SHARD_TICKET timeout";
				else if (!shardTicketErrorDetail.empty())
					result.errorMessage = std::string("REQUEST_SHARD_TICKET refusé — ") + shardTicketErrorDetail;
				else
					result.errorMessage = "REQUEST_SHARD_TICKET failed or empty";
				LOG_WARN(Net, "[MasterShardClientFlow] {}", result.errorMessage);
				return result;
			}
			LOG_INFO(Net, "[MasterShardClientFlow] Ticket received ({} bytes)", ticketPayload.size());
			disp.SetErrorHandler({});
		}

		std::string shardHost;
		uint16_t shardPort = 3841;
		ParseEndpoint(shardEndpoint, shardHost, shardPort);
		if (IsLoopbackHost(shardHost) && !IsLoopbackHost(m_masterHost))
		{
			// Liste serveurs : le shard a souvent enregistré 127.0.0.1 (Docker / bind local).
			// Si le client rejoint le master par une IP LAN, on se connecte au shard sur le même hôte.
			LOG_WARN(Net,
				"[MasterShardClientFlow] endpoint shard en loopback ({}) avec master distant — connexion shard via hôte maître {}:{}",
				shardEndpoint,
				m_masterHost,
				static_cast<unsigned>(shardPort));
			shardHost = m_masterHost;
		}
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

		// Phase 2 — Optional CHARACTER_LIST query on the master connection (still open via `disp`)
		// to let the client decide between CharacterSelect (>=1 char) and CharacterCreate (0 char).
		// A failure here does NOT fail the whole flow: the shard handshake is already complete.
		LOG_INFO(Net, "[MasterShardClientFlow] Requesting CHARACTER_LIST for shard_id={}", targetShardId);
		{
			std::vector<CharacterListEntry> characters;
			bool listDone = false;
			bool listSuccess = false;
			auto reqPayload = BuildCharacterListRequestPayload(targetShardId);
			if (disp.SendRequest(kOpcodeCharacterListRequest, reqPayload,
				[&](uint32_t, bool timeout, std::vector<uint8_t> payload) {
					listDone = true;
					if (timeout || payload.empty())
						return;
					auto parsed = ParseCharacterListResponsePayload(payload.data(), payload.size());
					if (parsed && parsed->success != 0)
					{
						characters = std::move(parsed->entries);
						listSuccess = true;
					}
				}, m_timeoutMs))
			{
				auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_timeoutMs + 500);
				while (!listDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
			}
			else
			{
				LOG_WARN(Net, "[MasterShardClientFlow] Send CHARACTER_LIST request failed (non-fatal)");
			}
			if (listSuccess)
			{
				LOG_INFO(Net, "[MasterShardClientFlow] CHARACTER_LIST OK ({} entries)", characters.size());
				result.character_list = std::move(characters);
			}
			else
			{
				LOG_WARN(Net, "[MasterShardClientFlow] CHARACTER_LIST timeout/empty (non-fatal); client will fall back to CharacterCreate");
			}
		}

		result.success = true;
		result.shard_id = targetShardId;
		result.shard_endpoint = shardHost + ":" + std::to_string(static_cast<unsigned>(shardPort));
		LOG_INFO(Net, "[MasterShardClientFlow] Flow complete (shard_id={}, endpoint={}, characters={})",
			result.shard_id, result.shard_endpoint, result.character_list.size());
		return result;
	}
}
