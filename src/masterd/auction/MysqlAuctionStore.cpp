// Wave 5 Persistence (Phase 5.09b) - Implementation MysqlAuctionStore.

#include "src/masterd/auction/MysqlAuctionStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace engine::server::auctions
{
	namespace
	{
		/// Echappe une chaine pour MySQL via mysql_real_escape_string.
		/// Retourne vide si mysql null.
		std::string EscapeMysql(MYSQL* mysql, std::string_view v)
		{
			if (!mysql) return {};
			std::vector<char> buf(v.size() * 2 + 1);
			const auto w = mysql_real_escape_string(mysql, buf.data(), v.data(),
				static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}

		/// Materialise une AuctionRow a partir d'un MYSQL_ROW.
		/// L'ordre des colonnes est ALIGNE sur la query SELECT.
		AuctionRow RowToAuction(MYSQL_ROW row)
		{
			AuctionRow a;
			if (row[0])  a.auctionId               = std::strtoull(row[0], nullptr, 10);
			if (row[1])  a.itemTemplateId          = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2])  a.itemName                = row[2];
			if (row[3])  a.count                   = static_cast<uint32_t>(std::strtoul(row[3], nullptr, 10));
			if (row[4])  a.startBidCopper          = std::strtoull(row[4], nullptr, 10);
			if (row[5])  a.currentBidCopper        = std::strtoull(row[5], nullptr, 10);
			if (row[6])  a.buyoutCopper            = std::strtoull(row[6], nullptr, 10);
			if (row[7])  a.ownerAccountId          = std::strtoull(row[7], nullptr, 10);
			if (row[8])  a.ownerName               = row[8];
			if (row[9])  a.highestBidderAccountId  = std::strtoull(row[9], nullptr, 10);
			if (row[10]) a.highestBidderName       = row[10];
			if (row[11]) a.expiresAtUnixMs         = std::strtoull(row[11], nullptr, 10);
			if (row[12]) a.ended                   = (std::strtoul(row[12], nullptr, 10) != 0u);
			if (row[13]) a.wonByBuyout             = (std::strtoul(row[13], nullptr, 10) != 0u);
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
		if (!mysql) return out;

		// SELECT colonnes alignees sur RowToAuction (14 colonnes).
		const char* sql =
			"SELECT auction_id, item_template_id, item_name, count, "
			"start_bid_copper, current_bid_copper, buyout_copper, "
			"owner_account_id, owner_name, "
			"COALESCE(highest_bidder_account_id, 0), "
			"COALESCE(highest_bidder_name, ''), "
			"expires_at_unix_ms, ended, won_by_buyout "
			"FROM auction_listings_v2 WHERE ended = 0";
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlAuctionStore] LoadAllActive query failed");
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
			out.push_back(RowToAuction(row));
		engine::server::db::DbFreeResult(res);
		LOG_INFO(Net, "[MysqlAuctionStore] LoadAllActive loaded {} active listings", out.size());
		return out;
	}

	uint64_t MysqlAuctionStore::Insert(const AuctionRow& row)
	{
		if (!IsAvailable()) return 0u;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return 0u;

		const std::string itemName  = EscapeMysql(mysql, row.itemName);
		const std::string ownerName = EscapeMysql(mysql, row.ownerName);

		char sql[1024];
		// AUTO_INCREMENT pour auction_id, on n'envoie pas la colonne.
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO auction_listings_v2 ("
			"item_template_id, item_name, count, start_bid_copper, "
			"current_bid_copper, buyout_copper, owner_account_id, owner_name, "
			"expires_at_unix_ms, ended, won_by_buyout"
			") VALUES ("
			"%u, '%s', %u, %llu, %llu, %llu, %llu, '%s', %llu, 0, 0"
			")",
			row.itemTemplateId,
			itemName.c_str(),
			row.count,
			static_cast<unsigned long long>(row.startBidCopper),
			static_cast<unsigned long long>(row.currentBidCopper),
			static_cast<unsigned long long>(row.buyoutCopper),
			static_cast<unsigned long long>(row.ownerAccountId),
			ownerName.c_str(),
			static_cast<unsigned long long>(row.expiresAtUnixMs));

		if (!engine::server::db::DbExecute(mysql, sql))
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
		if (!mysql) return false;

		const std::string name = EscapeMysql(mysql, bidderName);

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"UPDATE auction_listings_v2 SET "
			"current_bid_copper = %llu, "
			"highest_bidder_account_id = %llu, "
			"highest_bidder_name = '%s' "
			"WHERE auction_id = %llu",
			static_cast<unsigned long long>(newBidCopper),
			static_cast<unsigned long long>(bidderAccountId),
			name.c_str(),
			static_cast<unsigned long long>(auctionId));

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlAuctionStore] UpdateBid failed auctionId={}", auctionId);
		return ok;
	}

	bool MysqlAuctionStore::MarkEnded(uint64_t auctionId, bool wonByBuyout)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"UPDATE auction_listings_v2 SET ended = 1, won_by_buyout = %u "
			"WHERE auction_id = %llu",
			wonByBuyout ? 1u : 0u,
			static_cast<unsigned long long>(auctionId));

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlAuctionStore] MarkEnded failed auctionId={}", auctionId);
		return ok;
	}
}
