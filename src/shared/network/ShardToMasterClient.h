#pragma once

#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ServerMeta.h"
#include "src/shared/network/ShardPayloads.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

		/// Sécurité (audit F3) : secret partagé HMAC-SHA256 authentifiant les payloads
		/// SHARD_REGISTER/SHARD_HEARTBEAT envoyés au master (config `shard.ticket_hmac_secret`).
		/// Doit être appelé avant Start() ; si vide, l'émission échoue silencieusement
		/// (WrapShardAuth renvoie {} et le paquet n'est pas envoyé).
		void SetSharedSecret(std::string secret);

		/// Présence enrichie (v9) : fournisseur optionnel appelé à chaque heartbeat pour
		/// joindre la liste des joueurs en jeu `{accountId, characterId, level, zoneId}`.
		/// Doit renvoyer un snapshot cohérent (typiquement construit sur le thread monde).
		/// `nullptr` (défaut) = heartbeat legacy sans tableau joueurs.
		using PlayerPresenceProvider = std::function<std::vector<ShardPlayerPresence>()>;
		void SetPlayerPresenceProvider(PlayerPresenceProvider provider) { m_presence_provider = std::move(provider); }

		/// TA.3 — Callback invoqué quand le master pousse un `kOpcodeMasterToShardAdmitCharacter`
		/// (suite à un EnterWorld réussi côté master). Le destinataire typique alimente
		/// `AdmittedCharacterRegistry::Admit(character_id, account_id, characterName, gender, now)`
		/// côté shard. TD.5 — `character_name` est fourni par le master (table characters.name)
		/// pour alimenter les plaques de nom côté client ; peut être vide (master legacy).
		/// TD.6 — `gender` ("male"/"female", cf. migration 0067) permet au client de choisir
		/// le mesh skinné des avatars distants ; peut être vide (master sans migration 0067).
		/// Roadmap-7 — `guild_id` : guilde du compte (guild_members_v2 côté master), 0 si
		/// sans guilde ou master antérieur à l'extension. Consommé par le shard pour le
		/// partage de buffs à la guilde (gâteau, dette #991).
		/// `nullptr` (défaut) = drop silencieux.
		using AdmitCharacterCallback = std::function<void(uint64_t account_id, uint64_t character_id,
			std::string_view character_name, std::string_view gender, uint64_t guild_id)>;
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
		/// Sécurité (audit F3) : secret HMAC partagé avec le master. Voir SetSharedSecret.
		std::string m_shared_secret;

		State m_state = State::Disconnected;
		uint32_t m_shard_id = 0;
		std::chrono::steady_clock::time_point m_last_heartbeat_sent{};
		std::chrono::steady_clock::time_point m_reconnect_after{};
		int m_reconnect_backoff_sec = 1;
		int m_heartbeat_interval_sec = 10;
		/// TA.3 — callback admission (master push). Voir SetAdmitCharacterCallback.
		AdmitCharacterCallback m_admit_callback;
		/// Présence enrichie (v9) — fournisseur optionnel de la liste des joueurs. Voir SetPlayerPresenceProvider.
		PlayerPresenceProvider m_presence_provider;
	};
}
