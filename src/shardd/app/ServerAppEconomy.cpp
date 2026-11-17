// ServerApp — économie : boutiques, hôtel des ventes (mémoire et base), échanges entre joueurs.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	bool ServerApp::SendShopOpen(const ConnectedClient& receiver, uint32_t vendorId)
	{
		const VendorDefinition* vendor = m_vendorCatalog.FindVendor(vendorId);
		if (vendor == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] SendShopOpen FAILED: unknown vendor_id={}", vendorId);
			return false;
		}

		ShopOpenMessage message{};
		message.vendorId = vendorId;
		message.displayName = vendor->displayName;
		message.offers.reserve(vendor->items.size());
		for (const VendorItemDefinition& it : vendor->items)
		{
			ShopOfferWire row{};
			row.itemId = it.itemId;
			row.buyPrice = it.buyPrice;
			if (it.stock < 0)
			{
				row.stock = kShopInfiniteStockWire;
			}
			else
			{
				const std::optional<uint32_t> rem = m_vendorStock.GetRemaining(vendorId, it.itemId);
				row.stock = rem.value_or(0u);
			}
			message.offers.push_back(row);
		}

		const std::vector<std::byte> packet = EncodeShopOpen(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendShopOpen FAILED: transport send (client_id={})", receiver.clientId);
			return false;
		}

		LOG_INFO(Net,
			"[ServerApp] SendShopOpen OK (client_id={}, vendor_id={}, offers={})",
			receiver.clientId,
			vendorId,
			message.offers.size());
		return true;
	}

	void ServerApp::HandleShopBuyRequest(const Endpoint& endpoint, const ShopBuyRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] ShopBuy ignored: unknown endpoint {}", UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] ShopBuy ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				message.clientId);
			return;
		}

		if (message.quantity == 0u || message.quantity > kMaxShopQuantityPerRequest)
		{
			LOG_WARN(Net, "[ServerApp] ShopBuy ignored: bad quantity ({})", message.quantity);
			SendChatSystemNotice(*client, "Invalid quantity.");
			return;
		}

		const VendorItemDefinition* row = m_vendorCatalog.FindVendorItem(message.vendorId, message.itemId);
		if (row == nullptr)
		{
			LOG_WARN(Net,
				"[ServerApp] ShopBuy ignored: unknown offer (vendor_id={}, item_id={})",
				message.vendorId,
				message.itemId);
			SendChatSystemNotice(*client, "Item not sold here.");
			return;
		}

		if (row->stock >= 0)
		{
			const std::optional<uint32_t> rem = m_vendorStock.GetRemaining(message.vendorId, message.itemId);
			const uint32_t have = rem.value_or(0u);
			if (have < message.quantity)
			{
				LOG_WARN(Net, "[ServerApp] ShopBuy blocked: out of stock (have={}, need={})", have, message.quantity);
				SendChatSystemNotice(*client, "Vendor is out of stock.");
				return;
			}
		}

		if (row->buyPrice > 0u && message.quantity > (std::numeric_limits<uint32_t>::max() / row->buyPrice))
		{
			LOG_WARN(Net, "[ServerApp] ShopBuy blocked: cost overflow");
			SendChatSystemNotice(*client, "Invalid purchase.");
			return;
		}

		const uint64_t totalCostU64 = static_cast<uint64_t>(row->buyPrice) * static_cast<uint64_t>(message.quantity);
		if (totalCostU64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
		{
			LOG_WARN(Net, "[ServerApp] ShopBuy blocked: cost overflow");
			SendChatSystemNotice(*client, "Invalid purchase.");
			return;
		}

		std::string walletErr;
		if (!m_playerWallet.SubtractCurrency(*client, kCurrencyGold, totalCostU64, walletErr))
		{
			LOG_WARN(Net, "[ServerApp] ShopBuy blocked: wallet ({})", walletErr);
			SendChatSystemNotice(*client, "Not enough gold.");
			return;
		}

		if (row->stock >= 0)
		{
			std::string stockErr;
			if (!m_vendorStock.TryConsume(message.vendorId, message.itemId, message.quantity, stockErr))
			{
				std::string refundErr;
				(void)m_playerWallet.AddCurrency(*client, kCurrencyGold, totalCostU64, refundErr);
				LOG_WARN(Net, "[ServerApp] ShopBuy rolled back: stock ({})", stockErr);
				SendChatSystemNotice(*client, "Vendor is out of stock.");
				return;
			}
		}

		AddItemToInventory(*client, ItemStack{ message.itemId, message.quantity });
		SaveConnectedClient(*client, "shop_buy");
		(void)SendWalletUpdate(*client);
		(void)SendInventoryDelta(
			*client,
			std::span<const ItemStack>(client->inventory.data(), client->inventory.size()));
		LOG_INFO(Net,
			"[ServerApp] ShopBuy OK (client_id={}, vendor_id={}, item_id={}, qty={})",
			client->clientId,
			message.vendorId,
			message.itemId,
			message.quantity);
	}

	void ServerApp::HandleShopSellRequest(const Endpoint& endpoint, const ShopSellRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] ShopSell ignored: unknown endpoint {}", UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] ShopSell ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				message.clientId);
			return;
		}

		if (message.quantity == 0u || message.quantity > kMaxShopQuantityPerRequest)
		{
			LOG_WARN(Net, "[ServerApp] ShopSell ignored: bad quantity ({})", message.quantity);
			SendChatSystemNotice(*client, "Invalid quantity.");
			return;
		}

		const VendorItemDefinition* row = m_vendorCatalog.FindVendorItem(message.vendorId, message.itemId);
		if (row == nullptr)
		{
			LOG_WARN(Net,
				"[ServerApp] ShopSell ignored: item not in vendor table (vendor_id={}, item_id={})",
				message.vendorId,
				message.itemId);
			SendChatSystemNotice(*client, "Cannot sell that here.");
			return;
		}

		const uint32_t unitSell = VendorCatalog::ComputeSellPrice(row->buyPrice);
		if (unitSell > 0u && message.quantity > (std::numeric_limits<uint32_t>::max() / unitSell))
		{
			LOG_WARN(Net, "[ServerApp] ShopSell blocked: gold overflow");
			SendChatSystemNotice(*client, "Invalid sale.");
			return;
		}

		const uint64_t totalGoldU64 = static_cast<uint64_t>(unitSell) * static_cast<uint64_t>(message.quantity);
		if (totalGoldU64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
		{
			LOG_WARN(Net, "[ServerApp] ShopSell blocked: gold overflow");
			SendChatSystemNotice(*client, "Invalid sale.");
			return;
		}

		std::string invErr;
		if (!RemoveStackFromInventory(*client, message.itemId, message.quantity, invErr))
		{
			LOG_WARN(Net, "[ServerApp] ShopSell blocked: inventory ({})", invErr);
			SendChatSystemNotice(*client, "Not enough items.");
			return;
		}

		std::string walletErr;
		if (totalGoldU64 > 0u && !m_playerWallet.AddCurrency(*client, kCurrencyGold, totalGoldU64, walletErr))
		{
			AddItemToInventory(*client, ItemStack{ message.itemId, message.quantity });
			LOG_WARN(Net, "[ServerApp] ShopSell rolled back: wallet ({})", walletErr);
			SendChatSystemNotice(*client, "Cannot carry more gold.");
			return;
		}

		SaveConnectedClient(*client, "shop_sell");
		(void)SendWalletUpdate(*client);
		(void)SendInventoryDelta(
			*client,
			std::span<const ItemStack>(client->inventory.data(), client->inventory.size()));
		LOG_INFO(Net,
			"[ServerApp] ShopSell OK (client_id={}, vendor_id={}, item_id={}, qty={}, gold={})",
			client->clientId,
			message.vendorId,
			message.itemId,
			message.quantity,
			totalGoldU64);
	}

	ConnectedClient* ServerApp::FindConnectedClientByCharacterKey(uint64_t characterKey)
	{
		for (ConnectedClient& client : m_clients)
		{
			if (client.persistenceCharacterKey == characterKey)
			{
				return &client;
			}
		}
		return nullptr;
	}

	bool ServerApp::SendAuctionBrowseResult(const ConnectedClient& receiver, const AuctionBrowseRequestMessage& query)
	{
		const uint32_t maxRows = std::max(1u, std::min(query.maxRows, static_cast<uint32_t>(kMaxAuctionBrowseRowsWire)));
		const uint8_t sortMode = static_cast<uint8_t>(std::min<uint32_t>(query.sortMode, 2u));
		const std::vector<const AuctionListingRecord*> rows = m_auctionHouse.QueryBrowse(
			query.minPrice,
			query.maxPrice,
			query.itemIdFilter,
			sortMode,
			maxRows);
		AuctionBrowseResultMessage msg{};
		msg.clientId = receiver.clientId;
		for (const AuctionListingRecord* p : rows)
		{
			AuctionListingWireRow w{};
			w.listingId = p->listingId;
			w.itemId = p->itemId;
			w.quantity = p->quantity;
			w.startBid = p->startBid;
			w.buyoutPrice = p->buyoutPrice;
			w.currentBid = p->currentBid;
			w.expiresAtTick = p->expiresAtTick;
			msg.rows.push_back(w);
		}
		const std::vector<std::byte> packet = EncodeAuctionBrowseResult(msg);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] AuctionBrowseResult send FAILED (client_id={})", receiver.clientId);
			return false;
		}
		LOG_INFO(Net, "[ServerApp] AuctionBrowseResult OK (client_id={}, rows={})", receiver.clientId, msg.rows.size());
		return true;
	}

	void ServerApp::HandleAuctionBrowseRequest(const Endpoint& endpoint, const AuctionBrowseRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AuctionBrowse ignored: unknown endpoint {}", UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] AuctionBrowse ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				message.clientId);
			return;
		}
		(void)SendAuctionBrowseResult(*client, message);
	}

	void ServerApp::HandleAuctionListItemRequest(const Endpoint& endpoint, const AuctionListItemRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AuctionListItem ignored: unknown endpoint {}", UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] AuctionListItem ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				message.clientId);
			return;
		}
		if (message.quantity == 0u || message.quantity > kMaxShopQuantityPerRequest)
		{
			LOG_WARN(Net, "[ServerApp] AuctionListItem ignored: bad quantity ({})", message.quantity);
			SendChatSystemNotice(*client, "Invalid quantity.");
			return;
		}
		std::string verr;
		if (!m_auctionHouse.ValidateNewListingParams(
				message.startBid,
				message.buyoutPrice,
				static_cast<uint8_t>(message.durationHours),
				verr))
		{
			LOG_WARN(Net, "[ServerApp] AuctionListItem blocked: {}", verr);
			SendChatSystemNotice(*client, "Invalid auction parameters.");
			return;
		}
		if (AuctionInDb())
		{
			HandleAuctionListItemDb(*client, message); // ETAPE2.8 — une transaction
			return;
		}
		std::string invErr;
		if (!RemoveStackFromInventory(*client, message.itemId, message.quantity, invErr))
		{
			LOG_WARN(Net, "[ServerApp] AuctionListItem blocked: {}", invErr);
			SendChatSystemNotice(*client, "You do not have that item.");
			return;
		}
		AuctionListingRecord row{};
		row.sellerClientId = client->clientId;
		row.sellerCharacterKey = client->persistenceCharacterKey;
		row.itemId = message.itemId;
		row.quantity = message.quantity;
		row.startBid = message.startBid;
		row.buyoutPrice = message.buyoutPrice;
		row.expiresAtTick = m_currentTick + message.durationHours * 3600u * static_cast<uint32_t>(m_tickHz);
		const uint32_t newId = m_auctionHouse.EmplaceListing(row);
		SaveConnectedClient(*client, "auction_list");
		(void)SendInventoryDelta(
			*client,
			std::span<const ItemStack>(client->inventory.data(), client->inventory.size()));
		SendChatSystemNotice(*client, "Item listed on auction house.");
		LOG_INFO(Net,
			"[ServerApp] AuctionListItem OK (client_id={}, listing_id={}, item_id={}, qty={})",
			client->clientId,
			newId,
			message.itemId,
			message.quantity);
	}

	void ServerApp::HandleAuctionBidRequest(const Endpoint& endpoint, const AuctionBidRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AuctionBid ignored: unknown endpoint {}", UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] AuctionBid ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				message.clientId);
			return;
		}
		AuctionListingRecord* row = m_auctionHouse.FindListing(message.listingId);
		if (row == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AuctionBid ignored: unknown listing_id={}", message.listingId);
			SendChatSystemNotice(*client, "That listing is gone.");
			return;
		}
		// ETAPE4.2 — les échéances sont réglées une fois par seconde : une annonce
		// échue mais pas encore réglée n'accepte plus d'offre.
		if (row->expiresAtTick != 0u && m_currentTick >= row->expiresAtTick)
		{
			SendChatSystemNotice(*client, "That auction has ended.");
			return;
		}
		if (row->sellerCharacterKey == client->persistenceCharacterKey)
		{
			SendChatSystemNotice(*client, "You cannot bid on your own auction.");
			LOG_WARN(Net, "[ServerApp] AuctionBid blocked: own listing (client_id={})", client->clientId);
			return;
		}
		const uint32_t minBid = m_auctionHouse.MinimumNextBid(*row);
		if (message.bidAmount < minBid)
		{
			SendChatSystemNotice(*client, "Bid too low.");
			LOG_WARN(Net, "[ServerApp] AuctionBid blocked: bid {} < min {}", message.bidAmount, minBid);
			return;
		}
		if (row->currentBid > 0u && message.bidAmount <= row->currentBid)
		{
			SendChatSystemNotice(*client, "Bid too low.");
			return;
		}
		if (AuctionInDb())
		{
			HandleAuctionBidDb(*client, *row, message.bidAmount); // ETAPE2.8 — une transaction
			return;
		}
		const uint64_t prevCk = row->highBidderCharacterKey;
		const uint32_t prevBid = row->currentBid;
		const uint64_t bidderCk = client->persistenceCharacterKey;
		uint64_t charge = message.bidAmount;
		if (prevCk == bidderCk && prevBid > 0u)
		{
			charge = static_cast<uint64_t>(message.bidAmount) - static_cast<uint64_t>(prevBid);
		}
		std::string werr;
		if (charge > 0u
			&& !m_playerWallet.SubtractCurrency(*client, kCurrencyGold, charge, werr))
		{
			LOG_WARN(Net, "[ServerApp] AuctionBid blocked: wallet ({})", werr);
			SendChatSystemNotice(*client, "Not enough gold.");
			return;
		}
		if (charge > 0u)
		{
			(void)SendWalletUpdate(*client);
			SaveConnectedClient(*client, "auction_bid_charge");
		}
		if (prevCk != 0u && prevCk != bidderCk)
		{
			RefundGoldToCharacter(prevCk, prevBid, "outbid");
			ConnectedClient* prev = FindConnectedClientByCharacterKey(prevCk);
			if (prev != nullptr)
			{
				SendChatSystemNotice(*prev, "You were outbid.");
			}
		}
		row->currentBid = message.bidAmount;
		row->highBidderCharacterKey = bidderCk;
		row->highBidderClientId = client->clientId;
		(void)m_auctionHouse.PersistListings();
		SendChatSystemNotice(*client, "Bid accepted.");
		LOG_INFO(Net,
			"[ServerApp] AuctionBid OK (client_id={}, listing_id={}, bid={})",
			client->clientId,
			message.listingId,
			message.bidAmount);
	}

	void ServerApp::HandleAuctionBuyoutRequest(const Endpoint& endpoint, const AuctionBuyoutRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AuctionBuyout ignored: unknown endpoint {}", UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] AuctionBuyout ignored: client_id mismatch (expected={}, got={})",
				client->clientId,
				message.clientId);
			return;
		}
		AuctionListingRecord* row = m_auctionHouse.FindListing(message.listingId);
		if (row == nullptr || row->buyoutPrice == 0u)
		{
			LOG_WARN(Net, "[ServerApp] AuctionBuyout ignored: invalid listing_id={}", message.listingId);
			SendChatSystemNotice(*client, "Buyout not available.");
			return;
		}
		// ETAPE4.2 — annonce échue en attente de règlement (au plus 1 s) : plus d'achat.
		if (row->expiresAtTick != 0u && m_currentTick >= row->expiresAtTick)
		{
			SendChatSystemNotice(*client, "That auction has ended.");
			return;
		}
		if (row->sellerCharacterKey == client->persistenceCharacterKey)
		{
			SendChatSystemNotice(*client, "You cannot buy out your own auction.");
			return;
		}
		if (AuctionInDb())
		{
			HandleAuctionBuyoutDb(*client, *row); // ETAPE2.8 — une transaction
			return;
		}
		const uint64_t buyerCk = client->persistenceCharacterKey;
		uint64_t charge = row->buyoutPrice;
		if (row->highBidderCharacterKey == buyerCk && row->currentBid > 0u)
		{
			charge = static_cast<uint64_t>(row->buyoutPrice) - static_cast<uint64_t>(row->currentBid);
		}
		std::string werr;
		if (charge > 0u && !m_playerWallet.SubtractCurrency(*client, kCurrencyGold, charge, werr))
		{
			LOG_WARN(Net, "[ServerApp] AuctionBuyout blocked: wallet ({})", werr);
			SendChatSystemNotice(*client, "Not enough gold.");
			return;
		}
		if (charge > 0u)
		{
			(void)SendWalletUpdate(*client);
			SaveConnectedClient(*client, "auction_buyout_charge");
		}
		const uint64_t hiCk = row->highBidderCharacterKey;
		const uint32_t hiBid = row->currentBid;
		if (hiCk != 0u && hiCk != buyerCk)
		{
			RefundGoldToCharacter(hiCk, hiBid, "buyout_refund");
			ConnectedClient* prev = FindConnectedClientByCharacterKey(hiCk);
			if (prev != nullptr)
			{
				SendChatSystemNotice(*prev, "Auction ended (buyout). Your bid was refunded.");
			}
		}
		AuctionSettlement settlement{};
		settlement.listingId = row->listingId;
		settlement.item = ItemStack{ row->itemId, row->quantity };
		settlement.sellerCharacterKey = row->sellerCharacterKey;
		settlement.buyerCharacterKey = buyerCk;
		settlement.buyerClientId = client->clientId;
		settlement.finalPrice = row->buyoutPrice;
		settlement.sellerProceeds =
			(row->buyoutPrice * static_cast<uint32_t>(100u - kAuctionHouseFeePercent)) / 100u;
		settlement.expiredWithoutBids = false;
		const uint32_t lid = row->listingId;
		const uint32_t buyoutPaid = row->buyoutPrice;
		m_auctionHouse.EraseListing(lid);
		ApplyAuctionSettlement(settlement);
		SendChatSystemNotice(*client, "Buyout successful.");
		LOG_INFO(Net,
			"[ServerApp] AuctionBuyout OK (client_id={}, listing_id={}, price={})",
			client->clientId,
			lid,
			buyoutPaid);
	}

	// =========================================================================
	// ETAPE2.8 — hôtel des ventes en base : chaque opération (mise en vente,
	// enchère, achat immédiat, fin d'enchère) écrit l'annonce, les joueurs en ligne
	// concernés et le courrier des absents dans UNE transaction, identifiée par un
	// identifiant d'opération unique (auction_operations) : rejouée après une
	// réponse de COMMIT perdue, elle n'est jamais appliquée deux fois.
	// =========================================================================

	bool ServerApp::AuctionInDb() const
	{
#if defined(SHARD_POSITION_DB)
		return m_characterDbPool != nullptr && !m_auctionHouse.FilePersistence();
#else
		return false;
#endif
	}

	void ServerApp::LoadAuctionListingsFromDb()
	{
#if defined(SHARD_POSITION_DB)
		if (!AuctionInDb())
		{
			return;
		}
		ImportAuctionListingsFile();
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		auto* sel = (mysql != nullptr && cache != nullptr) ? cache->Acquire(mysql,
			"SELECT listing_id, seller_character_id, item_id, quantity, start_bid, buyout_price, current_bid, "
			"high_bidder_character_id, GREATEST(0, TIMESTAMPDIFF(SECOND, UTC_TIMESTAMP(), expires_at)) "
			"FROM auction_listings WHERE shard_id = ? ORDER BY listing_id") : nullptr;
		// FIX.8 — étape en échec + cause MySQL. Le 29/10/2026 la cause sortait vide : la
		// préparation avait échoué et son message disparaissait avec le statement fermé
		// (mysql_error(mysql) ne le porte pas). Le cache le conserve désormais.
		const char* failedStep = nullptr;
		std::string cause;
		if (mysql == nullptr || cache == nullptr)
		{
			failedStep = "connexion";
			cause = "aucune connexion disponible dans le pool";
		}
		else if (sel == nullptr)
		{
			failedStep = "preparation";
			cause = cache->LastAcquireError();
		}
		else if (!sel->Bind(0, m_shardId))
		{
			failedStep = "parametre";
			cause = "Bind(shard_id) refuse (nombre de parametres inattendu)";
		}
		else if (!sel->Execute())
		{
			failedStep = "execution";
			cause = sel->LastError();
		}
		if (failedStep != nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] Auction listings load FAILED (shard_id={}, etape={}, cause='{}') : hôtel des ventes vide "
				"(table auction_listings absente ? migration 0081 appliquée par le master ?)", m_shardId, failedStep, cause);
			return;
		}
		LOG_INFO(Net, "[ServerApp] Auction listings : lecture en base OK (shard_id={})", m_shardId);
		std::vector<AuctionListingRecord> rows;
		while (sel->FetchRow())
		{
			AuctionListingRecord r{};
			r.listingId = static_cast<uint32_t>(sel->GetUInt64(0));
			r.sellerCharacterKey = sel->GetUInt64(1);
			r.itemId = static_cast<uint32_t>(sel->GetUInt64(2));
			r.quantity = static_cast<uint32_t>(sel->GetUInt64(3));
			r.startBid = static_cast<uint32_t>(sel->GetUInt64(4));
			r.buyoutPrice = static_cast<uint32_t>(sel->GetUInt64(5));
			r.currentBid = static_cast<uint32_t>(sel->GetUInt64(6));
			r.highBidderCharacterKey = sel->GetUInt64(7);
			const uint64_t remainingSec = sel->GetUInt64(8);
			// Échéance en ticks du processus courant (au moins un tick : une annonce
			// échue pendant l'arrêt est réglée au premier tick).
			r.expiresAtTick = m_currentTick + 1u + static_cast<uint32_t>(std::min<uint64_t>(
				remainingSec * static_cast<uint64_t>(m_tickHz), std::numeric_limits<uint32_t>::max() / 2u));
			rows.push_back(r);
		}
		m_auctionHouse.ReplaceListings(std::move(rows));
