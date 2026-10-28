#pragma once

#include "src/shared/network/ReliableChannel.h"
#include "src/shared/network/ServerProtocol.h"
#include "src/shared/network/UdpSecureChannel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::client
{
	/// Minimal non-blocking UDP client for authoritative gameplay packets (Windows / M35.2).
	/// Connects to `host:port`, sends encoded \ref engine::server protocol datagrams, receives replies.
	class GameplayUdpClient final
	{
	public:
		GameplayUdpClient() = default;
		GameplayUdpClient(const GameplayUdpClient&) = delete;
		GameplayUdpClient& operator=(const GameplayUdpClient&) = delete;

		~GameplayUdpClient();

		/// Bind local socket and `connect()` to the gameplay server. Logs and returns false on failure.
		bool Init(std::string host, uint16_t port);

		/// Close socket and reset state. Safe to call multiple times.
		void Shutdown();

		/// Server-assigned id after \ref DecodeWelcome (0 before handshake completes).
		uint32_t ServerClientId() const { return m_serverClientId; }

		bool IsActive() const { return m_active; }

		/// ETAPE3.1 — active le chiffrement pour les prochaines sessions UDP : \p key est la
		/// clé de session envoyée au master dans la requête EnterWorld de \p characterId.
		/// Prend effet au prochain Init (chaque Init tire un nouveau sel, donc une
		/// nouvelle clé de travail). Doit être appelé AVANT Init / SendHello.
		void SetSessionKey(uint64_t characterId, const engine::network::UdpSessionKey& key);

		/// ETAPE3.1 — désactive le chiffrement des prochaines sessions (aucun EnterWorld
		/// envoyé au master, ex. mode hors ligne). Effacement sûr de la clé.
		void ClearSessionKey();

		/// ETAPE3.1 — vrai si la session UDP courante est chiffrée.
		bool IsEncrypted() const { return m_secure.IsReady(); }

		/// ETAPE3.1 — renvoie le DERNIER Hello (mêmes octets, même sel : le shard reconnaît
		/// un doublon) toutes les kHelloResendIntervalMs tant qu'aucun Welcome n'est reçu,
		/// kHelloMaxAttempts fois, puis (FIX.1) toutes les kHelloSlowResendIntervalMs au plus
		/// kHelloSlowMaxAttempts fois. Le Hello part souvent avant que le
		/// master ait poussé l'admission et la clé au shard. À appeler à chaque frame.
		/// \param nowMs horloge monotone en millisecondes.
		void MaybeResendHello(uint64_t nowMs);

		/// ETAPE3.1 — intervalle entre deux envois du Hello (ms).
		static constexpr uint64_t kHelloResendIntervalMs = 1000u;
		/// ETAPE3.1 — nombre maximal d'envois du même Hello.
		static constexpr uint32_t kHelloMaxAttempts = 15u;
		/// FIX.1 — après les kHelloMaxAttempts renvois rapides, on continue toutes les
		/// kHelloSlowResendIntervalMs, au plus kHelloSlowMaxAttempts fois (~5 min, durée de
		/// vie de la clé de session UDP côté shard) : un shard redémarré ou un pare-feu
		/// corrigé en cours de partie est rejoint sans relancer le client.
		static constexpr uint64_t kHelloSlowResendIntervalMs = 5000u;
		static constexpr uint32_t kHelloSlowMaxAttempts = 60u;

		/// FIX.1 — vrai quand les renvois rapides du Hello sont épuisés sans Welcome
		/// (le client est passé aux renvois lents). Sert au bandeau d'état du HUD.
		bool HelloFastAttemptsExhausted() const
		{
			return m_active && m_serverClientId == 0u && m_helloAttempts >= kHelloMaxAttempts;
		}

		/// Send one Hello datagram (character / persistence nonce).
		/// Phase 3.7.5 — clientNonce élargi à uint64 pour transporter un character_id complet.
		bool SendHello(uint16_t requestedTickHz, uint16_t requestedSnapshotHz, uint64_t clientNonce);

		/// TC.2 — envoie un Input (position + orientation de l'avatar local) au shard.
		/// Appelé à la cadence `client.gameplay_udp.request_tick_hz` depuis la boucle de jeu.
		/// TD.8 — `animationState` (valeur d'AvatarAnimState) propage l'animation courante
		/// pour que les autres joueurs voient emotes/roulades/run/sprint/saut/etc.
		/// ETAPE3.3 — champs de la commande de mouvement (cf. MovementRules.h).
		struct MoveCommandFields
		{
			uint32_t clientTimeMs = 0; ///< horloge de simulation du mouvement (ms).
			uint32_t lastJumpMs = 0;   ///< instant du dernier saut (0 = aucun).
			uint32_t lastDodgeMs = 0;  ///< instant de la dernière esquive (0 = aucune).
			uint16_t moveEpoch = 0;    ///< époque du dernier ForcePosition reçu.
		};

		/// ETAPE3.2 — \p ackSnapshotSeq : dernier snapshot reconstruit en entier
		/// (UIModelBinding::LatestSnapshotSeq) ; le shard écrit le suivant en delta sur lui.
		/// ETAPE3.3 — \p move : horloge et événements de la commande de mouvement.
		bool SendInput(uint32_t clientId, uint32_t inputSequence, float posX, float posY, float posZ, float yaw,
			uint8_t animationState, uint32_t ackSnapshotSeq, const MoveCommandFields& move);

		/// Envoie un GOODBYE (départ propre) pour que le shard évince immédiatement l'entité
		/// au lieu d'attendre le timeout d'inactivité (sinon l'avatar reste un « fantôme »
		/// visible des autres). Appelé automatiquement par Shutdown() si la session est active.
		bool SendGoodbye();

		/// Send TalkRequest (e.g. `vendor:1` to open shop).
		bool SendTalkRequest(uint32_t clientId, std::string_view targetId);
		/// ETAPE5.1 — envoie une ligne de chat de proximité (Dire, Crier) au shard
		/// (MessageKind::ChatSend). Le shard la redistribue aux joueurs à portée
		/// (Dire 25 m, Crier 200 m) via MessageKind::ChatRelay.
		/// \param channel valeur wire de engine::net::ChatChannel (Say ou Yell).
		/// \param text texte déjà débarrassé de son préfixe /s ou /y (tronqué à 256 octets, sans couper un caractère UTF-8).
		bool SendChatSend(uint32_t clientId, uint8_t channel, std::string_view text);

		/// SP2 — le joueur accepte une quête proposée par le PNJ \p giverTargetId
		/// (le serveur revalide l'éligibilité/le catalogue avant journalisation).
		bool SendQuestAcceptRequest(uint32_t clientId, const std::string& questId, const std::string& giverTargetId);

		/// SP2 — le joueur rend une quête au PNJ \p npcTargetId (le serveur
		/// revalide l'état « prête à rendre » avant de créditer les récompenses).
		bool SendQuestTurnInRequest(uint32_t clientId, const std::string& questId, const std::string& npcTargetId);

		/// Combat SP2 — demande d'attaque sur une entité ciblée. Le serveur
		/// revalide tout (portée, cooldown, cible vivante) ; le client peut
		/// throttler mais n'a pas d'autorité.
		bool SendAttackRequest(uint32_t clientId, uint64_t targetEntityId);

		/// Combat SP2 — demande de réapparition (joueur mort uniquement, validé
		/// serveur). Wire v13 : \p destination = kRespawnDestination* (cimetière
		/// ou auberge le plus proche du lieu de mort).
		bool SendRespawnRequest(uint32_t clientId, uint8_t destination);

		/// Combat SP3 — cast d'un sort du kit du profil. targetEntityId = 0 pour
		/// les sorts sans cible (le serveur revalide tout : kit, cooldown, coût,
		/// cible, portée).
		bool SendCastRequest(uint32_t clientId, uint64_t targetEntityId, std::string_view spellId);

		/// Grimoire — envoie l'assignation des 10 slots de barre d'action.
		/// Le serveur valide (kit/unicité) et renvoie un ActionBarLayoutUpdate autoritaire.
		bool SendSetActionBarLayout(uint32_t clientId, const std::array<std::string, 10>& slots);

		/// Roadmap-3 (2026-07-19) — Ceinture : pose le layout des 4 slots
		/// d'objets actifs (jetons "item:<id>", kind 99). ACK autoritaire
		/// par BeltLayoutUpdate (kind 100).
		/// Ceinture v2 (2026-07-20) — envoie le layout complet (taille = capacité
		/// active affichée ; le shard rejette un count > capacité autoritaire).
		bool SendSetBeltLayout(uint32_t clientId, const std::vector<std::string>& slots);

		/// SP-B — envoie un choix de skill de classe (ChooseClassSkillRequest, kind 91).
		/// Le shard valide (niveau, unicité, appartenance au kit) et renvoie un
		/// ClassProgressionUpdate autoritaire si le choix est accepté.
		bool SendChooseClassSkill(uint32_t clientId, uint32_t level, std::string_view skillId);

		/// Groupes SP1 — accepte l'invitation de groupe en attente (M32.2).
		bool SendPartyAccept(uint32_t clientId);

		/// Groupes SP1 — refuse l'invitation de groupe en attente (M32.2).
		bool SendPartyDecline(uint32_t clientId);

		/// Validation v12 — ramasse un sac de butin répliqué (le serveur valide
		/// proximité et propriété, puis pousse l'InventoryDelta).
		bool SendPickupRequest(uint32_t clientId, uint64_t lootBagEntityId);

		/// Chantier 2 SP-A — demande d'équipement d'un objet du sac (le serveur
		/// détermine le slot depuis son catalogue, puis pousse EquipmentUpdate +
		/// InventoryDelta + PlayerStats).
		bool SendEquipRequest(uint32_t clientId, uint32_t itemId);

		/// Chantier 2 SP-A — demande de retrait de l'objet d'un slot (1..10).
		bool SendUnequipRequest(uint32_t clientId, uint8_t slot);

		/// Métiers SP1 — démarre une récolte sur un node répliqué (M36.1, le
		/// serveur valide disponibilité/portée/session unique).
		bool SendHarvestRequest(uint32_t clientId, uint64_t nodeEntityId);

		/// Métiers SP1 — annule la récolte en cours (M36.1).
		bool SendHarvestCancelRequest(uint32_t clientId);

		/// Métiers SP1 — demande la liste des recettes d'un métier (M36.2).
		bool SendCraftRecipeListRequest(uint32_t clientId, std::string_view professionKey);

		/// Métiers SP1 — lance la fabrication d'une recette (M36.2).
		bool SendCraftRequest(uint32_t clientId, std::string_view recipeId);

		/// Métiers SP1 — annule la fabrication en cours (M36.2).
		bool SendCraftCancelRequest(uint32_t clientId);

		bool SendShopBuyRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity);

		bool SendShopSellRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity);

		bool SendAuctionBrowseRequest(const engine::server::AuctionBrowseRequestMessage& message);

		bool SendAuctionListItemRequest(const engine::server::AuctionListItemRequestMessage& message);

		bool SendAuctionBidRequest(const engine::server::AuctionBidRequestMessage& message);

		bool SendAuctionBuyoutRequest(const engine::server::AuctionBuyoutRequestMessage& message);

		/// Non-blocking recv loop; each full datagram becomes one element. Welcome updates \ref ServerClientId.
		/// ETAPE3.4 — les enveloppes du canal fiable sont ouvertes ici : les messages
		/// livrés (dans l'ordre, sans doublon) sont rendus comme des paquets reçus, les
		/// accusés consommés ; puis renvois dus et accusés groupés partent. À appeler à
		/// chaque image (c'est aussi l'horloge du canal fiable).
		std::vector<std::vector<std::byte>> PollIncoming();

		/// ETAPE3.4 — vrai si le canal fiable a abandonné (shard muet 15 s) : la session
		/// est perdue, le shard la coupe de son côté.
		bool ReliableChannelFailed() const { return m_reliable.Failed(); }

	private:
		bool EnsureWinsock();
		/// Envoie un paquet de jeu : par le canal fiable si son type l'exige (ETAPE3.4),
		/// scellé si la session est chiffrée (ETAPE3.1).
		bool SendBytes(std::span<const std::byte> packet);
		/// ETAPE3.4 — envoi direct (scellé si session), hors canal fiable.
		bool SendUnreliable(std::span<const std::byte> packet);
		/// ETAPE3.1 — envoie le datagramme tel quel (déjà scellé ou session en clair).
		bool SendRaw(std::span<const std::byte> datagram);
		void HandleMaybeWelcome(std::span<const std::byte> packet);

		void* m_socket = nullptr; // SOCKET
		bool m_winsockStarted = false;
		bool m_active = false;
		uint32_t m_serverClientId = 0;
		std::string m_host;
		uint16_t m_port = 0;

		// ETAPE3.1 — chiffrement des datagrammes (cf. UdpSecureChannel.h).
		bool m_hasSessionKey = false;                  ///< clé fournie par SetSessionKey.
		uint64_t m_sessionCharacterId = 0;             ///< personnage lié à la clé.
		engine::network::UdpSessionKey m_sessionKey{}; ///< clé de session de l'EnterWorld.
		engine::network::UdpSecureSession m_secure;    ///< session de la socket courante.
		uint64_t m_rejectedIncoming = 0;               ///< datagrammes reçus refusés.
		std::vector<std::byte> m_lastHelloDatagram;    ///< dernier Hello envoyé (tel quel).
		uint32_t m_helloAttempts = 0;                  ///< envois du Hello courant.
		uint64_t m_lastHelloSentMs = 0;                ///< 0 = horodatage pris au premier MaybeResendHello.

		// ETAPE3.4 — canal fiable de la session (cf. ReliableChannel.h). Remis à zéro à
		// chaque socket et à chaque nouveau client_id (Welcome d'une nouvelle session).
		engine::network::reliable::ReliableChannel m_reliable;
		bool m_reliableFailureLogged = false;
	};
}
