// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Implementation AuctionHandler.

#include "src/masterd/handlers/auction/AuctionHandler.h"

#include "src/masterd/auction/MysqlAuctionStore.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/AuctionPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Constante : nombre maximum d'entrees dans la table de noms d'items V1.
		constexpr uint32_t kItemNameTableMax = 10u;

		/// Table d'items V1 (10 entries hardcode). Adresses stables pour const char*.
		const char* const kItemNames[kItemNameTableMax + 1u] = {
			"Item #0",              // 0 (placeholder, jamais retourne pour 0)
			"Minerai de fer",      // 1
			"Toile de lin",        // 2
			"Tissu mage",          // 3
			"Potion de soin",      // 4
			"Potion de mana",      // 5
			"Lingot de mithril",   // 6
			"Minerai de thorium",  // 7
			"Lotus noir",          // 8
			"Lingot d'arcanite",   // 9
			"Eclat d'ame"          // 10
		};

		/// Format un fallback "Item #<id>" pour les ids hors table.
		std::string FormatUnknownItemName(uint32_t id)
		{
			char buf[32]{};
			// std::snprintf est ASCII-safe sur MSVC.
			std::snprintf(buf, sizeof(buf), "Item #%u", static_cast<unsigned>(id));
			return std::string(buf);
		}

		/// Format "Account#<id>" pour les owners V1 postes par le client.
		std::string FormatAccountOwnerName(uint64_t accountId)
		{
			char buf[40]{};
			std::snprintf(buf, sizeof(buf), "Account#%llu",
				static_cast<unsigned long long>(accountId));
			return std::string(buf);
		}

		/// Wave 5 helper : convertit un steady_clock::time_point en epoch ms
		/// wallclock pour la persistance DB. Calcul du delta steady->now et
		/// addition a system_clock::now() pour eviter la derive d'horloges.
		///
		/// \param tp instant cible en steady_clock (typiquement expiresAt).
		/// \return epoch ms (system_clock).
		uint64_t SteadyToUnixMs(std::chrono::steady_clock::time_point tp)
		{
			const auto steadyNow = std::chrono::steady_clock::now();
			const auto sysNow    = std::chrono::system_clock::now();
			const auto delta     = tp - steadyNow;
			const auto wallclock = sysNow + delta;
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				wallclock.time_since_epoch()).count();
			return static_cast<uint64_t>(ms < 0 ? 0 : ms);
		}

		/// Wave 5 helper : convertit un epoch ms wallclock en steady_clock::time_point.
		/// Si le timestamp est deja passe, retourne now() (l'auction sera marquee
		/// expired au prochain scan).
		///
		/// \param unixMs epoch ms cible (wallclock).
		/// \return steady_clock::time_point equivalent au mieux possible.
		std::chrono::steady_clock::time_point UnixMsToSteady(uint64_t unixMs)
		{
			const auto sysNow    = std::chrono::system_clock::now();
			const auto steadyNow = std::chrono::steady_clock::now();
			const auto sysTarget = std::chrono::system_clock::time_point(
				std::chrono::milliseconds(unixMs));
			const auto delta = sysTarget - sysNow;
			return steadyNow + delta;
		}
	}

	// -------------------------------------------------------------------------
	// ResolveItemName — static helper public.
	// -------------------------------------------------------------------------

	std::string AuctionHandler::ResolveItemName(uint32_t itemTemplateId)
	{
		if (itemTemplateId >= 1u && itemTemplateId <= kItemNameTableMax)
		{
			return std::string(kItemNames[itemTemplateId]);
		}
		return FormatUnknownItemName(itemTemplateId);
	}

	// -------------------------------------------------------------------------
	// SeedV1Auctions — register the 8 hardcoded listings at boot.
	// -------------------------------------------------------------------------

	void AuctionHandler::SeedV1Auctions()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_seeded)
		{
			LOG_DEBUG(Net, "[AuctionHandler] SeedV1Auctions already seeded (idempotent skip)");
			return;
		}

		// Wave 5 : si store DB branche, prefere LoadAllActive. Si la requete
		// retourne au moins une ligne, on l'utilise comme verite. Sinon
		// (DB vide ou indisponible), fallback sur le seed hardcode.
		if (m_store && m_store->IsAvailable())
		{
			auto rows = m_store->LoadAllActive();
			if (!rows.empty())
			{
				uint64_t maxId = 0u;
				for (const auto& r : rows)
				{
					InMemoryAuction a;
					a.auctionId              = r.auctionId;
					a.itemTemplateId         = r.itemTemplateId;
					a.itemName               = r.itemName;
					a.count                  = r.count;
					a.startBidCopper         = r.startBidCopper;
					a.currentBidCopper       = r.currentBidCopper;
					a.buyoutCopper           = r.buyoutCopper;
					a.ownerName              = r.ownerName;
					a.ownerAccountId         = r.ownerAccountId;
					a.highestBidderName      = r.highestBidderName;
					a.highestBidderAccountId = r.highestBidderAccountId;
					a.expiresAt              = UnixMsToSteady(r.expiresAtUnixMs);
					a.ended                  = r.ended;
					a.wonByBuyout            = r.wonByBuyout;
					if (a.auctionId > maxId) maxId = a.auctionId;
					m_auctions.push_back(std::move(a));
				}
				m_nextAuctionId.store(maxId + 1u, std::memory_order_relaxed);
				m_seeded = true;
				LOG_INFO(Net, "[AuctionHandler] V1 auctions loaded from DB : {} active listings (nextId={})",
					m_auctions.size(), maxId + 1u);
				return;
			}
			LOG_INFO(Net, "[AuctionHandler] DB store available but no active listings ; falling back to hardcoded seed");
		}

		const auto now = std::chrono::steady_clock::now();
		const auto hr  = std::chrono::hours(1);

		// Helper local : alloue l'id, init owner V1 par chaine fournie (les
		// owners seedees sont fictifs, sans accountId reel = 0).
		auto add = [&](uint32_t itemTemplateId, uint32_t count,
			uint64_t startBid, uint64_t buyout, uint64_t expiresInHours,
			const char* ownerName)
		{
			InMemoryAuction a;
			a.auctionId              = m_nextAuctionId.fetch_add(1u, std::memory_order_relaxed);
			a.itemTemplateId         = itemTemplateId;
			a.itemName               = ResolveItemName(itemTemplateId);
			a.count                  = count;
			a.startBidCopper         = startBid;
			a.currentBidCopper       = startBid;
			a.buyoutCopper           = buyout;
			a.ownerName              = ownerName;
			a.ownerAccountId         = 0u; // V1 : owners seed fictifs.
			a.highestBidderName      = "";
			a.highestBidderAccountId = 0u;
			a.expiresAt              = now + (hr * static_cast<long long>(expiresInHours));
			a.ended                  = false;
			a.wonByBuyout            = false;
			m_auctions.push_back(std::move(a));
		};

		// 8 auctions seedees (mix items / owners / durations / buyouts).
		add(1u,  20u, 500ull,    1000ull,   1ull,  "Garond");  // Minerai de fer
		add(2u,  50u, 300ull,    0ull,      6ull,  "Mirelle"); // Toile de lin, no buyout
		add(3u,  10u, 1500ull,   3000ull,   12ull, "Tobrek");  // Tissu mage
		add(4u,  5u,  250ull,    500ull,    24ull, "Garond");  // Potion de soin
		add(5u,  5u,  250ull,    0ull,      24ull, "Sylvane"); // Potion de mana, no buyout
		add(6u,  3u,  10000ull,  25000ull,  36ull, "Sylvane"); // Lingot de mithril
		add(7u,  15u, 8000ull,   0ull,      48ull, "Tobrek");  // Minerai de thorium, no buyout
		add(8u,  1u,  50000ull,  150000ull, 48ull, "Mirelle"); // Lotus noir

		m_seeded = true;
		LOG_INFO(Net, "[AuctionHandler] V1 auctions seeded : 8 listings ({} -> {})",
			m_auctions.front().auctionId, m_auctions.back().auctionId);
	}

	// -------------------------------------------------------------------------
	// HandlePacket — dispatch + session validation
	// -------------------------------------------------------------------------

	void AuctionHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[AuctionHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account.
		uint64_t accountId = 0;
		bool sessionOk = false;
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (connSessionId && *connSessionId != 0u
			&& sessionIdHeader != 0u && *connSessionId == sessionIdHeader)
		{
			auto acc = m_sessionMgr->GetAccountId(*connSessionId);
			if (acc && *acc != 0u)
			{
				accountId = *acc;
				sessionOk = true;
			}
		}

		if (!sessionOk)
		{
			std::vector<uint8_t> pkt;
			const uint8_t kUnauth = static_cast<uint8_t>(AuctionErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeAuctionListRequest:
				pkt = BuildAuctionListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeAuctionPostRequest:
				pkt = BuildAuctionPostResponsePacket(kUnauth, 0ull, requestId, sessionIdHeader);
				break;
			case kOpcodeAuctionBidRequest:
				pkt = BuildAuctionBidResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeAuctionCancelRequest:
				pkt = BuildAuctionCancelResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			default:
				return;
			}
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		switch (opcode)
		{
		case kOpcodeAuctionListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeAuctionPostRequest:
			HandlePost(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeAuctionBidRequest:
			HandleBid(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeAuctionCancelRequest:
			HandleCancel(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// ScanExpiredLocked — collect newly expired auctions, mutex held.
	// -------------------------------------------------------------------------

	void AuctionHandler::ScanExpiredLocked(std::chrono::steady_clock::time_point now,
		std::vector<ExpiredPushItem>& expiredOwners)
	{
		for (auto& a : m_auctions)
		{
			if (a.ended) continue;
			if (a.expiresAt <= now)
			{
				a.ended = true;
				ExpiredPushItem item;
				item.ownerAccountId = a.ownerAccountId;
				item.auctionId      = a.auctionId;
				const bool wonByBid = (a.highestBidderAccountId != 0u);
				item.won            = wonByBid ? 1u : 0u;
				item.finalBidCopper = wonByBid ? a.currentBidCopper : 0ull;
				item.winnerName     = wonByBid ? a.highestBidderName : std::string();
				expiredOwners.push_back(std::move(item));
			}
		}
	}

	// -------------------------------------------------------------------------
	// HandleList
	// -------------------------------------------------------------------------

	void AuctionHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseAuctionListRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[AuctionHandler] List parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildAuctionListResponsePacket(0u, {}, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint32_t filter = parsed->itemTemplateIdFilter;
		const auto now = std::chrono::steady_clock::now();

		std::vector<AuctionListingSummary> summaries;
		std::vector<ExpiredPushItem> expiredOwners;
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			// 1) Marque les auctions expirees + collecte les owners a notifier.
			ScanExpiredLocked(now, expiredOwners);

			// 2) Construit la liste des actives (non ended) avec filter.
			summaries.reserve(m_auctions.size());
			for (const auto& a : m_auctions)
			{
				if (a.ended) continue;
				if (filter != 0u && a.itemTemplateId != filter) continue;
				const auto remaining = a.expiresAt - now;
				const auto remainingSec = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
				AuctionListingSummary s;
				s.auctionId              = a.auctionId;
				s.itemTemplateId         = a.itemTemplateId;
				s.itemName               = a.itemName;
				s.count                  = a.count;
				s.currentBidCopper       = a.currentBidCopper;
				s.buyoutCopper           = a.buyoutCopper;
				s.ownerName              = a.ownerName;
				s.secondsUntilExpiration = (remainingSec > 0) ? static_cast<uint64_t>(remainingSec) : 0ull;
				summaries.push_back(std::move(s));
			}
		}

		// Wave 5 : persiste l'etat ended pour chaque auction nouvellement
		// expiree (sans buyout). Hors mutex, best-effort.
		if (m_store && m_store->IsAvailable())
		{
			for (const auto& item : expiredOwners)
			{
				// won=1 ici signifie qu'il y avait un bidder ; wonByBuyout
				// reste false (le buyout est marque dans HandleBid, pas ici).
				(void)m_store->MarkEnded(item.auctionId, /*wonByBuyout=*/false);
			}
		}

		// Push notifications hors mutex (ne devrait pas reentrer dans le handler).
		for (const auto& item : expiredOwners)
		{
			if (item.ownerAccountId == 0u) continue;
			const uint32_t ownerConn = FindConnIdForAccount(item.ownerAccountId);
			if (ownerConn == 0u) continue;
			(void)PushAuctionExpired(ownerConn, item.auctionId, item.won,
				item.finalBidCopper, item.winnerName);
		}

		LOG_INFO(Net, "[AuctionHandler] List account={} filter={} count={} expired={}",
			accountId, filter, summaries.size(), expiredOwners.size());

		auto pkt = BuildAuctionListResponsePacket(0u, summaries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandlePost
	// -------------------------------------------------------------------------

	void AuctionHandler::HandlePost(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseAuctionPostRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[AuctionHandler] Post parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildAuctionPostResponsePacket(
				static_cast<uint8_t>(AuctionErrorCode::InvalidParams), 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation params.
		const uint32_t itemTemplateId = parsed->itemTemplateId;
		const uint32_t count          = parsed->count;
		const uint64_t startBid       = parsed->startBidCopper;
		const uint64_t buyout         = parsed->buyoutCopper;
		const uint8_t  durationHours  = parsed->durationHours;

		const bool durationOk = (durationHours == 12u || durationHours == 24u || durationHours == 48u);
		const bool buyoutOk   = (buyout == 0ull) || (buyout >= startBid);
		const bool valid      = (count > 0u) && (startBid > 0ull) && buyoutOk && durationOk;

		if (!valid)
		{
			LOG_INFO(Net, "[AuctionHandler] Post InvalidParams account={} item={} count={} startBid={} buyout={} durationHours={}",
				accountId, itemTemplateId, count, startBid, buyout,
				static_cast<unsigned>(durationHours));
			auto pkt = BuildAuctionPostResponsePacket(
				static_cast<uint8_t>(AuctionErrorCode::InvalidParams), 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Wave 5 : si store DB branche, on tente d'inserer pour recuperer un
		// id AUTO_INCREMENT. Sinon (no-DB ou echec), fallback sur le compteur
		// atomic local.
		uint64_t newId = 0u;
		const auto now = std::chrono::steady_clock::now();
		const auto expiresAt = now + std::chrono::hours(static_cast<long long>(durationHours));

		if (m_store && m_store->IsAvailable())
		{
			engine::server::auctions::AuctionRow row;
			row.itemTemplateId   = itemTemplateId;
			row.itemName         = ResolveItemName(itemTemplateId);
			row.count            = count;
			row.startBidCopper   = startBid;
			row.currentBidCopper = startBid;
			row.buyoutCopper     = buyout;
			row.ownerName        = FormatAccountOwnerName(accountId);
			row.ownerAccountId   = accountId;
			row.expiresAtUnixMs  = SteadyToUnixMs(expiresAt);
			newId = m_store->Insert(row);
		}
		if (newId == 0u)
		{
			// Fallback : id local. Cela peut creer un mismatch avec la DB en
			// cas de redemarrage simultane multi-master ; acceptable V1 (un
			// seul master).
			newId = m_nextAuctionId.fetch_add(1u, std::memory_order_relaxed);
		}
		else
		{
			// On garde m_nextAuctionId au-dessus du max DB pour eviter une
			// collision si on bascule vers no-DB plus tard.
			uint64_t expected = m_nextAuctionId.load(std::memory_order_relaxed);
			while (expected <= newId
				&& !m_nextAuctionId.compare_exchange_weak(expected, newId + 1u,
					std::memory_order_relaxed))
			{
				// retry
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			InMemoryAuction a;
			a.auctionId              = newId;
			a.itemTemplateId         = itemTemplateId;
			a.itemName               = ResolveItemName(itemTemplateId);
			a.count                  = count;
			a.startBidCopper         = startBid;
			a.currentBidCopper       = startBid;
			a.buyoutCopper           = buyout;
			a.ownerName              = FormatAccountOwnerName(accountId);
			a.ownerAccountId         = accountId;
			a.highestBidderName      = "";
			a.highestBidderAccountId = 0u;
			a.expiresAt              = expiresAt;
			a.ended                  = false;
			a.wonByBuyout            = false;
			m_auctions.push_back(std::move(a));
		}

		LOG_INFO(Net, "[AuctionHandler] Post Ok account={} item={} count={} auctionId={} durationHours={}",
			accountId, itemTemplateId, count, newId,
			static_cast<unsigned>(durationHours));

		auto pkt = BuildAuctionPostResponsePacket(0u, newId, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleBid
	// -------------------------------------------------------------------------

	void AuctionHandler::HandleBid(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseAuctionBidRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[AuctionHandler] Bid parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildAuctionBidResponsePacket(
				static_cast<uint8_t>(AuctionErrorCode::AuctionNotFound), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t auctionId = parsed->auctionId;
		const uint64_t bidAmount = parsed->bidAmountCopper;

		uint8_t  errorCode    = 0u;
		uint8_t  isBuyout     = 0u;
		uint64_t sellerAccountId = 0u;
		uint64_t buyoutAuctionId = 0ull;
		uint64_t buyoutFinalBid  = 0ull;
		std::string buyoutWinnerName;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			InMemoryAuction* found = nullptr;
			for (auto& a : m_auctions)
			{
				if (a.auctionId == auctionId)
				{
					found = &a;
					break;
				}
			}
			if (!found)
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::AuctionNotFound);
			}
			else if (found->ended || found->expiresAt <= std::chrono::steady_clock::now())
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::AuctionExpired);
			}
			else if (found->ownerAccountId != 0u && found->ownerAccountId == accountId)
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::OwnAuction);
			}
			else if (bidAmount <= found->currentBidCopper)
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::BidTooLow);
			}
			else
			{
				// Bid acceptee. Verifie buyout.
				const std::string bidderName = FormatAccountOwnerName(accountId);
				if (found->buyoutCopper > 0ull && bidAmount >= found->buyoutCopper)
				{
					// Buyout immediat.
					found->currentBidCopper       = bidAmount;
					found->highestBidderName      = bidderName;
					found->highestBidderAccountId = accountId;
					found->ended                  = true;
					found->wonByBuyout            = true;
					isBuyout = 1u;
					sellerAccountId  = found->ownerAccountId;
					buyoutAuctionId  = found->auctionId;
					buyoutFinalBid   = bidAmount;
					buyoutWinnerName = bidderName;
				}
				else
				{
					// Bid normale.
					found->currentBidCopper       = bidAmount;
					found->highestBidderName      = bidderName;
					found->highestBidderAccountId = accountId;
				}
			}
		}

		if (errorCode != 0u)
		{
			LOG_INFO(Net, "[AuctionHandler] Bid error account={} auctionId={} bid={} error={}",
				accountId, auctionId, bidAmount, static_cast<unsigned>(errorCode));
			auto pkt = BuildAuctionBidResponsePacket(errorCode, 0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Wave 5 : best-effort persistance de la bid (et du end si buyout).
		// Effectue hors mutex car les operations DB peuvent etre lentes.
		if (m_store && m_store->IsAvailable())
		{
			const std::string bidderName = FormatAccountOwnerName(accountId);
			(void)m_store->UpdateBid(auctionId, bidAmount, bidderName, accountId);
			if (isBuyout == 1u)
				(void)m_store->MarkEnded(auctionId, /*wonByBuyout=*/true);
		}

		LOG_INFO(Net, "[AuctionHandler] Bid Ok account={} auctionId={} bid={} buyout={}",
			accountId, auctionId, bidAmount, static_cast<unsigned>(isBuyout));

		auto pkt = BuildAuctionBidResponsePacket(0u, isBuyout, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		// Push expired notification au seller en cas de buyout immediat.
		if (isBuyout == 1u && sellerAccountId != 0u)
		{
			const uint32_t sellerConn = FindConnIdForAccount(sellerAccountId);
			if (sellerConn != 0u)
			{
				(void)PushAuctionExpired(sellerConn, buyoutAuctionId, 1u,
					buyoutFinalBid, buyoutWinnerName);
			}
			else
			{
				LOG_DEBUG(Net, "[AuctionHandler] Bid buyout : seller account={} not connected (skip push)",
					sellerAccountId);
			}
		}
	}

	// -------------------------------------------------------------------------
	// HandleCancel
	// -------------------------------------------------------------------------

	void AuctionHandler::HandleCancel(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseAuctionCancelRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[AuctionHandler] Cancel parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildAuctionCancelResponsePacket(
				static_cast<uint8_t>(AuctionErrorCode::AuctionNotFound),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t auctionId = parsed->auctionId;

		uint8_t errorCode = 0u;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			InMemoryAuction* found = nullptr;
			for (auto& a : m_auctions)
			{
				if (a.auctionId == auctionId)
				{
					found = &a;
					break;
				}
			}
			if (!found)
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::AuctionNotFound);
			}
			else if (found->ended)
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::AuctionExpired);
			}
			else if (found->ownerAccountId == 0u || found->ownerAccountId != accountId)
			{
				errorCode = static_cast<uint8_t>(AuctionErrorCode::NotOwner);
			}
			else
			{
				found->ended = true;
			}
		}

		if (errorCode != 0u)
		{
			LOG_INFO(Net, "[AuctionHandler] Cancel error account={} auctionId={} error={}",
				accountId, auctionId, static_cast<unsigned>(errorCode));
			auto pkt = BuildAuctionCancelResponsePacket(errorCode, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Wave 5 : best-effort persistance du cancel.
		if (m_store && m_store->IsAvailable())
			(void)m_store->MarkEnded(auctionId, /*wonByBuyout=*/false);

		LOG_INFO(Net, "[AuctionHandler] Cancel Ok account={} auctionId={}",
			accountId, auctionId);

		auto pkt = BuildAuctionCancelResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// PushAuctionExpired
	// -------------------------------------------------------------------------

	bool AuctionHandler::PushAuctionExpired(uint32_t connId, uint64_t auctionId, uint8_t won,
		uint64_t finalBidCopper, const std::string& winnerName)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[AuctionHandler] PushAuctionExpired dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[AuctionHandler] PushAuctionExpired: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildAuctionExpiredNotificationPacket(auctionId, won, finalBidCopper,
			winnerName, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[AuctionHandler] PushAuctionExpired: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[AuctionHandler] PushAuctionExpired connId={} auctionId={} won={} finalBid={}",
			connId, auctionId, static_cast<unsigned>(won), finalBidCopper);
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	uint64_t AuctionHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}

	uint32_t AuctionHandler::FindConnIdForAccount(uint64_t accountId) const
	{
		if (!m_connMap || !m_sessionMgr) return 0u;
		const auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessionId] : snapshot)
		{
			auto acc = m_sessionMgr->GetAccountId(sessionId);
			if (acc && *acc == accountId)
				return connId;
		}
		return 0u;
	}
}