#endif
	}

	void ServerApp::ImportAuctionListingsFile()
	{
#if defined(SHARD_POSITION_DB)
		// Import UNIQUE des annonces de l'ancien fichier (objets et enchères en
		// séquestre) : toutes les annonces + un marqueur « import-file-s<shard> » dans
		// UNE transaction. Marqueur déjà présent = déjà importé (même si le fichier
		// n'a pas pu être renommé) : rien n'est réimporté.
		const std::vector<AuctionListingRecord> fileRows = m_auctionHouse.ReadListingsFile();
		if (fileRows.empty())
		{
			return;
		}
		const std::string marker = "import-file-s" + std::to_string(m_shardId);
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr || mysql_query(mysql, "START TRANSACTION") != 0)
		{
			LOG_ERROR(Net, "[ServerApp] Auction file import skipped: DB unavailable");
			return;
		}
		bool ok = true;
		bool alreadyImported = false;
		auto* mark = cache->Acquire(mysql,
			"INSERT IGNORE INTO auction_operations (operation_id, listing_id, kind) VALUES (?, 0, 'import')");
		ok = mark != nullptr && mark->Bind(0, std::string_view(marker)) && mark->Execute();
		if (ok && mark->AffectedRows() == 0u)
		{
			alreadyImported = true;
		}
		for (size_t i = 0; ok && !alreadyImported && i < fileRows.size(); ++i)
		{
			const AuctionListingRecord& r = fileRows[i];
			auto* ins = cache->Acquire(mysql,
				"INSERT INTO auction_listings (shard_id, seller_character_id, item_id, quantity, start_bid, buyout_price, "
				"current_bid, high_bidder_character_id, expires_at) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?, UTC_TIMESTAMP() + INTERVAL 1 DAY)");
			ok = ins != nullptr && ins->Bind(0, m_shardId) && ins->Bind(1, r.sellerCharacterKey) && ins->Bind(2, r.itemId)
				&& ins->Bind(3, r.quantity) && ins->Bind(4, r.startBid) && ins->Bind(5, r.buyoutPrice)
				&& ins->Bind(6, r.currentBid) && ins->Bind(7, r.highBidderCharacterKey) && ins->Execute();
		}
		if (!ok || mysql_query(mysql, "COMMIT") != 0)
		{
			mysql_query(mysql, "ROLLBACK");
			LOG_ERROR(Net, "[ServerApp] Auction file import FAILED (rolled back, retried at next start)");
			return;
		}
		LOG_WARN(Net, "[ServerApp] Auction file {} ({} listing(s))",
			alreadyImported ? "already imported, ignored" : "imported", fileRows.size());
		if (!m_auctionHouse.RetireListingsFile())
		{
			LOG_WARN(Net, "[ServerApp] Auction file could not be renamed: kept, never imported again (marker {})", marker);
		}
