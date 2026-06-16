// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - Implementation LootHandler.

#include "src/masterd/handlers/loot/LootHandler.h"

#include "src/masterd/loot/MysqlLootStore.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/LootPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdio>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Items hardcodes V1 (id 1..5). Adresses stables pour le wire.
		struct ItemEntry { uint32_t id; const char* name; };
		const ItemEntry kV1Items[] = {
			{1u, "Minerai de fer"},
			{2u, "Toile de lin"},
			{3u, "Tissu mage"},
			{4u, "Potion de soin"},
			{5u, "Potion de mana"},
		};
		constexpr size_t kV1ItemCount = sizeof(kV1Items) / sizeof(kV1Items[0]);
	}

	// -------------------------------------------------------------------------
	// ResolveItemName - static helper public.
	// -------------------------------------------------------------------------

	std::string LootHandler::ResolveItemName(uint32_t itemTemplateId)
	{
		for (size_t i = 0; i < kV1ItemCount; ++i)
		{
			if (kV1Items[i].id == itemTemplateId)
				return std::string(kV1Items[i].name);
		}
		// Fallback ASCII-safe pour les ids hors plage (V1 ne devrait pas arriver).
		char buf[32]{};
		std::snprintf(buf, sizeof(buf), "Item #%u", static_cast<unsigned>(itemTemplateId));
		return std::string(buf);
	}

	// -------------------------------------------------------------------------
	// LoadLootTablesFromDb - Wave 5 phase 2 (Phase 3.17b) : warm-load
	// des loot tables depuis le store DB pour log diagnostique. V1 :
	// les entries chargees ne sont pas encore branchees sur SimulateRoll
	// (qui garde son tableau hardcode 5 items). Une PR ulterieure pourra
	// remplacer PickRandomItemIdLocked par un tirage sur les entries DB.
	// -------------------------------------------------------------------------

	void LootHandler::LoadLootTablesFromDb()
	{
		m_loadedLootEntries = 0;
		if (!m_lootStore || !m_lootStore->IsAvailable())
		{
			LOG_INFO(Net, "[LootHandler] LoadLootTablesFromDb : no store / DB unavailable, keeping hardcoded V1 items");
			return;
		}
		auto tables = m_lootStore->LoadAllTables();
		if (tables.empty())
		{
			LOG_INFO(Net, "[LootHandler] LoadLootTablesFromDb : 0 tables in DB (fallback hardcoded V1 items)");
			return;
		}
		for (const auto& t : tables)
		{
			auto entries = m_lootStore->LoadEntriesForTable(t.tableId);
			m_loadedLootEntries += entries.size();
			LOG_INFO(Net, "[LootHandler] LoadLootTablesFromDb : table id={} name={} entries={}",
				t.tableId, t.name, entries.size());
		}
		LOG_INFO(Net, "[LootHandler] LoadLootTablesFromDb : total {} entries across {} tables",
			m_loadedLootEntries, tables.size());
	}

	// -------------------------------------------------------------------------
	// HandlePacket - dispatch + session validation
	// -------------------------------------------------------------------------

	void LootHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[LootHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(LootResponseStatus::Unauthorized);
			switch (opcode)
			{
			case kOpcodeLootRollChoiceRequest:
				pkt = BuildLootRollChoiceResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeLootSimulateRollRequest:
				pkt = BuildLootSimulateRollResponsePacket(kUnauth, 0ull, requestId, sessionIdHeader);
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
		case kOpcodeLootRollChoiceRequest:
			HandleChoice(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeLootSimulateRollRequest:
			HandleSimulateRoll(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// RollDie0to100Locked / PickRandomItemIdLocked
	// -------------------------------------------------------------------------

	uint8_t LootHandler::RollDie0to100Locked()
	{
		if (!m_rngSeeded)
		{
			const auto seed = static_cast<uint32_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			m_rng.seed(seed);
			m_rngSeeded = true;
		}
		std::uniform_int_distribution<int> dist(0, 100);
		return static_cast<uint8_t>(dist(m_rng));
	}

	uint32_t LootHandler::PickRandomItemIdLocked()
	{
		if (!m_rngSeeded)
		{
			const auto seed = static_cast<uint32_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			m_rng.seed(seed);
			m_rngSeeded = true;
		}
		std::uniform_int_distribution<int> dist(1, static_cast<int>(kV1ItemCount));
		return static_cast<uint32_t>(dist(m_rng));
	}

	// -------------------------------------------------------------------------
	// ResolveRollLocked - pick winner, push result, mark resolved.
	// -------------------------------------------------------------------------

	void LootHandler::ResolveRollLocked(ActiveRoll& r)
	{
		if (r.resolved) return;

		// Pour chaque eligible, on lit son choice. Si pas de choice, on
		// considere Pass (timeout). On pioche un roll 0..100 pour les
		// non-Pass. Need > Greed > Pass + plus haut roll dans la meme categorie.
		std::string winnerName;
		uint8_t     winnerChoice = static_cast<uint8_t>(LootChoice::Pass);
		uint8_t     winnerRoll   = 0u;
		LootChoice  winnerCat    = LootChoice::Pass;

		for (size_t i = 0; i < r.eligibleAccountIds.size(); ++i)
		{
			const uint64_t accId = r.eligibleAccountIds[i];
			LootChoice ch = LootChoice::Pass;
			auto it = r.choices.find(accId);
			if (it != r.choices.end())
				ch = it->second;

			if (ch == LootChoice::Pass)
				continue;

			// Pioche le roll 0..100 pour ce participant non-Pass.
			const uint8_t rollVal = RollDie0to100Locked();

			// Compare au winner courant. Need bat Greed bat Pass. Egale categorie =>
			// plus haut roll gagne.
			bool replace = false;
			if (winnerCat == LootChoice::Pass)
			{
				replace = true;
			}
			else if (ch == LootChoice::Need && winnerCat == LootChoice::Greed)
			{
				replace = true;
			}
			else if (ch == LootChoice::Greed && winnerCat == LootChoice::Need)
			{
				replace = false;
			}
			else
			{
				// Meme categorie : compare rolls.
				if (rollVal > winnerRoll)
					replace = true;
			}

			if (replace)
			{
				// Resoudre le nom : V1 simple, on utilise "Account #<id>" comme
				// fallback. Future PR : SessionManager exposera le character
				// name via OnCharacterEnterWorld.
				char nameBuf[32]{};
				std::snprintf(nameBuf, sizeof(nameBuf), "Account #%llu",
					static_cast<unsigned long long>(accId));
				winnerName   = nameBuf;
				winnerChoice = static_cast<uint8_t>(ch);
				winnerRoll   = rollVal;
				winnerCat    = ch;
			}
		}

		// Push RollResultNotification a chaque eligible (V1 : un seul, le creator).
		for (size_t i = 0; i < r.eligibleConnIds.size(); ++i)
		{
			const uint32_t cid = r.eligibleConnIds[i];
			PushRollResult(cid, r.rollId, winnerName, winnerChoice, winnerRoll,
				r.itemTemplateId, r.itemName, r.count);
		}

		r.resolved = true;
		LOG_INFO(Net, "[LootHandler] ResolveRoll rollId={} winner='{}' choice={} roll={}",
			r.rollId, winnerName, static_cast<unsigned>(winnerChoice),
			static_cast<unsigned>(winnerRoll));
	}

	// -------------------------------------------------------------------------
	// ScanExpiredRollsLocked - resolve les rolls dont endsAt est depasse.
	// -------------------------------------------------------------------------

	void LootHandler::ScanExpiredRollsLocked()
	{
		const auto now = std::chrono::steady_clock::now();
		for (auto& kv : m_rolls)
		{
			ActiveRoll& r = kv.second;
			if (r.resolved) continue;
			if (now >= r.endsAt)
			{
				ResolveRollLocked(r);
			}
		}
	}

	// -------------------------------------------------------------------------
	// HandleSimulateRoll - V1 outil dev : creator-only eligible.
	// -------------------------------------------------------------------------

	void LootHandler::HandleSimulateRoll(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseLootSimulateRollRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[LootHandler] SimulateRoll parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildLootSimulateRollResponsePacket(
				static_cast<uint8_t>(LootResponseStatus::Unauthorized), 0ull,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		uint64_t newRollId = 0;
		uint32_t itemTemplateId = 0;
		std::string itemName;
		uint32_t count = 1u;
		constexpr uint32_t kV1DurationSec = 30u;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			newRollId      = m_nextRollId.fetch_add(1u);
			itemTemplateId = PickRandomItemIdLocked();
			itemName       = ResolveItemName(itemTemplateId);

			ActiveRoll r;
			r.rollId         = newRollId;
			r.itemTemplateId = itemTemplateId;
			r.itemName       = itemName;
			r.count          = count;
			r.endsAt         = std::chrono::steady_clock::now()
				+ std::chrono::seconds(kV1DurationSec);
			r.eligibleAccountIds.push_back(accountId);
			r.eligibleConnIds.push_back(connId);
			r.resolved = false;

			m_rolls[newRollId] = std::move(r);
		}

		// Push RollNotification au creator (V1 = seul eligible).
		PushRollNotification(connId, newRollId, itemTemplateId, itemName, count, kV1DurationSec);

		// Reponse Ok + rollId.
		auto pkt = BuildLootSimulateRollResponsePacket(
			static_cast<uint8_t>(LootResponseStatus::Ok), newRollId,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		LOG_INFO(Net, "[LootHandler] SimulateRoll account={} rollId={} item={} count={}",
			accountId, newRollId, itemName, count);
	}

	// -------------------------------------------------------------------------
	// HandleChoice
	// -------------------------------------------------------------------------

	void LootHandler::HandleChoice(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseLootRollChoiceRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[LootHandler] Choice parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildLootRollChoiceResponsePacket(
				static_cast<uint8_t>(LootResponseStatus::InvalidChoice),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t rollId = parsed->rollId;
		const uint8_t  choice = parsed->choice;

		// Wire-level rejette les choices > 2.
		if (choice > 2u)
		{
			LOG_INFO(Net, "[LootHandler] Choice InvalidChoice account={} rollId={} choice={}",
				accountId, rollId, static_cast<unsigned>(choice));
			auto pkt = BuildLootRollChoiceResponsePacket(
				static_cast<uint8_t>(LootResponseStatus::InvalidChoice),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		uint8_t status = static_cast<uint8_t>(LootResponseStatus::Ok);
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			// Avant de traiter, scan les rolls expirees.
			ScanExpiredRollsLocked();

			auto it = m_rolls.find(rollId);
			if (it == m_rolls.end())
			{
				status = static_cast<uint8_t>(LootResponseStatus::RollNotFound);
			}
			else
			{
				ActiveRoll& r = it->second;
				if (r.resolved)
				{
					status = static_cast<uint8_t>(LootResponseStatus::RollEnded);
				}
				else
				{
					// Verifie que accountId est eligible.
					bool eligible = false;
					for (uint64_t accId : r.eligibleAccountIds)
					{
						if (accId == accountId) { eligible = true; break; }
					}
					if (!eligible)
					{
						status = static_cast<uint8_t>(LootResponseStatus::Unauthorized);
					}
					else
					{
						r.choices[accountId] = static_cast<LootChoice>(choice);

						// V1 simple : un seul eligible, donc des qu'il choose, on resout.
						// Plus general : verifier si toutes les choices sont recues.
						if (r.choices.size() >= r.eligibleAccountIds.size())
						{
							ResolveRollLocked(r);
						}
					}
				}
			}
		}

		auto pkt = BuildLootRollChoiceResponsePacket(status, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		LOG_INFO(Net, "[LootHandler] Choice account={} rollId={} choice={} status={}",
			accountId, rollId, static_cast<unsigned>(choice),
			static_cast<unsigned>(status));
	}

	// -------------------------------------------------------------------------
	// PushRollNotification
	// -------------------------------------------------------------------------

	bool LootHandler::PushRollNotification(uint32_t connId, uint64_t rollId, uint32_t itemTemplateId,
		const std::string& itemName, uint32_t count, uint32_t durationSec)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[LootHandler] PushRollNotification dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[LootHandler] PushRollNotification: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildLootRollNotificationPacket(rollId, itemTemplateId, itemName, count,
			durationSec, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[LootHandler] PushRollNotification: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[LootHandler] PushRollNotification connId={} rollId={} item='{}' count={} duration={}s",
			connId, rollId, itemName, count, durationSec);
		return true;
	}

	// -------------------------------------------------------------------------
	// PushRollResult
	// -------------------------------------------------------------------------

	bool LootHandler::PushRollResult(uint32_t connId, uint64_t rollId, const std::string& winnerName,
		uint8_t winnerChoice, uint8_t winnerRoll, uint32_t itemTemplateId,
		const std::string& itemName, uint32_t count)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[LootHandler] PushRollResult dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[LootHandler] PushRollResult: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildLootRollResultNotificationPacket(rollId, winnerName, winnerChoice,
			winnerRoll, itemTemplateId, itemName, count, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[LootHandler] PushRollResult: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[LootHandler] PushRollResult connId={} rollId={} winner='{}' choice={} roll={}",
			connId, rollId, winnerName, static_cast<unsigned>(winnerChoice),
			static_cast<unsigned>(winnerRoll));
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	uint64_t LootHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
