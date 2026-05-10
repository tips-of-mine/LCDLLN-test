#pragma once
// Wave 5 Persistence (Phase 5.09b) - MysqlAuctionStore : wrapper
// MySQL pour persister les auctions de la maison de vente. Migration
// 0050_auction_listings.sql. Cible UNIX (master).
//
// Le runtime continue de manipuler des InMemoryAuction (steady_clock
// pour les expirations). Le store traduit expiresAt en epoch ms
// system_clock (wallclock) au moment de l'insert et reconstruit le
// steady_clock au LoadAllActive en calculant le delta restant. C'est
// la convention la plus simple pour preserver la semantique du handler
// sans modifier sa struct.
//
// Lifecycle :
//   - main_linux : instancie le store -> auctionHandler.SetAuctionStore.
//   - SeedV1Auctions : si store branche, charge LoadAllActive ; sinon
//     fallback sur le seed hardcode (8 listings).
//   - HandlePost : apres push in-memory, Insert pour assigner un id
//     persistant. Le handler privilegie l'id auto-increment DB si la
//     query reussit, sinon garde l'id atomic local (mode degrade).
//   - HandleBid : UpdateBid + (si buyout) MarkEnded.
//   - HandleCancel et ScanExpiredLocked : MarkEnded.
//
// Tous les appels au store sont best-effort : un retour false logge
// un warning mais n'interrompt pas la requete client (cohrence eventuelle).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::auctions
{
	/// Ligne d'auction telle que persistee en DB. Les expirations sont
	/// en epoch ms system_clock pour survivre au reboot. Le caller (AuctionHandler)
	/// derive un steady_clock::time_point a partir de ce nowMs.
	struct AuctionRow
	{
		uint64_t    auctionId               = 0;
		uint32_t    itemTemplateId          = 0;
		std::string itemName;
		uint32_t    count                   = 0;
		uint64_t    startBidCopper          = 0;
		uint64_t    currentBidCopper        = 0;
		uint64_t    buyoutCopper            = 0;
		uint64_t    ownerAccountId          = 0;
		std::string ownerName;
		uint64_t    highestBidderAccountId  = 0;
		std::string highestBidderName;
		uint64_t    expiresAtUnixMs         = 0;
		bool        ended                   = false;
		bool        wonByBuyout             = false;
	};

	/// MySQL backed store for AuctionHandler. Toutes les operations
	/// retournent false / vide si le pool n'est pas initialise (le
	/// caller en deduit "fallback in-memory").
	class MysqlAuctionStore final
	{
	public:
		explicit MysqlAuctionStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Retourne true si le store est en mode DB (pool initialise).
		bool IsAvailable() const noexcept;

		/// Charge toutes les auctions non terminees (ended = 0). Appele
		/// au boot par AuctionHandler::SeedV1Auctions pour reconstruire
		/// m_auctions. Vide si DB indisponible.
		std::vector<AuctionRow> LoadAllActive() const;

		/// Insere une nouvelle auction. En sortie, retourne l'id alloue
		/// par AUTO_INCREMENT (0 si echec).
		///
		/// \param row champs de la ligne (auctionId ignore en entree :
		///            la DB gen le nouvel id).
		/// \return id de la nouvelle ligne (>0) ou 0 en cas d'erreur.
		uint64_t Insert(const AuctionRow& row);

		/// Met a jour la bid courante (currentBidCopper, highestBidder*).
		/// Retourne true si la ligne existe et a ete updatee.
		bool UpdateBid(uint64_t auctionId, uint64_t newBidCopper,
			std::string_view bidderName, uint64_t bidderAccountId);

		/// Marque une auction terminee (ended=1). wonByBuyout=1 si la
		/// fin provient d'un buyout immediat ; 0 pour cancel / expiration.
		bool MarkEnded(uint64_t auctionId, bool wonByBuyout);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
