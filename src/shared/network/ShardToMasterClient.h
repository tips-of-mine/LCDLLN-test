#pragma once

#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ServerMeta.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace engine::network
{
	class NetClient;
}

namespace engine::network
{
	/// Shard-side client: outbound persistent TLS to Master, register at startup, periodic heartbeat, reconnect with backoff on disconnect.
	/// Uses NetClient (TLS). Config: master host/port, TLS fingerprint or allow insecure dev, shard name/endpoint/max_capacity/build_version.
	class ShardToMasterClient
	{
	public:
		ShardToMasterClient();
		~ShardToMasterClient();

		/// Set Master address and TLS. Call before Start().
		void SetMasterAddress(std::string host, uint16_t port);
		/// Set server cert fingerprint (SHA-256 hex). If empty and not AllowInsecureDev, TLS is not used.
		void SetExpectedServerFingerprint(std::string hexFingerprint);
		void SetAllowInsecureDev(bool allow);

		/// Shard identity for REGISTER. Call before Start().
		/// \param display_name nom public affiché au joueur (repli sur \p name si vide).
		/// \param game_mode mode de jeu annoncé (PvE/PvP).
		/// \param ruleset règle annoncée (liste fermée).
		/// \param region région annoncée (texte libre), exposée par l'API /status.
		void SetShardIdentity(std::string name, std::string endpoint, std::string udp_endpoint, uint32_t max_capacity, std::string build_version,
			std::string display_name = {}, ShardGameMode game_mode = ShardGameMode::PvE,
			ShardRuleset ruleset = ShardRuleset::Cooperative, std::string region = {});

		/// Start connecting. Returns immediately; result via Pump() / GetState().
		void Start();

		/// Call periodically (e.g. every 100 ms). Processes events, sends register on connect, sends heartbeat on interval, schedules reconnect on disconnect.
		void Pump();

		/// Current load to send in register and heartbeats. Default 0.
		void SetCurrentLoad(uint32_t load) { m_current_load = load; }

		/// Heartbeat interval in seconds. Default 10. Call before Start() to take effect.
		void SetHeartbeatIntervalSec(int sec) { m_heartbeat_interval_sec = (sec > 0) ? sec : 10; }

		/// TA.3 — Callback invoqué quand le master pousse un `kOpcodeMasterToShardAdmitCharacter`
		/// (suite à un EnterWorld réussi côté master). Le destinataire typique alimente
		/// `AdmittedCharacterRegistry::Admit(character_id, account_id, now)` côté shard.
		/// `nullptr` (défaut) = drop silencieux.
		using AdmitCharacterCallback = std::function<void(uint64_t account_id, uint64_t character_id)>;
		void SetAdmitCharacterCallback(AdmitCharacterCallback cb) { m_admit_callback = std::move(cb); }

		enum class State
		{
			Disconnected,
			Connecting,
			Registering,
			Registered,
		};
		State GetState() const { return m_state; }
		uint32_t GetShardId() const { return m_shard_id; }

	private:
		void SendRegister();
		void SendHeartbeat();
		void OnConnected();
		void OnDisconnected(std::string_view reason);
		void OnPacketReceived(const uint8_t* data, size_t size);
		void ScheduleReconnect();

		std::unique_ptr<NetClient> m_client;
		std::string m_host;
		uint16_t m_port = 0;
		std::string m_fingerprint;
		bool m_allow_insecure = false;
		std::string m_name;
		std::string m_endpoint;
		std::string m_udp_endpoint; ///< TB.1: endpoint UDP gameplay annoncé au master.
		uint32_t m_max_capacity = 0;
		std::string m_build_version;
		std::string m_display_name;
		ShardGameMode m_game_mode = ShardGameMode::PvE;
		ShardRuleset m_ruleset = ShardRuleset::Cooperative;
		std::string m_region;
		uint32_t m_current_load = 0;

		State m_state = State::Disconnected;
		uint32_t m_shard_id = 0;
		std::chrono::steady_clock::time_point m_last_heartbeat_sent{};
		std::chrono::steady_clock::time_point m_reconnect_after{};
		int m_reconnect_backoff_sec = 1;
		int m_heartbeat_interval_sec = 10;
		/// TA.3 — callback admission (master push). Voir SetAdmitCharacterCallback.
		AdmitCharacterCallback m_admit_callback;
	};
}