#endif
	}

	ServerApp::CharacterDbSaveResult ServerApp::ExecuteAuctionTx(AuctionTxBuilder& tx, uint32_t* outListingId)
	{
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr)
		{
			return CharacterDbSaveResult::SkippedNoDb;
		}
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
		const char* kind = tx.plan.op == AuctionListingOp::Insert ? "list"
			: (tx.plan.op == AuctionListingOp::UpdateBid ? "bid" : "settle");
		const std::string operationId = MakeCharacterOperationId(
			std::string("auction_") + kind, m_shardId, nowMs, ++m_characterOperationCounter);
		CharacterDbSaveResult result = CharacterDbSaveResult::Failed;
		int attempt = 0;
		int replans = 0;
		while (attempt < 3)
		{
			++attempt;
			// Les sauvegardes en file des joueurs concernés sont plus anciennes : elles
			// passent avant (sinon un état ancien écraserait celui de l'opération).
			std::vector<ConnectedClient*> characters = tx.Characters();
			std::sort(characters.begin(), characters.end(), [](const ConnectedClient* a, const ConnectedClient* b) {
				return a->persistenceCharacterKey < b->persistenceCharacterKey; // ordre des verrous
			});
			std::vector<PersistedCharacterState> states;
			std::vector<std::pair<float, uint64_t>> extras; // yaw, génération du verrou
			bool flushed = true;
			for (const ConnectedClient* client : characters)
			{
				if (m_characterSaveQueue != nullptr && !m_characterSaveQueue->Flush(client->persistenceCharacterKey))
				{
					flushed = false;
					break;
				}
				states.push_back(BuildPersistedCharacterState(*client));
				extras.emplace_back(client->yawRadians, client->ownerGeneration);
				m_savedBlocks.Invalidate(client->persistenceCharacterKey); // ETAPE5.5 — écrit par l'opération
			}
			if (!flushed)
			{
				LOG_ERROR(Net, "[ServerApp] Auction tx: pending saves not finished (operation={})", operationId);
				return CharacterDbSaveResult::Failed;
			}
			bool connected = false;
			bool commitUncertain = false;
			uint64_t refusedCharacterKey = 0;
			result = ExecuteAuctionTxOnce(tx.plan, states, extras, operationId, outListingId,
				connected, commitUncertain, refusedCharacterKey);
			if (commitUncertain)
			{
				tx.commitUncertain = true; // réponse perdue : vérifié par l'essai suivant
			}
			// Un joueur crédité dont ce shard n'est plus propriétaire (il a changé de
			// shard) : son gain part au courrier et on recommence (sans compter l'essai).
			if (result == CharacterDbSaveResult::RefusedNotOwner && refusedCharacterKey != 0u && replans < 4
				&& tx.ReplanAsMail(refusedCharacterKey))
			{
				LOG_WARN(Net, "[ServerApp] Auction tx: character_key={} no longer owned here, credited by mail (operation={})",
					refusedCharacterKey, operationId);
				++replans;
				--attempt;
				continue;
			}
			if (result != CharacterDbSaveResult::Failed || !connected)
			{
				break;
			}
			if (attempt < 3)
			{
				LOG_WARN(Net, "[ServerApp] Auction tx retry {} (operation={})", attempt, operationId);
				std::this_thread::sleep_for(std::chrono::milliseconds(20 * attempt));
			}
		}
		if (result == CharacterDbSaveResult::Failed && tx.commitUncertain)
		{
			// Un COMMIT dont la réponse s'est perdue a peut-être été validé, et aucune
			// relecture n'a pu le confirmer : on garde l'état en mémoire tel quel
			// (pas d'annulation) pour ne jamais rendre deux fois ce qui a été payé.
			LOG_ERROR(Net, "[ServerApp] Auction tx outcome UNKNOWN (operation={}) : memory kept as applied, check auction_operations",
				operationId);
			return CharacterDbSaveResult::Saved;
		}
		return result;
