#pragma once
// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — AuctionHandler : dispatch
// des opcodes Auction (173/175/177/179) cote joueur et registry in-memory V1
// de 8 listings hardcodees au boot. Le master gere les bids et marque les
// auctions ended a chaque scan AuctionListRequest, en pushant
// AuctionExpiredNotification a leurs owners si la connexion existe.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// 4 requests opcodes. Les responses 174/176/178/180 sont emises avec le
// meme requestId / sessionId que la request recue. La push notification 181
// (AuctionExpired) est emise par le handler (helper public PushAuctionExpired)
// pour signaler les fins d'auctions.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 1) dans la reponse type-specific.
//
// Store in-memory V1 (mutex protege) :
//   - 8 auctions hardcodees au boot avec differents owners (Aragorn, Legolas,
//     Gimli, Saruman) et expirations echelonnees (1h-48h).
//   - Map item template id -> name (10 entries) pour les listings V1.
//   - m_nextAuctionId (atomic) : incremente a chaque AuctionPostRequest.
//
// V1 limitations :
//   - 8 listings hardcodes au boot. Future PR : DB seed via MysqlAuctionStore.
//   - Item name table 10 entries hardcode. Future PR : ItemTemplate join.
//   - Owner name = "Account#<id>" pour les listings postes par le client V1.
//   - Pas d'expiration tick periodique : scan a chaque AuctionListRequest.
//   - Pas de SyncAuction RPC entre master et shardd.
//   - Pas de paiement reel : economie cosmetique V1.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Auction in-memory V1.
	struct InMemoryAuction
	{
		uint64_t                              auctionId               = 0;
		uint32_t                              itemTemplateId          = 0;
		std::string                           itemName;
		uint32_t                              count                   = 0;
		uint64_t                              startBidCopper          = 0;
		uint64_t                              currentBidCopper        = 0;
		uint64_t                              buyoutCopper            = 0;
		std::string                           ownerName;
		uint64_t                              ownerAccountId          = 0;
		std::string                           highestBidderName;
		uint64_t                              highestBidderAccountId  = 0;
		std::chrono::steady_clock::time_point expiresAt{};
		bool                                  ended                   = false;
		bool                                  wonByBuyout             = false;
	};

	/// Dispatcher Auction cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class AuctionHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Initialise le store V1 : enregistre 8 auctions hardcodees avec
		/// differents owners (Aragorn, Legolas, Gimli, Saruman) et expirations
		/// echelonnees (1h-48h). Idempotent : appelable a chaque boot.
		void SeedV1Auctions();

		/// Point d'entree appele par NetServer pour les opcodes Auction.
		/// Dispatch vers HandleList / HandlePost / HandleBid / HandleCancel
		/// selon l'opcode. Si l'opcode n'est pas un opcode Auction, ignore
		/// silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (173/175/177/179).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push AuctionExpiredNotification (opcode 181)
		/// au client identifie par \p connId. Utilise par le handler en interne
		/// (a chaque AuctionListRequest pour les auctions expirees) mais
		/// accessible egalement depuis l'exterieur (tests, hooks futurs).
		///
		/// \param connId          identifiant de connexion TCP cible (0 = no-op).
		/// \param auctionId       identifiant de l'auction expiree.
		/// \param won             1 si vendue, 0 si terminee sans bid.
		/// \param finalBidCopper  montant final (0 si won=0).
		/// \param winnerName      nom du gagnant (vide si won=0).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushAuctionExpired(uint32_t connId, uint64_t auctionId, uint8_t won,
		                        uint64_t finalBidCopper, const std::string& winnerName);

		/// Helper static : retourne le nom hardcode d'un item template V1.
		/// Pour les ids inconnus retourne "Item #<id>".
		static std::string ResolveItemName(uint32_t itemTemplateId);

	private:
		/// Traite AUCTION_LIST_REQUEST : scan + push expired si necessaires +
		/// retourne les listings actifs (filtre eventuel sur itemTemplateId).
		void HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite AUCTION_POST_REQUEST : valide params (count > 0, startBid > 0,
		/// buyout 0 ou >= startBid, durationHours in {12, 24, 48}) puis cree
		/// une nouvelle auction.
		void HandlePost(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite AUCTION_BID_REQUEST : verifie auction active + bidAmount >
		/// currentBid + bidder != owner. Si bidAmount >= buyoutCopper et buyout
		/// > 0 -> buyout immediat (push expired au seller).
		void HandleBid(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite AUCTION_CANCEL_REQUEST : verifie ownership puis marque
		/// ended=true.
		void HandleCancel(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// Resout un accountId vers un connId via Snapshot() + GetAccountId().
		/// Retourne 0 si aucune connexion ne correspond.
		uint32_t FindConnIdForAccount(uint64_t accountId) const;

		/// Scanne m_auctions et marque ended=true pour celles dont
		/// expiresAt <= now. Pour chaque auction nouvellement marquee ended,
		/// pousse AuctionExpiredNotification au owner si sa connexion est
		/// resolvable. Doit etre appele AVEC m_mutex tenu par l'appelant.
		///
		/// \param now    instant de reference (steady_clock).
		/// \param[out] expiredOwners  collecte des paires (ownerAccountId,
		///                            auctionId, won, finalBid, winnerName)
		///                            a pousser apres relachement du mutex.
		struct ExpiredPushItem
		{
			uint64_t    ownerAccountId  = 0;
			uint64_t    auctionId       = 0;
			uint8_t     won             = 0;
			uint64_t    finalBidCopper  = 0;
			std::string winnerName;
		};
		void ScanExpiredLocked(std::chrono::steady_clock::time_point now,
			std::vector<ExpiredPushItem>& expiredOwners);

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Mutex protegeant m_auctions + m_seeded.
		mutable std::mutex                               m_mutex;

		/// Registry auctions in-memory (V1, 8 listings seedees au boot +
		/// listings postes par les clients).
		std::vector<InMemoryAuction>                     m_auctions;

		/// Generateur d'auctionId monotone. Atomic pour autoriser un futur
		/// usage hors mutex si necessaire.
		std::atomic<uint64_t>                            m_nextAuctionId{1ull};

		/// True une fois SeedV1Auctions() appele avec succes.
		bool                                             m_seeded     = false;
	};
}
