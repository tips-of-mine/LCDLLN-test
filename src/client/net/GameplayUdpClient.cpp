#include "src/client/net/GameplayUdpClient.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <array>
#include <chrono>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <winsock2.h>
#	include <ws2tcpip.h>
#endif

namespace engine::client
{
	namespace
	{
		/// ETAPE3.4 — horloge monotone (ms) du canal fiable.
		uint64_t NowSteadyMs()
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}

#if defined(_WIN32)
		SOCKET Native(void* s) { return static_cast<SOCKET>(reinterpret_cast<uintptr_t>(s)); }

		void* ToOpaque(SOCKET s) { return reinterpret_cast<void*>(static_cast<uintptr_t>(s)); }

		bool SetNonBlocking(SOCKET s)
		{
			u_long mode = 1;
			return ioctlsocket(s, FIONBIO, &mode) == 0;
		}
#endif
	}

	GameplayUdpClient::~GameplayUdpClient()
	{
		Shutdown();
	}

	bool GameplayUdpClient::EnsureWinsock()
	{
#if defined(_WIN32)
		if (m_winsockStarted)
		{
			return true;
		}
		WSADATA wsa{};
		const int r = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (r != 0)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] WSAStartup FAILED (code={})", r);
			return false;
		}
		m_winsockStarted = true;
		LOG_INFO(Net, "[GameplayUdpClient] WSAStartup OK");
		return true;
#else
		LOG_WARN(Net, "[GameplayUdpClient] Init ignored: UDP gameplay client is Windows-only in this build");
		return false;
#endif
	}

	bool GameplayUdpClient::Init(std::string host, uint16_t port)
	{
		if (m_active)
		{
			LOG_WARN(Net, "[GameplayUdpClient] Init ignored: already active");
			return true;
		}

		if (!EnsureWinsock())
		{
			return false;
		}

#if defined(_WIN32)
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;

		addrinfo* resolved = nullptr;
		const std::string portStr = std::to_string(static_cast<int>(port));
		const int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &resolved);
		if (gai != 0 || resolved == nullptr)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: getaddrinfo ({}, {})", host, portStr);
			if (resolved)
				freeaddrinfo(resolved);
			return false;
		}

		SOCKET s = socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
		if (s == INVALID_SOCKET)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: socket() wsa={}", WSAGetLastError());
			freeaddrinfo(resolved);
			return false;
		}

		if (::connect(s, resolved->ai_addr, static_cast<int>(resolved->ai_addrlen)) == SOCKET_ERROR)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: connect() wsa={}", WSAGetLastError());
			closesocket(s);
			freeaddrinfo(resolved);
			return false;
		}
		freeaddrinfo(resolved);

		if (!SetNonBlocking(s))
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: non-blocking wsa={}", WSAGetLastError());
			closesocket(s);
			return false;
		}

		// ETAPE3.1 — nouvelle socket = nouveau sel = nouvelle clé de travail : les
		// compteurs repartent de zéro sans jamais réutiliser un nonce. Échec du tirage :
		// on refuse d'ouvrir la session plutôt que de retomber en clair.
		if (m_hasSessionKey)
		{
			engine::network::UdpSessionSalt salt{};
			if (!engine::network::FillRandomBytes(std::span<uint8_t>(salt.data(), salt.size()))
				|| !m_secure.Init(m_sessionKey, salt, engine::network::UdpSecureRole::Client))
			{
				LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: session chiffrée impossible (générateur aléatoire / OpenSSL)");
				closesocket(s);
				return false;
			}
		}
		else
		{
			m_secure = engine::network::UdpSecureSession{};
		}
		m_rejectedIncoming = 0;

		m_socket = ToOpaque(s);
		m_host = std::move(host);
		m_port = port;
		m_serverClientId = 0;
		m_active = true;
		LOG_INFO(Net, "[GameplayUdpClient] Init OK (host={}, port={}, chiffré={})", m_host, m_port,
			m_secure.IsReady() ? "oui" : "non");
		return true;
#else
		(void)host;
		(void)port;
		return false;