#else
		(void)tx;
		(void)outListingId;
		return CharacterDbSaveResult::SkippedNoDb;
#endif
	}

	ServerApp::CharacterDbSaveResult ServerApp::ExecuteAuctionTxOnce(const AuctionTxPlan& plan,
		const std::vector<PersistedCharacterState>& states, const std::vector<std::pair<float, uint64_t>>& extras,
		const std::string& operationId, uint32_t* outListingId, bool& outConnected, bool& outCommitUncertain,
		uint64_t& outRefusedCharacterKey)
	{
		outConnected = false;
		outCommitUncertain = false;
		outRefusedCharacterKey = 0;
#if defined(SHARD_POSITION_DB)
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
		{
			return CharacterDbSaveResult::Failed;
		}
		outConnected = true;
		(void)mysql_query(mysql, "SET TRANSACTION ISOLATION LEVEL READ COMMITTED");
		if (mysql_query(mysql, "START TRANSACTION") != 0)
		{
			return CharacterDbSaveResult::Failed;
		}
		const auto rollback = [mysql](CharacterDbSaveResult r) {
			mysql_query(mysql, "ROLLBACK");
			return r;
		};
		// 0. Opération déjà validée par un essai précédent (réponse du COMMIT perdue) ?
		{
			auto* done = cache->Acquire(mysql, "SELECT listing_id FROM auction_operations WHERE operation_id = ?");
			if (done == nullptr || !done->Bind(0, std::string_view(operationId)) || !done->Execute())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
			if (done->FetchRow())
			{
				if (outListingId != nullptr)
				{
					*outListingId = static_cast<uint32_t>(done->GetUInt64(0));
				}
				while (done->FetchRow()) {}
				mysql_query(mysql, "ROLLBACK");
				return CharacterDbSaveResult::Saved;
			}
		}
		// 1. L'annonce : une annonce supprimée ou absente = déjà réglée (AlreadyApplied).
		const AuctionListingRecord& l = plan.listing;
		uint32_t listingId = l.listingId;
		switch (plan.op)
		{
		case AuctionListingOp::Insert:
		{
			auto* ins = cache->Acquire(mysql,
				"INSERT INTO auction_listings (shard_id, seller_character_id, item_id, quantity, start_bid, buyout_price, "
				"current_bid, high_bidder_character_id, expires_at) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?, UTC_TIMESTAMP() + INTERVAL ? SECOND)");
			if (ins == nullptr || !ins->Bind(0, m_shardId) || !ins->Bind(1, l.sellerCharacterKey) || !ins->Bind(2, l.itemId)
				|| !ins->Bind(3, l.quantity) || !ins->Bind(4, l.startBid) || !ins->Bind(5, l.buyoutPrice)
				|| !ins->Bind(6, l.currentBid) || !ins->Bind(7, l.highBidderCharacterKey)
				|| !ins->Bind(8, plan.durationSeconds) || !ins->Execute())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
			auto* id = cache->Acquire(mysql, "SELECT LAST_INSERT_ID()");
			if (id == nullptr || !id->Execute() || !id->FetchRow())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
			listingId = static_cast<uint32_t>(id->GetUInt64(0));
			while (id->FetchRow()) {}
			break;
		}
		case AuctionListingOp::UpdateBid:
		{
			auto* upd = cache->Acquire(mysql,
				"UPDATE auction_listings SET current_bid = ?, high_bidder_character_id = ? WHERE listing_id = ? AND shard_id = ?");
			if (upd == nullptr || !upd->Bind(0, l.currentBid) || !upd->Bind(1, l.highBidderCharacterKey)
				|| !upd->Bind(2, l.listingId) || !upd->Bind(3, m_shardId) || !upd->Execute())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
			if (upd->AffectedRows() == 0u) // lignes correspondantes (CLIENT_FOUND_ROWS) : 0 = annonce absente
			{
				return rollback(CharacterDbSaveResult::AlreadyApplied);
			}
			break;
		}
		case AuctionListingOp::Delete:
		{
			auto* del = cache->Acquire(mysql, "DELETE FROM auction_listings WHERE listing_id = ? AND shard_id = ?");
			if (del == nullptr || !del->Bind(0, l.listingId) || !del->Bind(1, m_shardId) || !del->Execute())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
			if (del->AffectedRows() == 0u)
			{
				return rollback(CharacterDbSaveResult::AlreadyApplied);
			}
			break;
		}
		}
		// 2. Les joueurs en ligne concernés (état complet, contrôle de propriété 1.2).
		for (size_t i = 0; i < states.size(); ++i)
		{
			const CharacterDbSaveResult written =
				WriteCharacterStateInTx(mysql, cache, states[i], extras[i].first, extras[i].second);
			if (written != CharacterDbSaveResult::Saved)
			{
				if (written == CharacterDbSaveResult::RefusedNotOwner)
				{
					outRefusedCharacterKey = states[i].characterKey;
				}
				return rollback(written);
			}
		}
		// 3. Le courrier des joueurs absents.
		for (const AuctionMailRow& mail : plan.mail)
		{
			auto* ins = cache->Acquire(mysql,
				"INSERT INTO character_mailbox (character_id, gold, item_id, quantity, source) VALUES (?, ?, ?, ?, 'auction')");
			if (ins == nullptr || !ins->Bind(0, mail.characterId) || !ins->Bind(1, mail.gold)
				|| !ins->Bind(2, mail.item.itemId) || !ins->Bind(3, mail.item.quantity) || !ins->Execute())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
		}
		// 4. L'opération elle-même : sa présence protège contre un second passage.
		{
			const char* kind = plan.op == AuctionListingOp::Insert ? "list"
				: (plan.op == AuctionListingOp::UpdateBid ? "bid" : "settle");
			auto* op = cache->Acquire(mysql,
				"INSERT INTO auction_operations (operation_id, listing_id, kind) VALUES (?, ?, ?)");
			if (op == nullptr || !op->Bind(0, std::string_view(operationId)) || !op->Bind(1, listingId)
				|| !op->Bind(2, std::string_view(kind)) || !op->Execute())
			{
				return rollback(CharacterDbSaveResult::Failed);
			}
		}
		if (mysql_query(mysql, "COMMIT") != 0)
		{
			outCommitUncertain = true;
			return rollback(CharacterDbSaveResult::Failed);
		}
		if (outListingId != nullptr)
		{
			*outListingId = listingId;
		}
		return CharacterDbSaveResult::Saved;
#else
		(void)plan; (void)states; (void)extras; (void)operationId; (void)outListingId;
		(void)outCommitUncertain; (void)outRefusedCharacterKey;
		return CharacterDbSaveResult::SkippedNoDb;
#endif
	}

	ServerApp::AuctionTxBuilder::Saved* ServerApp::AuctionTxBuilder::Find(ConnectedClient& client)
	{
		for (Saved& s : saved)
		{
			if (s.client == &client)
			{
				return &s;
			}
		}
		return nullptr;
	}

	ServerApp::AuctionTxBuilder::Saved& ServerApp::AuctionTxBuilder::Remember(ConnectedClient& client, bool isPayer)
	{
		if (Saved* existing = Find(client))
		{
			existing->isPayer = existing->isPayer || isPayer;
			return *existing;
		}
		saved.push_back(Saved{ &client, client.gold, client.inventory, client.cakeExpiresAtMsUtcByItemId, isPayer, 0u, {}, false });
		return saved.back();
	}

	std::vector<ConnectedClient*> ServerApp::AuctionTxBuilder::Characters() const
	{
		std::vector<ConnectedClient*> out;
		for (const Saved& s : saved)
		{
			if (!s.mailed)
			{
				out.push_back(s.client);
			}
		}
		return out;
	}

	bool ServerApp::AuctionTxBuilder::ReplanAsMail(uint64_t characterKey)
	{
		for (Saved& s : saved)
		{
			if (s.mailed || s.client->persistenceCharacterKey != characterKey)
			{
				continue;
			}
			if (s.isPayer)
			{
				return false; // le payeur doit être écrit : l'opération échoue
			}
			// Remet son état en mémoire et envoie ses gains au courrier.
			s.client->gold = s.gold;
			s.client->inventory = s.inventory;
			s.client->cakeExpiresAtMsUtcByItemId = s.cakes;
			if (s.creditGold != 0u)
			{
				plan.mail.push_back(AuctionMailRow{ characterKey, s.creditGold, ItemStack{} });
			}
			for (const ItemStack& item : s.creditItems)
			{
				plan.mail.push_back(AuctionMailRow{ characterKey, 0u, item });
			}
			s.mailed = true;
			return true;
		}
		return false;
	}

	void ServerApp::AuctionTxBuilder::Revert()
	{
		for (Saved& s : saved)
		{
			if (s.mailed)
			{
				continue; // déjà remis en l'état par ReplanAsMail
			}
			s.client->gold = s.gold;
			s.client->inventory = s.inventory;
			s.client->cakeExpiresAtMsUtcByItemId = s.cakes;
		}
		saved.clear();
	}

	void ServerApp::PlanAuctionCredit(AuctionTxBuilder& tx, uint64_t characterKey, uint32_t gold, const ItemStack* item)
	{
		const bool hasItem = item != nullptr && item->itemId != 0u && item->quantity != 0u;
		if (characterKey == 0u || (gold == 0u && !hasItem))
		{
			return;
		}
		ConnectedClient* online = FindConnectedClientByCharacterKey(characterKey);
		if (online == nullptr)
		{
			tx.plan.mail.push_back(AuctionMailRow{ characterKey, gold, hasItem ? *item : ItemStack{} });
			return;
		}
		AuctionTxBuilder::Saved& saved = tx.Remember(*online, false);
		if (gold != 0u)
		{
			std::string werr;
			if (m_playerWallet.AddCurrency(*online, kCurrencyGold, gold, werr))
			{
				saved.creditGold += gold;
			}
			else
			{
				tx.plan.mail.push_back(AuctionMailRow{ characterKey, gold, ItemStack{} }); // bourse pleine
			}
		}
		if (hasItem)
		{
			AddItemToInventory(*online, *item);
			saved.creditItems.push_back(*item);
		}
	}

	void ServerApp::NotifyAuctionParticipants(const AuctionTxBuilder& tx)
	{
		for (const auto& s : tx.saved)
		{
			if (s.mailed)
			{
				continue;
			}
			(void)SendWalletUpdate(*s.client);
			(void)SendInventoryDelta(*s.client,
				std::span<const ItemStack>(s.client->inventory.data(), s.client->inventory.size()));
		}
	}

	ServerApp::CharacterDbSaveResult ServerApp::SettleAuctionInDb(const AuctionSettlement& s, uint32_t refundGold,
		uint64_t refundCharacterKey, ConnectedClient* payer, uint64_t payerCharge)
	{
		AuctionTxBuilder tx{};
		tx.plan.op = AuctionListingOp::Delete;
		tx.plan.listing.listingId = s.listingId;
		tx.plan.listing.sellerCharacterKey = s.sellerCharacterKey;
		if (payer != nullptr)
		{
			tx.Remember(*payer, true);
			std::string werr;
			if (payerCharge > 0u && !m_playerWallet.SubtractCurrency(*payer, kCurrencyGold, payerCharge, werr))
			{
				tx.Revert();
				SendChatSystemNotice(*payer, "Not enough gold.");
				return CharacterDbSaveResult::Failed;
			}
		}
		if (refundCharacterKey != 0u && refundGold != 0u)
		{
			PlanAuctionCredit(tx, refundCharacterKey, refundGold, nullptr);
		}
		if (s.expiredWithoutBids)
		{
			PlanAuctionCredit(tx, s.sellerCharacterKey, 0u, &s.item);
		}
		else
		{
			PlanAuctionCredit(tx, s.buyerCharacterKey, 0u, &s.item);
			PlanAuctionCredit(tx, s.sellerCharacterKey, s.sellerProceeds, nullptr);
		}
		const CharacterDbSaveResult result = ExecuteAuctionTx(tx, nullptr);
		if (result != CharacterDbSaveResult::Saved)
		{
			tx.Revert();
			LOG_ERROR(Net, "[ServerApp] Auction settlement not applied (listing_id={}, result={})",
				s.listingId, static_cast<int>(result));
			return result;
		}
		NotifyAuctionParticipants(tx);
		{
			std::ostringstream hist;
			hist << "listing=" << s.listingId << " final=" << s.finalPrice << " seller_ck=" << s.sellerCharacterKey
				<< " buyer_ck=" << s.buyerCharacterKey << " no_bids=" << (s.expiredWithoutBids ? 1 : 0);
			(void)m_auctionHouse.AppendHistoryLine(hist.str());
		}
		if (ConnectedClient* seller = FindConnectedClientByCharacterKey(s.sellerCharacterKey))
		{
			SendChatSystemNotice(*seller, s.expiredWithoutBids ? "Auction expired. Item returned." : "Your auction sold.");
		}
		if (!s.expiredWithoutBids)
		{
			if (ConnectedClient* buyer = FindConnectedClientByCharacterKey(s.buyerCharacterKey))
			{
				SendChatSystemNotice(*buyer, "You won an auction: item received.");
			}
		}
		LOG_INFO(Net, "[ServerApp] Auction settled in DB (listing_id={}, no_bids={}, mail_rows={})",
			s.listingId, s.expiredWithoutBids, tx.plan.mail.size());
		return CharacterDbSaveResult::Saved;
	}

	void ServerApp::HandleAuctionListItemDb(ConnectedClient& client, const AuctionListItemRequestMessage& message)
	{
		AuctionTxBuilder tx{};
		tx.Remember(client, true);
		std::string invErr;
		if (!RemoveStackFromInventory(client, message.itemId, message.quantity, invErr))
		{
			tx.Revert();
			LOG_WARN(Net, "[ServerApp] AuctionListItem blocked: {}", invErr);
			SendChatSystemNotice(client, "You do not have that item.");
			return;
		}
		tx.plan.op = AuctionListingOp::Insert;
		tx.plan.listing.sellerClientId = client.clientId;
		tx.plan.listing.sellerCharacterKey = client.persistenceCharacterKey;
		tx.plan.listing.itemId = message.itemId;
		tx.plan.listing.quantity = message.quantity;
		tx.plan.listing.startBid = message.startBid;
		tx.plan.listing.buyoutPrice = message.buyoutPrice;
		tx.plan.durationSeconds = static_cast<uint32_t>(message.durationHours) * 3600u;
		uint32_t listingId = 0;
		const CharacterDbSaveResult result = ExecuteAuctionTx(tx, &listingId);
		if (result != CharacterDbSaveResult::Saved || listingId == 0u)
		{
			if (!(result == CharacterDbSaveResult::Saved && tx.commitUncertain))
			{
				tx.Revert(); // l'objet revient dans le sac
			}
			SendChatSystemNotice(client, "Auction house unavailable, try again later.");
			LOG_ERROR(Net, "[ServerApp] AuctionListItem FAILED in DB (client_id={}, item_id={})", client.clientId, message.itemId);
			return;
		}
		AuctionListingRecord row = tx.plan.listing;
		row.listingId = listingId;
		row.expiresAtTick = m_currentTick + message.durationHours * 3600u * static_cast<uint32_t>(m_tickHz);
		m_auctionHouse.InsertListingWithId(row);
		(void)SendInventoryDelta(client, std::span<const ItemStack>(client.inventory.data(), client.inventory.size()));
		SendChatSystemNotice(client, "Item listed on auction house.");
		LOG_INFO(Net, "[ServerApp] AuctionListItem OK (db, client_id={}, listing_id={}, item_id={}, qty={})",
			client.clientId, listingId, message.itemId, message.quantity);
	}

	void ServerApp::HandleAuctionBidDb(ConnectedClient& client, AuctionListingRecord& row, uint32_t bidAmount)
	{
		const uint64_t prevCk = row.highBidderCharacterKey;
		const uint32_t prevBid = row.currentBid;
		const uint64_t bidderCk = client.persistenceCharacterKey;
		uint64_t charge = bidAmount;
		if (prevCk == bidderCk && prevBid > 0u)
		{
			charge = static_cast<uint64_t>(bidAmount) - static_cast<uint64_t>(prevBid);
		}
		AuctionTxBuilder tx{};
		tx.Remember(client, true);
		std::string werr;
		if (charge > 0u && !m_playerWallet.SubtractCurrency(client, kCurrencyGold, charge, werr))
		{
			tx.Revert();
			LOG_WARN(Net, "[ServerApp] AuctionBid blocked: wallet ({})", werr);
			SendChatSystemNotice(client, "Not enough gold.");
			return;
		}
		// Remboursement du précédent enchérisseur dans la même transaction.
		if (prevCk != 0u && prevCk != bidderCk)
		{
			PlanAuctionCredit(tx, prevCk, prevBid, nullptr);
		}
		tx.plan.op = AuctionListingOp::UpdateBid;
		tx.plan.listing = row;
		tx.plan.listing.currentBid = bidAmount;
		tx.plan.listing.highBidderCharacterKey = bidderCk;
		const CharacterDbSaveResult result = ExecuteAuctionTx(tx, nullptr);
		if (result != CharacterDbSaveResult::Saved)
		{
			tx.Revert();
			if (result == CharacterDbSaveResult::AlreadyApplied)
			{
				m_auctionHouse.EraseListing(row.listingId); // annonce disparue de la base
				SendChatSystemNotice(client, "That listing is gone.");
				return;
			}
			SendChatSystemNotice(client, "Auction house unavailable, try again later.");
			LOG_ERROR(Net, "[ServerApp] AuctionBid FAILED in DB (client_id={}, listing_id={})", client.clientId, row.listingId);
			return;
		}
		row.currentBid = bidAmount;
		row.highBidderCharacterKey = bidderCk;
		row.highBidderClientId = client.clientId;
		NotifyAuctionParticipants(tx);
		if (prevCk != 0u && prevCk != bidderCk)
		{
			if (ConnectedClient* prev = FindConnectedClientByCharacterKey(prevCk))
			{
				SendChatSystemNotice(*prev, "You were outbid.");
			}
		}
		SendChatSystemNotice(client, "Bid accepted.");
		LOG_INFO(Net, "[ServerApp] AuctionBid OK (db, client_id={}, listing_id={}, bid={})", client.clientId, row.listingId, bidAmount);
	}

	void ServerApp::HandleAuctionBuyoutDb(ConnectedClient& client, const AuctionListingRecord& row)
	{
		const uint64_t buyerCk = client.persistenceCharacterKey;
		uint64_t charge = row.buyoutPrice;
		if (row.highBidderCharacterKey == buyerCk && row.currentBid > 0u)
		{
			charge = static_cast<uint64_t>(row.buyoutPrice) - static_cast<uint64_t>(row.currentBid);
		}
		AuctionSettlement settlement{};
		settlement.listingId = row.listingId;
		settlement.item = ItemStack{ row.itemId, row.quantity };
		settlement.sellerCharacterKey = row.sellerCharacterKey;
		settlement.buyerCharacterKey = buyerCk;
		settlement.buyerClientId = client.clientId;
		settlement.finalPrice = row.buyoutPrice;
		settlement.sellerProceeds = (row.buyoutPrice * static_cast<uint32_t>(100u - kAuctionHouseFeePercent)) / 100u;
		const uint64_t hiCk = row.highBidderCharacterKey;
		const uint32_t hiBid = row.currentBid;
		const bool refundOther = hiCk != 0u && hiCk != buyerCk;
		const uint32_t listingId = row.listingId;
		const CharacterDbSaveResult result =
			SettleAuctionInDb(settlement, refundOther ? hiBid : 0u, refundOther ? hiCk : 0u, &client, charge);
		if (result == CharacterDbSaveResult::AlreadyApplied)
		{
			m_auctionHouse.EraseListing(listingId); // déjà réglée en base
			SendChatSystemNotice(client, "That listing is gone.");
			return;
		}
		if (result != CharacterDbSaveResult::Saved)
		{
			SendChatSystemNotice(client, "Auction house unavailable, try again later.");
			return;
		}
		m_auctionHouse.EraseListing(listingId);
		if (refundOther)
		{
			if (ConnectedClient* prev = FindConnectedClientByCharacterKey(hiCk))
			{
				SendChatSystemNotice(*prev, "Auction ended (buyout). Your bid was refunded.");
			}
		}
		SendChatSystemNotice(client, "Buyout successful.");
		LOG_INFO(Net, "[ServerApp] AuctionBuyout OK (db, client_id={}, listing_id={}, price={})",
			client.clientId, listingId, settlement.finalPrice);
	}

	void ServerApp::ProcessAuctionHouseTickDb()
	{
		for (const AuctionListingRecord& row : m_auctionHouse.ExpiredListings(m_currentTick))
		{
			const CharacterDbSaveResult result = SettleAuctionInDb(AuctionHouseService::SettlementFor(row), 0u, 0u, nullptr, 0u);
			if (result == CharacterDbSaveResult::Saved || result == CharacterDbSaveResult::AlreadyApplied)
			{
				// AlreadyApplied : annonce déjà absente de la base, rien à régler.
				m_auctionHouse.EraseListing(row.listingId);
				continue;
			}
			// Base indisponible : nouvel essai dans une minute.
			if (AuctionListingRecord* live = m_auctionHouse.FindListing(row.listingId))
			{
				live->expiresAtTick = m_currentTick + 60u * static_cast<uint32_t>(m_tickHz);
			}
		}
	}

	void ServerApp::ProcessAuctionHouseTick()
	{
		// ETAPE4.2 — échéances des enchères une fois par seconde (parcours de toutes les
		// annonces), décalé des autres travaux à 1 Hz.
		constexpr uint64_t kAuctionSliceId = 7u;
		if (!tick_slicing::IsSliceTurn(kAuctionSliceId, m_currentTick, m_auctionExpiryPeriodTicks))
		{
			return;
		}
		if (AuctionInDb())
		{
			ProcessAuctionHouseTickDb(); // ETAPE2.8
			return;
		}
		std::vector<AuctionSettlement> settlements;
		m_auctionHouse.CollectExpired(m_currentTick, settlements);
		for (const AuctionSettlement& s : settlements)
		{
			ApplyAuctionSettlement(s);
		}
	}

	void ServerApp::RefundGoldToCharacter(uint64_t characterKey, uint32_t amountGold, std::string_view /*reason*/)
	{
		if (amountGold == 0u)
		{
			return;
		}
		ConnectedClient* online = FindConnectedClientByCharacterKey(characterKey);
		if (online != nullptr)
		{
			std::string err;
			if (!m_playerWallet.AddCurrency(*online, kCurrencyGold, amountGold, err))
			{
				LOG_WARN(Net, "[ServerApp] RefundGold AddCurrency FAILED (ck={}, {}) — mailing", characterKey, err);
				(void)DepositMailboxDelivery(characterKey, amountGold, nullptr);
				return;
			}
			(void)SendWalletUpdate(*online);
			SaveConnectedClient(*online, "auction_refund");
			LOG_INFO(Net, "[ServerApp] RefundGold OK online (ck={}, amount={})", characterKey, amountGold);
			return;
		}
		if (!DepositMailboxDelivery(characterKey, amountGold, nullptr))
		{
			LOG_ERROR(Net, "[ServerApp] Refund gold mailbox FAILED (ck={}, amount={})", characterKey, amountGold);
			return;
		}
		LOG_INFO(Net, "[ServerApp] RefundGold mailed (ck={}, amount={})", characterKey, amountGold);
	}

	bool ServerApp::DepositMailboxDelivery(uint64_t characterKey, uint32_t goldDelta, const ItemStack* itemOptional)
	{
#if defined(SHARD_POSITION_DB)
		// ETAPE2.1 — avec une base : une ligne character_mailbox, sans lire ni
		// réécrire l'état du destinataire (qui peut être en jeu sur un autre shard).
		// Son shard propriétaire la fusionne à la prochaine connexion.
		if (m_characterDbPool != nullptr)
		{
			const bool hasItem = itemOptional != nullptr && itemOptional->itemId != 0u && itemOptional->quantity != 0u;
			if (characterKey == 0u || (goldDelta == 0u && !hasItem))
			{
				return true; // rien à déposer
			}
			auto guard = m_characterDbPool->Acquire();
			MYSQL* mysql = guard.get();
			auto* cache = guard.cache();
			auto* ins = (mysql != nullptr && cache != nullptr)
				? cache->Acquire(mysql,
					"INSERT INTO character_mailbox (character_id, gold, item_id, quantity, source) VALUES (?, ?, ?, ?, 'shard')")
				: nullptr;
			const bool ok = ins != nullptr
				&& ins->Bind(0, characterKey)
				&& ins->Bind(1, goldDelta)
				&& ins->Bind(2, hasItem ? itemOptional->itemId : 0u)
				&& ins->Bind(3, hasItem ? itemOptional->quantity : 0u)
				&& ins->Execute();
			if (!ok)
			{
				LOG_ERROR(Net, "[ServerApp] DepositMailbox DB insert FAILED (character_key={}, gold_delta={}, item={})",
					characterKey, goldDelta, hasItem);
				return false;
			}
			LOG_INFO(Net, "[ServerApp] DepositMailbox OK (db, character_key={}, gold_delta={}, item={})",
				characterKey, goldDelta, hasItem);
			return true;
		}
#endif
		PersistedCharacterState state{};
		if (!m_characterPersistence.LoadCharacter(characterKey, state))
		{
			LOG_WARN(Net, "[ServerApp] DepositMailbox FAILED: cannot load character_key={}", characterKey);
			return false;
		}
		const uint64_t newMailGold = static_cast<uint64_t>(state.mailboxGold) + static_cast<uint64_t>(goldDelta);
		state.mailboxGold = static_cast<uint32_t>(std::min<uint64_t>(
			newMailGold,
			static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
		if (itemOptional != nullptr && itemOptional->itemId != 0u && itemOptional->quantity != 0u)
		{
			constexpr size_t kMaxMailboxItems = 64;
			if (state.mailboxItems.size() >= kMaxMailboxItems)
			{
				LOG_WARN(Net, "[ServerApp] DepositMailbox item dropped: mailbox full (character_key={})", characterKey);
			}
			else
			{
				state.mailboxItems.push_back(*itemOptional);
			}
		}
		if (!m_characterPersistence.SaveCharacter(state))
		{
			LOG_ERROR(Net, "[ServerApp] DepositMailbox Save FAILED (character_key={})", characterKey);
			return false;
		}
		LOG_INFO(Net, "[ServerApp] DepositMailbox OK (character_key={}, gold_delta={}, item={})",
			characterKey,
			goldDelta,
			itemOptional != nullptr);
		return true;
	}

	void ServerApp::ApplyAuctionSettlement(const AuctionSettlement& s)
	{
		{
			std::ostringstream hist;
			hist << "listing=" << s.listingId << " final=" << s.finalPrice << " seller_ck=" << s.sellerCharacterKey
				<< " buyer_ck=" << s.buyerCharacterKey << " no_bids=" << (s.expiredWithoutBids ? 1 : 0);
			if (!m_auctionHouse.AppendHistoryLine(hist.str()))
			{
				LOG_WARN(Net, "[ServerApp] Auction history append failed (listing_id={})", s.listingId);
			}
		}

		if (s.expiredWithoutBids)
		{
			ConnectedClient* seller = FindConnectedClientByCharacterKey(s.sellerCharacterKey);
			if (seller != nullptr)
			{
				AddItemToInventory(*seller, s.item);
				SaveConnectedClient(*seller, "auction_expired_return");
				SendChatSystemNotice(*seller, "Auction expired. Item returned.");
				(void)SendInventoryDelta(
					*seller,
					std::span<const ItemStack>(seller->inventory.data(), seller->inventory.size()));
				LOG_INFO(Net, "[ServerApp] Auction expired: item returned online (seller_ck={})", s.sellerCharacterKey);
			}
			else
			{
				if (!DepositMailboxDelivery(s.sellerCharacterKey, 0, &s.item))
				{
					LOG_ERROR(Net, "[ServerApp] Auction expired: mailbox return FAILED (seller_ck={})", s.sellerCharacterKey);
				}
			}
			return;
		}

		ConnectedClient* buyer = FindConnectedClientByCharacterKey(s.buyerCharacterKey);
		if (buyer != nullptr)
		{
			AddItemToInventory(*buyer, s.item);
			SaveConnectedClient(*buyer, "auction_won_item");
			(void)SendInventoryDelta(
				*buyer,
				std::span<const ItemStack>(buyer->inventory.data(), buyer->inventory.size()));
			SendChatSystemNotice(*buyer, "You won an auction: item received.");
			LOG_INFO(Net, "[ServerApp] Auction won: buyer online (buyer_ck={})", s.buyerCharacterKey);
		}
		else if (!DepositMailboxDelivery(s.buyerCharacterKey, 0, &s.item))
		{
			LOG_ERROR(Net, "[ServerApp] Auction won: item mailbox FAILED (buyer_ck={})", s.buyerCharacterKey);
		}

		if (s.sellerProceeds == 0u)
		{
			return;
		}
		ConnectedClient* seller = FindConnectedClientByCharacterKey(s.sellerCharacterKey);
		if (seller != nullptr)
		{
			std::string werr;
			if (m_playerWallet.AddCurrency(*seller, kCurrencyGold, s.sellerProceeds, werr))
			{
				(void)SendWalletUpdate(*seller);
				SaveConnectedClient(*seller, "auction_seller_paid");
				SendChatSystemNotice(*seller, "Your auction sold.");
				LOG_INFO(Net, "[ServerApp] Auction seller paid online (ck={}, amount={})", s.sellerCharacterKey, s.sellerProceeds);
			}
			else
			{
				LOG_WARN(Net, "[ServerApp] Seller wallet full, mailing proceeds (ck={}, err={})", s.sellerCharacterKey, werr);
				(void)DepositMailboxDelivery(s.sellerCharacterKey, s.sellerProceeds, nullptr);
			}
		}
		else if (!DepositMailboxDelivery(s.sellerCharacterKey, s.sellerProceeds, nullptr))
		{
			LOG_ERROR(Net, "[ServerApp] Auction seller mailbox FAILED (ck={})", s.sellerCharacterKey);
		}
	}

	bool ServerApp::RemoveStackFromInventory(ConnectedClient& client, uint32_t itemId, uint32_t quantity, std::string& outError)
	{
		if (quantity == 0u)
		{
			outError = "zero_quantity";
			LOG_WARN(Net, "[ServerApp] RemoveStackFromInventory FAILED: zero quantity (client_id={})", client.clientId);
			return false;
		}

		uint32_t remaining = quantity;
		for (size_t i = 0; i < client.inventory.size() && remaining > 0;)
		{
			ItemStack& stack = client.inventory[i];
			if (stack.itemId != itemId)
			{
				++i;
				continue;
			}

			if (stack.quantity <= remaining)
			{
				remaining -= stack.quantity;
				client.inventory.erase(client.inventory.begin() + static_cast<std::ptrdiff_t>(i));
				continue;
			}

			stack.quantity -= remaining;
			remaining = 0;
			break;
		}

		if (remaining != 0u)
		{
			outError = "not_enough_items";
			LOG_WARN(Net,
				"[ServerApp] RemoveStackFromInventory FAILED: insufficient (client_id={}, item_id={}, short_by={})",
				client.clientId,
				itemId,
				remaining);
			return false;
		}

		LOG_INFO(Net,
			"[ServerApp] RemoveStackFromInventory OK (client_id={}, item_id={}, quantity={})",
			client.clientId,
			itemId,
			quantity);
		return true;
	}

	// -------------------------------------------------------------------------
	// M35.3 — Direct player-to-player trade handlers
	// -------------------------------------------------------------------------

	bool ServerApp::HandleTradeCommand(ConnectedClient& sender, std::string_view argsRemainder)
	{
		const std::string_view targetName = argsRemainder;
		if (targetName.empty())
		{
			SendChatSystemNotice(sender, "Usage: /trade <player_name>");
			return true;
		}

		ConnectedClient* target = FindConnectedClientByChatDisplayName(targetName);
		if (target == nullptr)
		{
			SendChatSystemNotice(sender, "Player not found or not in range.");
			LOG_WARN(Net, "[TradeSystem] /trade target not found (sender={}, target='{}')",
			         sender.clientId, targetName);
			return true;
		}

		if (target->clientId == sender.clientId)
		{
			SendChatSystemNotice(sender, "You cannot trade with yourself.");
			return true;
		}

		std::string error;
		const TradeOpResult result = m_tradeSystem.SendTradeRequest(sender, *target, error);
		if (result != TradeOpResult::Ok)
		{
			SendChatSystemNotice(sender, "Trade request failed: " + error);
			return true;
		}

		/// Notify target via TradeRequestNotify packet.
		const TradeRequestNotifyMessage notify{ "P" + std::to_string(sender.clientId) };
		const auto packet = EncodeTradeRequestNotify(notify);
		SendDatagram(target->endpoint, std::span<const std::byte>(packet.data(), packet.size()));

		SendChatSystemNotice(sender, "Trade request sent to " + std::string(targetName) + ".");
		LOG_INFO(Net, "[TradeSystem] /trade request sent (sender={}, target={})",
		         sender.clientId, target->clientId);
		return true;
	}

	void ServerApp::HandleTradeAccept(const Endpoint& endpoint, [[maybe_unused]] const TradeAcceptMessage& message)
	{
		ConnectedClient* acceptor = FindClient(endpoint);
		if (acceptor == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeAccept: unknown endpoint");
			return;
		}

		std::string error;
		const TradeOpResult result = m_tradeSystem.AcceptTradeRequest(*acceptor, error);
		if (result != TradeOpResult::Ok)
		{
			SendChatSystemNotice(*acceptor, "Trade accept failed: " + error);
			LOG_WARN(Net, "[TradeSystem] TradeAccept failed (client={}, reason={})",
			         acceptor->clientId, error);
			return;
		}

		const uint32_t partnerClientId = m_tradeSystem.GetPartnerClientId(acceptor->clientId);
		BroadcastTradeWindowUpdate(acceptor->clientId, partnerClientId);
		LOG_INFO(Net, "[TradeSystem] TradeAccept OK (client={}, partner={})",
		         acceptor->clientId, partnerClientId);
	}

	void ServerApp::HandleTradeDecline(const Endpoint& endpoint, [[maybe_unused]] const TradeDeclineMessage& message)
	{
		ConnectedClient* decliner = FindClient(endpoint);
		if (decliner == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeDecline: unknown endpoint");
			return;
		}

		/// Find initiator before declining so we can notify them.
		const PendingTradeRequest* req = m_tradeSystem.FindPendingRequest(decliner->clientId);
		uint32_t initiatorClientId = req ? req->initiatorClientId : 0;

		std::string error;
		m_tradeSystem.DeclineTradeRequest(*decliner, error);

		SendChatSystemNotice(*decliner, "Trade request declined.");
		if (initiatorClientId != 0)
		{
			ConnectedClient* initiator = FindConnectedClient(initiatorClientId);
			if (initiator != nullptr)
			{
				SendChatSystemNotice(*initiator,
					"P" + std::to_string(decliner->clientId) + " declined your trade request.");
			}
		}
		LOG_INFO(Net, "[TradeSystem] TradeDecline OK (client={})", decliner->clientId);
	}

	void ServerApp::HandleTradeAddItem(const Endpoint& endpoint, const TradeAddItemMessage& message)
	{
		ConnectedClient* caller = FindClient(endpoint);
		if (caller == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeAddItem: unknown endpoint");
			return;
		}

		std::string error;
		const TradeOpResult result = m_tradeSystem.AddItem(*caller, message.itemId, message.quantity, error);
		if (result != TradeOpResult::Ok)
		{
			SendChatSystemNotice(*caller, "Cannot add item: " + error);
			LOG_WARN(Net, "[TradeSystem] AddItem failed (client={}, item={}, reason={})",
			         caller->clientId, message.itemId, error);
			return;
		}

		const uint32_t partnerClientId = m_tradeSystem.GetPartnerClientId(caller->clientId);
		BroadcastTradeWindowUpdate(caller->clientId, partnerClientId);
	}

	void ServerApp::HandleTradeSetGold(const Endpoint& endpoint, const TradeSetGoldMessage& message)
	{
		ConnectedClient* caller = FindClient(endpoint);
		if (caller == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeSetGold: unknown endpoint");
			return;
		}

		std::string error;
		const TradeOpResult result = m_tradeSystem.SetGold(*caller, message.goldAmount, error);
		if (result != TradeOpResult::Ok)
		{
			SendChatSystemNotice(*caller, "Cannot set gold: " + error);
			LOG_WARN(Net, "[TradeSystem] SetGold failed (client={}, reason={})",
			         caller->clientId, error);
			return;
		}

		const uint32_t partnerClientId = m_tradeSystem.GetPartnerClientId(caller->clientId);
		BroadcastTradeWindowUpdate(caller->clientId, partnerClientId);
	}

	void ServerApp::HandleTradeLock(const Endpoint& endpoint, [[maybe_unused]] const TradeLockMessage& message)
	{
		ConnectedClient* caller = FindClient(endpoint);
		if (caller == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeLock: unknown endpoint");
			return;
		}

		std::string error;
		const TradeOpResult result = m_tradeSystem.Lock(*caller, m_currentTick, error);
		if (result != TradeOpResult::Ok)
		{
			SendChatSystemNotice(*caller, "Cannot lock trade: " + error);
			LOG_WARN(Net, "[TradeSystem] Lock failed (client={}, reason={})",
			         caller->clientId, error);
			return;
		}

		const uint32_t partnerClientId = m_tradeSystem.GetPartnerClientId(caller->clientId);
		BroadcastTradeWindowUpdate(caller->clientId, partnerClientId);
	}

	void ServerApp::HandleTradeConfirm(const Endpoint& endpoint, [[maybe_unused]] const TradeConfirmMessage& message)
	{
		ConnectedClient* caller = FindClient(endpoint);
		if (caller == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeConfirm: unknown endpoint");
			return;
		}

		const uint32_t partnerClientId = m_tradeSystem.GetPartnerClientId(caller->clientId);
		ConnectedClient* partner = FindConnectedClient(partnerClientId);
		if (partner == nullptr)
		{
			/// Partner disconnected — cancel the trade.
			std::string cancelError;
			m_tradeSystem.Cancel(caller->clientId, cancelError);
			SendChatSystemNotice(*caller, "Trade cancelled: partner disconnected.");
			LOG_WARN(Net, "[TradeSystem] TradeConfirm: partner not found (caller={}, partner_id={})",
			         caller->clientId, partnerClientId);
			return;
		}

		/// Build audit strings before swap (items may change after ApplySwap).
		const TradeSession* sessionSnap = m_tradeSystem.FindSession(caller->clientId);
		std::string itemsA, itemsB;
		uint32_t goldA = 0, goldB = 0;
		if (sessionSnap != nullptr)
		{
			for (const auto& s : sessionSnap->sideA.items)
				itemsA += std::to_string(s.itemId) + "x" + std::to_string(s.quantity) + " ";
			for (const auto& s : sessionSnap->sideB.items)
				itemsB += std::to_string(s.itemId) + "x" + std::to_string(s.quantity) + " ";
			goldA = sessionSnap->sideA.goldAmount;
			goldB = sessionSnap->sideB.goldAmount;
		}

		std::string error;
		const TradeOpResult result = m_tradeSystem.Confirm(*caller, *partner, error);
		if (result == TradeOpResult::Ok)
		{
			/// Check whether session was closed (both confirmed & swap applied).
			if (m_tradeSystem.FindSession(caller->clientId) == nullptr)
			{
				// ETAPE2.4 — les deux personnages échangés sont écrits ensemble, dans une
				// transaction (avant : aucune sauvegarde, on attendait l'autosave ; un
				// arrêt entre les deux sauvegardes pouvait dupliquer ou perdre des objets).
				SaveCharacterPair(*caller, *partner, "trade");
				LogTradeAudit(caller->clientId, partnerClientId, itemsA, itemsB, goldA, goldB);
				/// Send WalletUpdate to both so gold counters refresh.
				SendWalletUpdate(*caller);
				SendWalletUpdate(*partner);
				BroadcastTradeComplete(caller->clientId, partnerClientId);
				LOG_INFO(Net, "[TradeSystem] TradeConfirm: swap completed (A={} B={})",
				         caller->clientId, partnerClientId);
			}
			else
			{
				/// Waiting for partner — send updated window.
				BroadcastTradeWindowUpdate(caller->clientId, partnerClientId);
			}
		}
		else
		{
			/// Validation failed — cancel trade and return items (no swap occurred).
			BroadcastTradeCancelled(caller->clientId, partnerClientId, error);
			LOG_WARN(Net, "[TradeSystem] TradeConfirm: validation failed (caller={}, reason={})",
			         caller->clientId, error);
		}
	}

	void ServerApp::HandleTradeCancelPacket(const Endpoint& endpoint, [[maybe_unused]] const TradeCancelMessage& message)
	{
		ConnectedClient* caller = FindClient(endpoint);
		if (caller == nullptr)
		{
			LOG_WARN(Net, "[TradeSystem] TradeCancel: unknown endpoint");
			return;
		}

		const uint32_t partnerClientId = m_tradeSystem.GetPartnerClientId(caller->clientId);
		std::string error;
		m_tradeSystem.Cancel(caller->clientId, error);
		BroadcastTradeCancelled(caller->clientId, partnerClientId, "Trade cancelled by player.");
		LOG_INFO(Net, "[TradeSystem] TradeCancel by client={}", caller->clientId);
	}

	void ServerApp::BroadcastTradeWindowUpdate(uint32_t clientIdA, uint32_t clientIdB)
	{
		ConnectedClient* clientA = FindConnectedClient(clientIdA);
		ConnectedClient* clientB = FindConnectedClient(clientIdB);

		if (clientA != nullptr)
		{
			const TradeWindowUpdateMessage msg = m_tradeSystem.BuildWindowUpdate(clientIdA, m_currentTick);
			const auto packet = EncodeTradeWindowUpdate(msg);
			SendDatagram(clientA->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
		}
		if (clientB != nullptr)
		{
			const TradeWindowUpdateMessage msg = m_tradeSystem.BuildWindowUpdate(clientIdB, m_currentTick);
			const auto packet = EncodeTradeWindowUpdate(msg);
			SendDatagram(clientB->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
		}
	}

	void ServerApp::BroadcastTradeComplete(uint32_t clientIdA, uint32_t clientIdB)
	{
		auto sendComplete = [&](uint32_t clientId)
		{
			ConnectedClient* client = FindConnectedClient(clientId);
			if (client == nullptr)
			{
				return;
			}
			const TradeCompleteMessage msg{ clientId };
			const auto packet = EncodeTradeComplete(msg);
			SendDatagram(client->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
			SendChatSystemNotice(*client, "Trade completed successfully.");
		};
		sendComplete(clientIdA);
		sendComplete(clientIdB);
		LOG_INFO(Net, "[TradeSystem] BroadcastTradeComplete: A={} B={}", clientIdA, clientIdB);
	}

	void ServerApp::BroadcastTradeCancelled(uint32_t clientIdA, uint32_t clientIdB, std::string_view reason)
	{
		const TradeCancelledMessage msg{ std::string(reason) };
		const auto packet = EncodeTradeCancelled(msg);

		auto sendCancelled = [&](uint32_t clientId)
		{
			ConnectedClient* client = FindConnectedClient(clientId);
			if (client == nullptr)
			{
				return;
			}
			SendDatagram(client->endpoint, std::span<const std::byte>(packet.data(), packet.size()));
			SendChatSystemNotice(*client, "Trade cancelled: " + std::string(reason));
		};
		sendCancelled(clientIdA);
		if (clientIdB != 0 && clientIdB != clientIdA)
		{
			sendCancelled(clientIdB);
		}
		LOG_INFO(Net, "[TradeSystem] BroadcastTradeCancelled: A={} B={} reason={}",
		         clientIdA, clientIdB, reason);
	}

	void ServerApp::LogTradeAudit(
		uint32_t clientIdA,
		uint32_t clientIdB,
		const std::string& itemsA,
		const std::string& itemsB,
		uint32_t goldA,
		uint32_t goldB)
	{
		LOG_INFO(Gameplay,
		         "[TradeAudit] TRADE A={} B={} | A_gave_gold={} A_gave_items=[{}] | "
		         "B_gave_gold={} B_gave_items=[{}]",
		         clientIdA, clientIdB, goldA, itemsA, goldB, itemsB);
	}
}
