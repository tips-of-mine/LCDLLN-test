#pragma once

#include "src/shared/network/ServerProtocol.h"

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

		/// Send one Hello datagram (character / persistence nonce).
		/// Phase 3.7.5 — clientNonce élargi à uint64 pour transporter un character_id complet.
		bool SendHello(uint16_t requestedTickHz, uint16_t requestedSnapshotHz, uint64_t clientNonce);

		/// TC.2 — envoie un Input (position + orientation de l'avatar local) au shard.
		/// Appelé à la cadence `client.gameplay_udp.request_tick_hz` depuis la boucle de jeu.
		/// TD.8 — `animationState` (valeur d'AvatarAnimState) propage l'animation courante
		/// pour que les autres joueurs voient emotes/roulades/run/sprint/saut/etc.
		bool SendInput(uint32_t clientId, uint32_t inputSequence, float posX, float posY, float posZ, float yaw, uint8_t animationState);

		/// Envoie un GOODBYE (départ propre) pour que le shard évince immédiatement l'entité
		/// au lieu d'attendre le timeout d'inactivité (sinon l'avatar reste un « fantôme »
		/// visible des autres). Appelé automatiquement par Shutdown() si la session est active.
		bool SendGoodbye();

		/// Send TalkRequest (e.g. `vendor:1` to open shop).
		bool SendTalkRequest(uint32_t clientId, std::string_view targetId);

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
		std::vector<std::vector<std::byte>> PollIncoming();

	private:
		bool EnsureWinsock();
		bool SendBytes(std::span<const std::byte> packet);
		void HandleMaybeWelcome(std::span<const std::byte> packet);

		void* m_socket = nullptr; // SOCKET
		bool m_winsockStarted = false;
		bool m_active = false;
		uint32_t m_serverClientId = 0;
		std::string m_host;
		uint16_t m_port = 0;
	};
}
