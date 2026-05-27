#include "src/shared/network/ShardToMasterClient.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ShardPayloads.h"
#include "src/shared/core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace engine::network
{
	ShardToMasterClient::ShardToMasterClient() = default;

	ShardToMasterClient::~ShardToMasterClient()
	{
		LOG_INFO(Net, "[STMC] destructor enter state={}", (int)m_state);
		if (m_client && m_client->GetState() != NetClientState::Disconnected)
		{
			m_client->Disconnect("ShardToMasterClient destroy");
			while (m_client->GetState() != NetClientState::Disconnected)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		LOG_INFO(Core, "[ShardToMasterClient] Destroyed");
	}

	void ShardToMasterClient::SetMasterAddress(std::string host, uint16_t port)
	{
		m_host = std::move(host);
		m_port = port;
	}

	void ShardToMasterClient::SetExpectedServerFingerprint(std::string hexFingerprint)
	{
		m_fingerprint = std::move(hexFingerprint);
	}

	void ShardToMasterClient::SetAllowInsecureDev(bool allow)
	{
		m_allow_insecure = allow;
	}

	void ShardToMasterClient::SetShardIdentity(std::string name, std::string endpoint, std::string udp_endpoint, uint32_t max_capacity, std::string build_version,
		std::string display_name, ShardGameMode game_mode, ShardRuleset ruleset, std::string region)
	{
		m_name = std::move(name);
		m_endpoint = std::move(endpoint);
		m_udp_endpoint = std::move(udp_endpoint);
		m_max_capacity = max_capacity;
		m_build_version = std::move(build_version);
		m_display_name = display_name.empty() ? m_name : std::move(display_name);
		m_game_mode = game_mode;
		m_ruleset = ruleset;
		m_region = std::move(region);
	}

	void ShardToMasterClient::Start()
	{
		LOG_DEBUG(Net, "[STMC] Start host={} port={}", m_host.c_str(), (unsigned)m_port);
		if (m_host.empty())
		{
			LOG_ERROR(Core, "[ShardToMasterClient] Start failed: no master address");
			return;
		}
		m_client = std::make_unique<NetClient>();
		if (!m_fingerprint.empty())
			m_client->SetExpectedServerFingerprint(m_fingerprint);
		m_client->SetAllowInsecureDev(m_allow_insecure);
		m_client->Connect(m_host, m_port);
		m_state = State::Connecting;
		m_shard_id = 0;
		m_reconnect_backoff_sec = 1;
		LOG_INFO(Core, "[ShardToMasterClient] Connecting to {}:{}", m_host, m_port);
	}

	void ShardToMasterClient::Pump()
	{
		if (!m_client)
			return;
LOG_DEBUG(Net, "[STMC] Pump state={} reconnect_in={}s", (int)m_state, (long long)std::chrono::duration_cast<std::chrono::seconds>(m_reconnect_after - std::chrono::steady_clock::now()).count());
		auto now = std::chrono::steady_clock::now();

		if (m_state == State::Disconnected && now >= m_reconnect_after)
		{
			m_client = std::make_unique<NetClient>();
			if (!m_fingerprint.empty())
				m_client->SetExpectedServerFingerprint(m_fingerprint);
			m_client->SetAllowInsecureDev(m_allow_insecure);
			m_client->Connect(m_host, m_port);
			m_state = State::Connecting;
			m_shard_id = 0;
			LOG_INFO(Core, "[ShardToMasterClient] Reconnecting to {}:{} (backoff was {}s)", m_host, m_port, m_reconnect_backoff_sec);
		}

		auto events = m_client->PollEvents();
		for (const auto& ev : events)
		{
			if (ev.type == NetClientEventType::Connected)
				OnConnected();
			else if (ev.type == NetClientEventType::Disconnected)
				OnDisconnected(ev.reason);
			else if (ev.type == NetClientEventType::PacketReceived && !ev.packet.empty())
				OnPacketReceived(ev.packet.data(), ev.packet.size());
		}

		if (m_state == State::Registered && m_shard_id != 0)
		{
			if (now - m_last_heartbeat_sent >= std::chrono::seconds(m_heartbeat_interval_sec))
			{
				SendHeartbeat();
				m_last_heartbeat_sent = now;
			}
		}
	}

	void ShardToMasterClient::SendRegister()
	{
LOG_DEBUG(Net, "[STMC] SendRegister name='{}' display='{}' endpoint='{}' cap={} mode={} ruleset={}", m_name.c_str(), m_display_name.c_str(), m_endpoint.c_str(), m_max_capacity, GameModeToken(m_game_mode), RulesetToken(m_ruleset));
		auto payload = BuildShardRegisterPayload(m_name, m_endpoint, m_udp_endpoint, m_max_capacity, m_current_load, m_build_version,
			m_display_name, m_game_mode, m_ruleset, m_region);
		if (payload.empty())
		{
			LOG_ERROR(Core, "[ShardToMasterClient] BuildShardRegisterPayload failed");
			return;
		}
		PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!w.WriteBytes(payload.data(), payload.size()))
		{
			LOG_ERROR(Core, "[ShardToMasterClient] PayloadWriter register failed");
			return;
		}
		if (!builder.Finalize(kOpcodeShardRegister, 0, 1, 0, payload.size()))
		{
			LOG_ERROR(Core, "[ShardToMasterClient] Finalize register packet failed");
			return;
		}
		if (!m_client->Send(builder.Data()))
			LOG_ERROR(Core, "[ShardToMasterClient] Send register failed");
		else
			LOG_INFO(Core, "[ShardToMasterClient] Sent SHARD_REGISTER");
	}

	void ShardToMasterClient::SendHeartbeat()
	{
		LOG_DEBUG(Net, "[STMC] SendHeartbeat shard_id={} load={}", m_shard_id, m_current_load);
		uint64_t timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		auto payload = BuildShardHeartbeatPayload(m_shard_id, m_current_load, timestamp);
		if (payload.empty())
			return;
		PacketBuilder builder;
		auto w = builder.PayloadWriter();
		if (!w.WriteBytes(payload.data(), payload.size()))
			return;
		if (!builder.Finalize(kOpcodeShardHeartbeat, 0, 0, 0, payload.size()))
			return;
		m_client->Send(builder.Data());
	}

	void ShardToMasterClient::OnConnected()
	{
		LOG_INFO(Net, "[STMC] OnConnected -> Registering");
		m_state = State::Registering;
		m_reconnect_backoff_sec = 1;
		LOG_INFO(Core, "[ShardToMasterClient] Connected, sending register");
		SendRegister();
	}

	void ShardToMasterClient::OnDisconnected(std::string_view reason)
	{
		LOG_INFO(Net, "[STMC] OnDisconnected reason='{}' next_backoff={}s", reason, m_reconnect_backoff_sec);
		m_state = State::Disconnected;
		m_shard_id = 0;
		ScheduleReconnect();
		LOG_WARN(Core, "[ShardToMasterClient] Disconnected: {}", reason);
	}

	void ShardToMasterClient::OnPacketReceived(const uint8_t* data, size_t size)
	{
		PacketView view;
		if (PacketView::Parse(data, size, view) != PacketParseResult::Ok)
			return;
		const uint16_t opcode = view.Opcode();
		if (opcode == kOpcodeShardRegisterOk)
		{
			auto parsed = ParseShardRegisterOkPayload(view.Payload(), view.PayloadSize());
			if (parsed)
			{
				m_shard_id = parsed->shard_id;
				m_state = State::Registered;
				m_last_heartbeat_sent = std::chrono::steady_clock::now();
				LOG_INFO(Core, "[ShardToMasterClient] Register OK (shard_id={})", m_shard_id);
			}
		}
		else if (opcode == kOpcodeShardRegisterError)
		{
			LOG_ERROR(Core, "[ShardToMasterClient] Register ERROR from Master");
			m_client->Disconnect("register error");
		}
		else if (opcode == kOpcodeMasterToShardAdmitCharacter)
		{
			// TA.3 — push master->shard suite a un EnterWorld reussi cote master.
			// Le master reutilise cette connexion long-vivante (initialement
			// shard->master) pour pousser (account_id, character_id) au shard sans
			// inventer un nouveau canal de transport.
			auto parsed = ParseAdmitCharacterPayload(view.Payload(), view.PayloadSize());
			if (!parsed)
			{
				LOG_WARN(Core, "[ShardToMasterClient] AdmitCharacter: parse FAILED (size={})", view.PayloadSize());
				return;
			}
			if (!m_admit_callback)
			{
				LOG_WARN(Core, "[ShardToMasterClient] AdmitCharacter received but no callback registered (account_id={}, character_id={})",
					parsed->account_id, parsed->character_id);
				return;
			}
			LOG_INFO(Core, "[ShardToMasterClient] AdmitCharacter received (account_id={}, character_id={}, name='{}', gender='{}')",
				parsed->account_id, parsed->character_id, parsed->character_name, parsed->gender);
			m_admit_callback(parsed->account_id, parsed->character_id, parsed->character_name, parsed->gender);
		}
	}

	void ShardToMasterClient::ScheduleReconnect()
	{
		LOG_DEBUG(Net, "[STMC] ScheduleReconnect backoff={}s", m_reconnect_backoff_sec);
		m_reconnect_after = std::chrono::steady_clock::now() + std::chrono::seconds(m_reconnect_backoff_sec);
		if (m_reconnect_backoff_sec < 60)
			m_reconnect_backoff_sec = std::min(60, m_reconnect_backoff_sec * 2);
	}
}
