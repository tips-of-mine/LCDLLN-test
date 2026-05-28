// Wave 5 Persistence (Phase 5.09b) - Implementation MysqlAuctionStore.
// N1-G : converti en prepared statements (LoadAllActive + Insert + UpdateBid + MarkEnded).

#include "src/masterd/auction/MysqlAuctionStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server::auctions
{
	namespace
	{
		/// Materialise une AuctionRow a partir d'un SqlPreparedStatement au FetchRow courant.
		/// L'ordre des colonnes est ALIGNE sur la query SELECT (14 colonnes).
		AuctionRow StmtToAuction(engine::server::db::SqlPreparedStatement* stmt)
		{
			AuctionRow a;
			a.auctionId               = stmt->GetUInt64(0);
			a.itemTemplateId          = static_cast<uint32_t>(stmt->GetUInt64(1));
			a.itemName                = stmt->GetString(2);
			a.count                   = static_cast<uint32_t>(stmt->GetUInt64(3));
			a.startBidCopper          = stmt->GetUInt64(4);
			a.currentBidCopper        = stmt->GetUInt64(5);
			a.buyoutCopper            = stmt->GetUInt64(6);
			a.ownerAccountId          = stmt->GetUInt64(7);
			a.ownerName               = stmt->GetString(8);
			a.highestBidderAccountId  = stmt->GetUInt64(9);
			a.highestBidderName       = stmt->GetString(10);
			a.expiresAtUnixMs         = stmt->GetUInt64(11);
			a.ended                   = (stmt->GetUInt64(12) != 0u);
			a.wonByBuyout             = (stmt->GetUInt64(13) != 0u);
			return a;
		}
	}

	bool MysqlAuctionStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<AuctionRow> MysqlAuctionStore::LoadAllActive() const
	{
		std::vector<AuctionRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		// SELECT colonnes alignees sur StmtToAuction (14 colonnes).
		auto* stmt = cache->Acquire(mysql,
			"SELECT auction_id, item_template_id, item_name, count, "
			"start_bid_copper, current_bid_copper, buyout_copper, "
			"owner_account_id, owner_name, "
			"COALESCE(highest_bidder_account_id, 0), "
			"COALESCE(highest_bidder_name, ''), "
			"expires_at_unix_ms, ended, won_by_buyout "
			"FROM auction_listings_v2 WHERE ended = 0");
		if (!stmt || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlAuctionStore] LoadAllActive query failed");
			return out;
		}
		while (stmt->FetchRow())
			out.push_back(StmtToAuction(stmt));
		LOG_INFO(Net, "[MysqlAuctionStore] LoadAllActive loaded {} active listings", out.size());
		return out;
	}

	uint64_t MysqlAuctionStore::Insert(const AuctionRow& row)
	{
		if (!IsAvailable()) return 0u;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0u;

		// AUTO_INCREMENT pour auction_id, on n'envoie pas la colonne.
		// 10 binds, ended=0 / won_by_buyout=0 littéraux (auction nouvellement créée).
		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO auction_listings_v2 ("
			"item_template_id, item_name, count, start_bid_copper, "
			"current_bid_copper, buyout_copper, owner_account_id, owner_name, "
			"expires_at_unix_ms, ended, won_by_buyout"
			") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0)");
		const bool ok = stmt
			&& stmt->Bind(0, row.itemTemplateId)
			&& stmt->Bind(1, std::string_view(row.itemName))
			&& stmt->Bind(2, row.count)
			&& stmt->Bind(3, row.startBidCopper)
			&& stmt->Bind(4, row.currentBidCopper)
			&& stmt->Bind(5, row.buyoutCopper)
			&& stmt->Bind(6, row.ownerAccountId)
			&& stmt->Bind(7, std::string_view(row.ownerName))
			&& stmt->Bind(8, row.expiresAtUnixMs)
			&& stmt->Execute();
		if (!ok)
		{
			LOG_WARN(Net, "[MysqlAuctionStore] Insert auction failed");
			return 0u;
		}
		return mysql_insert_id(mysql);
	}

	bool MysqlAuctionStore::UpdateBid(uint64_t auctionId, uint64_t newBidCopper,
		std::string_view bidderName, uint64_t bidderAccountId)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"UPDATE auction_listings_v2 SET "
			"current_bid_copper = ?, "
			"highest_bidder_account_id = ?, "
			"highest_bidder_name = ? "
			"WHERE auction_id = ?");
		const bool ok = stmt
			&& stmt->Bind(0, newBidCopper)
			&& stmt->Bind(1, bidderAccountId)
			&& stmt->Bind(2, bidderName)
			&& stmt->Bind(3, auctionId)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlAuctionStore] UpdateBid failed auctionId={}", auctionId);
		return ok;
	}

	bool MysqlAuctionStore::MarkEnded(uint64_t auctionId, bool wonByBuyout)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"UPDATE auction_listings_v2 SET ended = 1, won_by_buyout = ? "
			"WHERE auction_id = ?");
		const bool ok = stmt
			&& stmt->Bind(0, static_cast<uint32_t>(wonByBuyout ? 1u : 0u))
			&& stmt->Bind(1, auctionId)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlAuctionStore] MarkEnded failed auctionId={}", auctionId);
		return ok;
	}
}
