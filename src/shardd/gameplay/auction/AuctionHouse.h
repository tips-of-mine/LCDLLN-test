#pragma once

#include "src/shared/core/Config.h"
#include "src/shared/network/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	inline constexpr uint8_t kAuctionHouseFeePercent = 5;

	/// Authoritative listing row (M35.4); item is escrowed on server until close.
	struct AuctionListingRecord
	{
		uint32_t listingId = 0;
		uint32_t sellerClientId = 0;
		/// Phase 3.7.5 — élargi à uint64 (alignement avec ConnectedClient::persistenceCharacterKey).
		uint64_t sellerCharacterKey = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		uint32_t startBid = 0;
		/// 0 = no buyout.
		uint32_t buyoutPrice = 0;
		uint32_t currentBid = 0;
		uint32_t highBidderClientId = 0;
		uint64_t highBidderCharacterKey = 0;
		uint32_t expiresAtTick = 0;
		bool closed = false;
	};

	/// One completed auction outcome for the shard to apply (wallet + inventory + notices).
	struct AuctionSettlement
	{
		uint32_t listingId = 0;
		/// 0 = expired with no bids (return item to seller).
		uint64_t buyerCharacterKey = 0;
		uint32_t buyerClientId = 0;
		uint64_t sellerCharacterKey = 0;
		ItemStack item{};
		uint32_t finalPrice = 0;
		uint32_t sellerProceeds = 0;
		bool expiredWithoutBids = false;
	};

	/// runtime shard auction house: listings file under \p paths.content + character INI mailbox (M35.4).
	class AuctionHouseService final
	{
	public:
		explicit AuctionHouseService(const engine::core::Config& config);

		AuctionHouseService(const AuctionHouseService&) = delete;
		AuctionHouseService& operator=(const AuctionHouseService&) = delete;

		~AuctionHouseService();

		/// Load `server.auction_listings_path` (default `auction/listings.ini`). Creates empty store if missing.
		bool Init();

		/// Persist listings and emit shutdown log.
		void Shutdown();

		const std::vector<AuctionListingRecord>& GetListings() const { return m_listings; }

		AuctionListingRecord* FindListing(uint32_t listingId);

		/// Minimum allowed next bid (5% over current, or \ref startBid if no bid yet).
		uint32_t MinimumNextBid(const AuctionListingRecord& row) const;

		/// Insert closed listing into history file (best-effort).
		bool AppendHistoryLine(std::string_view line) const;

		/// \return false when hours not in {12,24,48} or buyout < start (when buyout &gt; 0).
		bool ValidateNewListingParams(uint32_t startBid, uint32_t buyoutPrice, uint8_t durationHours, std::string& outError) const;

		/// Reserve new id and append listing; \p expiresAtTick must be computed by caller.
		uint32_t EmplaceListing(AuctionListingRecord row);

		void MarkClosed(uint32_t listingId);

		void EraseListing(uint32_t listingId);

		/// Filter/sort for browse packet (price = current bid if &gt; 0 else startBid).
		std::vector<const AuctionListingRecord*> QueryBrowse(
			uint32_t minPrice,
			uint32_t maxPrice,
			uint32_t itemIdFilter,
			uint8_t sortMode,
			uint32_t maxRows) const;

		/// Move closed/expired listings to \p outSettlements; mutates \p m_listings.
		void CollectExpired(uint32_t currentTick, std::vector<AuctionSettlement>& outSettlements);

		/// Persist current listings after in-place mutation via \ref FindListing (bids, etc.).
		bool PersistListings();

		/// ETAPE2.8 — avec une base, les annonces vivent dans `auction_listings` :
		/// le service ne lit ni n'écrit plus le fichier (à appeler AVANT \ref Init).
		void SetFilePersistence(bool enabled) { m_filePersistence = enabled; }
		bool FilePersistence() const { return m_filePersistence; }

		/// ETAPE2.8 — annonces échues à \p currentTick (copies ; rien n'est retiré).
		std::vector<AuctionListingRecord> ExpiredListings(uint32_t currentTick) const;

		/// ETAPE2.8 — règlement d'une annonce close (mêmes règles que CollectExpired :
		/// sans enchère, l'objet revient au vendeur ; sinon il va au meilleur
		/// enchérisseur et le vendeur touche le prix moins la commission).
		static AuctionSettlement SettlementFor(const AuctionListingRecord& row);

		/// ETAPE2.8 — remplace toutes les annonces en mémoire (chargement depuis la base).
		void ReplaceListings(std::vector<AuctionListingRecord> rows);

		/// ETAPE2.8 — ajoute une annonce dont l'identifiant vient de la base.
		void InsertListingWithId(const AuctionListingRecord& row);

		/// ETAPE2.8 — annonces du fichier (import unique vers la base). Vide si absent.
		std::vector<AuctionListingRecord> ReadListingsFile() const;

		/// ETAPE2.8 — renomme le fichier d'annonces en `.imported` après import en
		/// base (pour ne jamais le réimporter). \return false si le renommage échoue.
		bool RetireListingsFile() const;

	private:
		bool LoadFromContent();
		bool SaveToContent() const;

		engine::core::Config m_config;
		std::string m_relativeListingsPath;
		std::string m_relativeHistoryPath;
		std::vector<AuctionListingRecord> m_listings{};
		uint32_t m_nextListingId = 1;
		bool m_initialized = false;
		bool m_filePersistence = true; ///< ETAPE2.8 — false avec une base
	};
}