#endif
	}

	void GameplayUdpClient::Shutdown()
	{
		// Départ propre : prévient le shard AVANT de fermer la socket pour qu'il évince
		// immédiatement notre entité (sinon avatar « fantôme » jusqu'au timeout). Best-effort
		// (UDP, sans accusé) : si le datagramme se perd, le timeout serveur prend le relais.
		if (m_active && m_serverClientId != 0u)
		{
			(void)SendGoodbye();
		}
#if defined(_WIN32)
		if (m_socket != nullptr)
		{
			closesocket(Native(m_socket));
			m_socket = nullptr;
			LOG_INFO(Net, "[GameplayUdpClient] Socket closed (host={})", m_host);
		}
#endif
		// Intentionally no WSACleanup(): Winsock may be shared with other subsystems (e.g. NetClient).
		m_winsockStarted = false;
		m_active = false;
		m_serverClientId = 0;
		m_host.clear();
		m_port = 0;
		// ETAPE3.1 — la session (clé de travail + compteurs) meurt avec la socket ; la clé
		// de session reste pour le prochain Init du même EnterWorld.
		m_secure = engine::network::UdpSecureSession{};
		m_lastHelloDatagram.clear();
		m_helloAttempts = 0;
		m_lastHelloSentMs = 0;
		// ETAPE3.4 — le canal fiable meurt avec la session.
		m_reliable.Reset();
		m_reliableFailureLogged = false;
	}

	void GameplayUdpClient::SetSessionKey(uint64_t characterId, const engine::network::UdpSessionKey& key)
	{
		m_sessionKey = key;
		m_sessionCharacterId = characterId;
		m_hasSessionKey = characterId != 0u;
	}

	void GameplayUdpClient::ClearSessionKey()
	{
		// Effacement volontaire octet par octet (pas de memset optimisable).
		volatile uint8_t* bytes = m_sessionKey.data();
		for (size_t i = 0; i < m_sessionKey.size(); ++i)
			bytes[i] = 0u;
		m_hasSessionKey = false;
		m_sessionCharacterId = 0;
	}

	bool GameplayUdpClient::SendBytes(std::span<const std::byte> packet)
	{
		// ETAPE3.4 (chantier 4.4) — demandes qui ne doivent pas se perdre (butin,
		// échange, chat, quêtes, équipement…) : canal fiable (renvoi jusqu'à l'accusé).
		engine::server::MessageKind kind{};
		if (engine::server::PeekMessageKind(packet, kind) && engine::network::reliable::IsReliableKind(kind))
		{
			std::vector<std::byte> envelope;
			if (packet.size() > engine::network::reliable::ReliableChannel::kMaxClientToShardMessageSize
				|| !m_reliable.Queue(packet, NowSteadyMs(), envelope))
			{
				LOG_ERROR(Net, "[GameplayUdpClient] Send FAILED: canal fiable en échec ou message trop gros (kind={}, size={})",
					static_cast<uint32_t>(kind), packet.size());
				return false;
			}
			(void)SendUnreliable(envelope); // perdu : renvoyé par le canal
			return true;
		}
		return SendUnreliable(packet);
	}

	bool GameplayUdpClient::SendUnreliable(std::span<const std::byte> packet)
	{
		// ETAPE3.1 — session chiffrée : chaque paquet de jeu part scellé.
		if (m_secure.IsReady())
		{
			std::vector<std::byte> sealed;
			if (!m_secure.Seal(packet, sealed))
			{
				LOG_ERROR(Net, "[GameplayUdpClient] Send FAILED: scellement impossible");
				return false;
			}
			return SendRaw(sealed);
		}
		return SendRaw(packet);
	}

	bool GameplayUdpClient::SendRaw(std::span<const std::byte> packet)
	{
#if defined(_WIN32)
		if (!m_active || m_socket == nullptr)
		{
			LOG_WARN(Net, "[GameplayUdpClient] Send ignored: not active");
			return false;
		}
		const int r = send(Native(m_socket),
			reinterpret_cast<const char*>(packet.data()),
			static_cast<int>(packet.size()),
			0);
		if (r == SOCKET_ERROR)
		{
			const int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
			{
				LOG_WARN(Net, "[GameplayUdpClient] Send would block");
				return false;
			}
			LOG_ERROR(Net, "[GameplayUdpClient] Send FAILED wsa={}", err);
			return false;
		}
		LOG_DEBUG(Net, "[GameplayUdpClient] Sent {} bytes", r);
		return true;
#else
		(void)packet;
		return false;
#endif
	}

	void GameplayUdpClient::HandleMaybeWelcome(std::span<const std::byte> packet)
	{
		engine::server::MessageKind kind{};
		if (!engine::server::PeekMessageKind(packet, kind) || kind != engine::server::MessageKind::Welcome)
		{
			return;
		}
		engine::server::WelcomeMessage welcome{};
		if (!engine::server::DecodeWelcome(packet, welcome))
		{
			LOG_WARN(Net, "[GameplayUdpClient] Welcome decode FAILED");
			return;
		}
		if (m_serverClientId != 0u && welcome.clientId != m_serverClientId)
		{
			// ETAPE3.4 — nouvelle session côté shard (autre client_id sur la même socket) :
			// son canal fiable repart de zéro, le nôtre aussi. Le PREMIER Welcome ne remet
			// rien à zéro (le canal est neuf depuis Init ; des messages fiables du shard
			// ont pu arriver avant un Welcome perdu puis renvoyé). Un Welcome répété (même
			// client) garde le canal.
			m_reliable.Reset();
			m_reliableFailureLogged = false;
		}
		m_serverClientId = welcome.clientId;
		LOG_INFO(Net,
			"[GameplayUdpClient] Welcome received (client_id={}, tick_hz={}, snapshot_hz={})",
			welcome.clientId,
			welcome.tickHz,
			welcome.snapshotHz);
	}

	bool GameplayUdpClient::SendHello(uint16_t requestedTickHz, uint16_t requestedSnapshotHz, uint64_t clientNonce)
	{
		engine::server::HelloMessage hm{};
		hm.requestedTickHz = requestedTickHz;
		hm.requestedSnapshotHz = requestedSnapshotHz;
		hm.clientNonce = clientNonce;
		const std::vector<std::byte> packet = engine::server::EncodeHello(hm);
		std::vector<std::byte> datagram;
		if (m_secure.IsReady())
		{
			// ETAPE3.1 — le Hello part dans une poignée de main chiffrée qui annonce le
			// personnage en clair (le shard y retrouve la clé) et l'authentifie. Scellée
			// une seule fois par socket (compteur 0) : les répétitions renvoient ces
			// mêmes octets (cf. MaybeResendHello).
			if (clientNonce != m_sessionCharacterId)
			{
				LOG_ERROR(Net, "[GameplayUdpClient] Hello refusé : nonce={} différent du personnage de la clé ({})",
					clientNonce, m_sessionCharacterId);
				return false;
			}
			if (!m_secure.SealHandshake(clientNonce, packet, datagram))
			{
				LOG_ERROR(Net, "[GameplayUdpClient] Hello refusé : poignée de main déjà scellée ou erreur OpenSSL");
				return false;
			}
		}
		else
		{
			datagram = packet;
		}
		m_lastHelloDatagram = datagram;
		m_helloAttempts = 1;
		m_lastHelloSentMs = 0;
		const bool ok = SendRaw(datagram);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] Hello sent (nonce={}, chiffré={})", clientNonce,
				m_secure.IsReady() ? "oui" : "non");
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] Hello send FAILED (nonce={})", clientNonce);
		}
		return ok;
	}

	void GameplayUdpClient::MaybeResendHello(uint64_t nowMs)
	{
		if (!m_active || m_serverClientId != 0u || m_lastHelloDatagram.empty() || m_helloAttempts >= kHelloMaxAttempts)
			return;
		if (m_lastHelloSentMs == 0u)
		{
			m_lastHelloSentMs = nowMs; // premier appel après SendHello : point de départ
			return;
		}
		if (nowMs - m_lastHelloSentMs < kHelloResendIntervalMs)
			return;
		m_lastHelloSentMs = nowMs;
		++m_helloAttempts;
		const bool ok = SendRaw(m_lastHelloDatagram);
		LOG_INFO(Net, "[GameplayUdpClient] Hello renvoyé (essai {}/{}, ok={})", m_helloAttempts, kHelloMaxAttempts, ok ? 1 : 0);
		if (m_helloAttempts >= kHelloMaxAttempts)
			LOG_WARN(Net, "[GameplayUdpClient] Aucun Welcome après {} Hello : le shard ne répond pas", kHelloMaxAttempts);
	}

	bool GameplayUdpClient::SendInput(uint32_t clientId, uint32_t inputSequence, float posX, float posY, float posZ, float yaw,
		uint8_t animationState, uint32_t ackSnapshotSeq, const MoveCommandFields& move)
	{
		engine::server::InputMessage im{};
		im.clientId = clientId;
		im.inputSequence = inputSequence;
		im.positionMetersX = posX;
		im.positionMetersY = posY;
		im.positionMetersZ = posZ;
		im.yawRadians = yaw;
		im.animationState = animationState; // TD.8 : état d'animation local propagé au shard.
		im.ackSnapshotSeq = ackSnapshotSeq; // ETAPE3.2 : acquittement du dernier snapshot reconstruit.
		im.clientTimeMs = move.clientTimeMs; // ETAPE3.3 : commande de mouvement.
		im.lastJumpMs = move.lastJumpMs;
		im.lastDodgeMs = move.lastDodgeMs;
		im.moveEpoch = move.moveEpoch;
		// Pas de log par paquet (cadence ~20 Hz) — éviterait de noyer la console.
		return SendBytes(engine::server::EncodeInput(im));
	}

	bool GameplayUdpClient::SendGoodbye()
	{
		engine::server::GoodbyeMessage gm{};
		gm.clientId = m_serverClientId;
		const bool ok = SendBytes(engine::server::EncodeGoodbye(gm));
		LOG_INFO(Net, "[GameplayUdpClient] Goodbye sent (client_id={}, ok={})", m_serverClientId, ok ? 1 : 0);
		return ok;
	}

	bool GameplayUdpClient::SendTalkRequest(uint32_t clientId, std::string_view targetId)
	{
		engine::server::TalkRequestMessage msg{};
		msg.clientId = clientId;
		msg.targetId = std::string(targetId);
		const std::vector<std::byte> packet = engine::server::EncodeTalkRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] TalkRequest sent (client_id={}, target={})", clientId, msg.targetId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] TalkRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendChatSend(uint32_t clientId, uint8_t channel, std::string_view text)
	{
		engine::server::ChatSendRequestMessage msg{};
		msg.clientId = clientId;
		msg.channel = channel;
		// Le décodeur shard rejette un texte > 256 octets : on coupe ici, sans
		// casser un caractère UTF-8 multi-octets (on recule sur les octets de suite 10xxxxxx).
		constexpr size_t kMaxChatBytes = 256u;
		// Pas de std::min ici : <winsock2.h> (inclus plus haut sous Windows) définit
		// les macros min/max, qui cassent l'appel sous MSVC.
		size_t length = text.size() < kMaxChatBytes ? text.size() : kMaxChatBytes;
		while (length > 0u && length < text.size()
			&& (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u)
		{
			--length;
		}
		msg.text = std::string(text.substr(0, length));
		const std::vector<std::byte> packet = engine::server::EncodeChatSend(msg);
		if (packet.empty())
		{
			LOG_WARN(Net, "[GameplayUdpClient] ChatSend encode FAILED (client_id={}, channel={})", clientId, channel);
			return false;
		}
		const bool ok = SendBytes(packet);
		if (!ok)
		{
			LOG_WARN(Net, "[GameplayUdpClient] ChatSend FAILED (client_id={}, channel={})", clientId, channel);
		}
		return ok;
	}

	bool GameplayUdpClient::SendQuestAcceptRequest(uint32_t clientId, const std::string& questId, const std::string& giverTargetId)
	{
		engine::server::QuestAcceptRequestMessage msg{};
		msg.clientId = clientId;
		msg.questId = questId;
		msg.giverTargetId = giverTargetId;
		const std::vector<std::byte> packet = engine::server::EncodeQuestAcceptRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] QuestAcceptRequest sent (client_id={}, quest={}, giver={})",
				clientId, msg.questId, msg.giverTargetId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] QuestAcceptRequest FAILED (client_id={}, quest={})", clientId, msg.questId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendQuestTurnInRequest(uint32_t clientId, const std::string& questId, const std::string& npcTargetId)
	{
		engine::server::QuestTurnInRequestMessage msg{};
		msg.clientId = clientId;
		msg.questId = questId;
		msg.npcTargetId = npcTargetId;
		const std::vector<std::byte> packet = engine::server::EncodeQuestTurnInRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] QuestTurnInRequest sent (client_id={}, quest={}, npc={})",
				clientId, msg.questId, msg.npcTargetId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] QuestTurnInRequest FAILED (client_id={}, quest={})", clientId, msg.questId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAttackRequest(uint32_t clientId, uint64_t targetEntityId)
	{
		engine::server::AttackRequestMessage msg{};
		msg.clientId = clientId;
		msg.targetEntityId = targetEntityId;
		const std::vector<std::byte> packet = engine::server::EncodeAttackRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_DEBUG(Net, "[GameplayUdpClient] AttackRequest sent (client_id={}, target_entity_id={})",
				clientId, targetEntityId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AttackRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendRespawnRequest(uint32_t clientId, uint8_t destination)
	{
		engine::server::RespawnRequestMessage msg{};
		msg.clientId = clientId;
		msg.destination = destination;
		const std::vector<std::byte> packet = engine::server::EncodeRespawnRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] RespawnRequest sent (client_id={}, destination={})", clientId, destination);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] RespawnRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendCastRequest(uint32_t clientId, uint64_t targetEntityId, std::string_view spellId)
	{
		engine::server::CastRequestMessage msg{};
		msg.clientId = clientId;
		msg.targetEntityId = targetEntityId;
		msg.spellId = std::string(spellId);
		const std::vector<std::byte> packet = engine::server::EncodeCastRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_DEBUG(Net, "[GameplayUdpClient] CastRequest sent (client_id={}, spell={}, target_entity_id={})",
				clientId, msg.spellId, targetEntityId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] CastRequest FAILED (client_id={}, spell={})", clientId, msg.spellId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendSetActionBarLayout(uint32_t clientId, const std::array<std::string, 10>& slots)
	{
		engine::server::SetActionBarLayoutMessage msg{};
		msg.clientId = clientId;
		msg.slots = slots;
		const std::vector<std::byte> packet = engine::server::EncodeSetActionBarLayout(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_DEBUG(Net, "[GameplayUdpClient] SetActionBarLayout sent (client_id={})", clientId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] SetActionBarLayout FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendSetBeltLayout(uint32_t clientId, const std::vector<std::string>& slots)
	{
		engine::server::SetBeltLayoutMessage msg{};
		msg.clientId = clientId;
		msg.slots = slots;
		const std::vector<std::byte> packet = engine::server::EncodeSetBeltLayout(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_DEBUG(Net, "[GameplayUdpClient] SetBeltLayout sent (client_id={})", clientId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] SetBeltLayout FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendChooseClassSkill(uint32_t clientId, uint32_t level, std::string_view skillId)
	{
		engine::server::ChooseClassSkillRequestMessage msg{};
		msg.clientId = clientId;
		msg.level = level;
		msg.skillId = std::string(skillId);
		const std::vector<std::byte> packet = engine::server::EncodeChooseClassSkillRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_DEBUG(Net, "[GameplayUdpClient] ChooseClassSkill sent (client_id={}, level={}, skill={})",
				clientId, level, msg.skillId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] ChooseClassSkill FAILED (client_id={}, level={}, skill={})",
				clientId, level, msg.skillId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendPartyAccept(uint32_t clientId)
	{
		engine::server::PartyAcceptMessage msg{};
		msg.clientId = clientId;
		const std::vector<std::byte> packet = engine::server::EncodePartyAccept(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] PartyAccept sent (client_id={})", clientId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] PartyAccept FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendPartyDecline(uint32_t clientId)
	{
		engine::server::PartyDeclineMessage msg{};
		msg.clientId = clientId;
		const std::vector<std::byte> packet = engine::server::EncodePartyDecline(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] PartyDecline sent (client_id={})", clientId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] PartyDecline FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendPickupRequest(uint32_t clientId, uint64_t lootBagEntityId)
	{
		engine::server::PickupRequestMessage msg{};
		msg.clientId = clientId;
		msg.lootBagEntityId = lootBagEntityId;
		const bool ok = SendBytes(engine::server::EncodePickupRequest(msg));
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] PickupRequest sent (client_id={}, loot_bag_entity_id={})",
				clientId, lootBagEntityId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] PickupRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendEquipRequest(uint32_t clientId, uint32_t itemId)
	{
		engine::server::EquipRequestMessage msg{};
		msg.clientId = clientId;
		msg.itemId = itemId;
		const bool ok = SendBytes(engine::server::EncodeEquipRequest(msg));
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] EquipRequest sent (client_id={}, item_id={})",
				clientId, itemId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] EquipRequest FAILED (client_id={}, item_id={})",
				clientId, itemId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendUnequipRequest(uint32_t clientId, uint8_t slot)
	{
		engine::server::UnequipRequestMessage msg{};
		msg.clientId = clientId;
		msg.slot = slot;
		const bool ok = SendBytes(engine::server::EncodeUnequipRequest(msg));
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] UnequipRequest sent (client_id={}, slot={})",
				clientId, slot);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] UnequipRequest FAILED (client_id={}, slot={})",
				clientId, slot);
		}
		return ok;
	}

	bool GameplayUdpClient::SendHarvestRequest(uint32_t clientId, uint64_t nodeEntityId)
	{
		engine::server::HarvestRequestMessage msg{};
		msg.clientId = clientId;
		msg.nodeEntityId = nodeEntityId;
		const bool ok = SendBytes(engine::server::EncodeHarvestRequest(msg));
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] HarvestRequest sent (client_id={}, node_entity_id={})",
				clientId, nodeEntityId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] HarvestRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendHarvestCancelRequest(uint32_t clientId)
	{
		engine::server::HarvestCancelRequestMessage msg{};
		msg.clientId = clientId;
		const bool ok = SendBytes(engine::server::EncodeHarvestCancelRequest(msg));
		if (!ok)
		{
			LOG_WARN(Net, "[GameplayUdpClient] HarvestCancelRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendCraftRecipeListRequest(uint32_t clientId, std::string_view professionKey)
	{
		engine::server::CraftRecipeListRequestMessage msg{};
		msg.clientId = clientId;
		msg.professionKey = std::string(professionKey);
		const bool ok = SendBytes(engine::server::EncodeCraftRecipeListRequest(msg));
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] CraftRecipeListRequest sent (client_id={}, profession={})",
				clientId, msg.professionKey);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] CraftRecipeListRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendCraftRequest(uint32_t clientId, std::string_view recipeId)
	{
		engine::server::CraftRequestMessage msg{};
		msg.clientId = clientId;
		msg.recipeId = std::string(recipeId);
		const bool ok = SendBytes(engine::server::EncodeCraftRequest(msg));
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] CraftRequest sent (client_id={}, recipe={})",
				clientId, msg.recipeId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] CraftRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendCraftCancelRequest(uint32_t clientId)
	{
		engine::server::CraftCancelRequestMessage msg{};
		msg.clientId = clientId;
		const bool ok = SendBytes(engine::server::EncodeCraftCancelRequest(msg));
		if (!ok)
		{
			LOG_WARN(Net, "[GameplayUdpClient] CraftCancelRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendShopBuyRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity)
	{
		engine::server::ShopBuyRequestMessage msg{};
		msg.clientId = clientId;
		msg.vendorId = vendorId;
		msg.itemId = itemId;
		msg.quantity = quantity;
		const std::vector<std::byte> packet = engine::server::EncodeShopBuyRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] ShopBuy sent (client_id={}, vendor_id={}, item_id={}, qty={})",
				clientId,
				vendorId,
				itemId,
				quantity);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] ShopBuy FAILED (vendor_id={}, item_id={})", vendorId, itemId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendShopSellRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity)
	{
		engine::server::ShopSellRequestMessage msg{};
		msg.clientId = clientId;
		msg.vendorId = vendorId;
		msg.itemId = itemId;
		msg.quantity = quantity;
		const std::vector<std::byte> packet = engine::server::EncodeShopSellRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] ShopSell sent (client_id={}, vendor_id={}, item_id={}, qty={})",
				clientId,
				vendorId,
				itemId,
				quantity);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] ShopSell FAILED (vendor_id={}, item_id={})", vendorId, itemId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionBrowseRequest(const engine::server::AuctionBrowseRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionBrowseRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionBrowse sent (client_id={}, sort={}, max_rows={})",
				message.clientId,
				message.sortMode,
				message.maxRows);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionBrowse send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionListItemRequest(const engine::server::AuctionListItemRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionListItemRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionListItem sent (client_id={}, item_id={}, qty={})",
				message.clientId,
				message.itemId,
				message.quantity);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionListItem send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionBidRequest(const engine::server::AuctionBidRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionBidRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionBid sent (client_id={}, listing_id={}, bid={})",
				message.clientId,
				message.listingId,
				message.bidAmount);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionBid send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionBuyoutRequest(const engine::server::AuctionBuyoutRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionBuyoutRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionBuyout sent (client_id={}, listing_id={})",
				message.clientId,
				message.listingId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionBuyout send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	std::vector<std::vector<std::byte>> GameplayUdpClient::PollIncoming()
	{
		std::vector<std::vector<std::byte>> out;
#if defined(_WIN32)
		if (!m_active || m_socket == nullptr)
		{
			return out;
		}

		for (;;)
		{
			std::array<std::byte, 2048> buf{};
			const int r = recv(Native(m_socket), reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
			if (r == SOCKET_ERROR)
			{
				const int err = WSAGetLastError();
				if (err == WSAEWOULDBLOCK)
				{
					break;
				}
				LOG_WARN(Net, "[GameplayUdpClient] Recv error wsa={}", err);
				break;
			}
			if (r <= 0)
			{
				break;
			}

			std::vector<std::byte> one(static_cast<size_t>(r));
			std::copy_n(buf.data(), static_cast<size_t>(r), one.begin());
			// ETAPE3.1 — session chiffrée : seuls les datagrammes authentifiés passent
			// (falsifiés, rejoués ou en clair : abandonnés).
			if (m_secure.IsReady())
			{
				std::vector<std::byte> plain;
				if (!m_secure.Open(one, plain))
				{
					if ((m_rejectedIncoming++ & 0xFFu) == 0u)
						LOG_WARN(Net, "[GameplayUdpClient] Datagramme refusé (non authentifié), total={}", m_rejectedIncoming);
					continue;
				}
				one = std::move(plain);
			}
			HandleMaybeWelcome(one);
			// ETAPE3.4 — enveloppes du canal fiable.
			engine::server::MessageKind kind{};
			if (engine::server::PeekMessageKind(one, kind))
			{
				if (kind == engine::server::MessageKind::ReliableAck)
				{
					(void)m_reliable.OnAck(one, NowSteadyMs());
					continue;
				}
				if (kind == engine::server::MessageKind::Reliable)
				{
					std::vector<std::vector<std::byte>> delivered;
					if (m_reliable.OnReliable(one, NowSteadyMs(), delivered))
					{
						for (std::vector<std::byte>& inner : delivered)
						{
							engine::server::MessageKind innerKind{};
							if (engine::server::PeekMessageKind(inner, innerKind)
								&& engine::network::reliable::IsReliableKind(innerKind))
								out.push_back(std::move(inner));
						}
					}
					continue;
				}
			}
			out.push_back(std::move(one));
		}
		// ETAPE3.4 — renvois dus et accusés groupés (horloge du canal = images du jeu).
		std::vector<std::vector<std::byte>> reliableOut;
		m_reliable.Update(NowSteadyMs(), reliableOut);
		for (const std::vector<std::byte>& datagram : reliableOut)
			(void)SendUnreliable(datagram);
		if (m_reliable.Failed() && !m_reliableFailureLogged)
		{
			m_reliableFailureLogged = true;
			LOG_ERROR(Net, "[GameplayUdpClient] Canal fiable en échec : aucun accusé du shard depuis {} ms",
				engine::network::reliable::ReliableChannel::kGiveUpMs);
		}
#endif
		return out;
	}
}
