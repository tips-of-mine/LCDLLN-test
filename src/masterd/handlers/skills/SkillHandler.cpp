// CMANGOS.39 (Phase 4.39 step 3+4) — Implementation SkillHandler.

#include "src/masterd/handlers/skills/SkillHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/SkillPayloads.h"

#include <chrono>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Seuil RNG pour SKILL_USE V1 : 70% Success, 30% Fail. Pas de Crit V1
		/// (le presenter affiche tout de meme l'enum, et on pourra introduire
		/// 5% Crit dans une calibration future). Distribution uniforme [0..99].
		constexpr uint32_t kUseSuccessThresholdPct = 70u;

		/// Cap initial pour un skill juste appris (V1). Aligne avec le starter set.
		constexpr uint16_t kInitialCap = 75u;
	}

	void SkillHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[SkillHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(SkillErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeSkillsListRequest:
				pkt = BuildSkillsListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeSkillLearnRequest:
				pkt = BuildSkillLearnResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeSkillUseRequest:
				pkt = BuildSkillUseResponsePacket(kUnauth, 0u, 0u, requestId, sessionIdHeader);
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
		case kOpcodeSkillsListRequest:
			HandleListRequest(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeSkillLearnRequest:
			HandleLearnRequest(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeSkillUseRequest:
			HandleUseRequest(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void SkillHandler::SeedStarterSetIfNeeded(uint64_t accountId)
	{
		using namespace engine::network;

		auto& entry = m_skillsByAccount[accountId];
		if (!entry.empty())
			return;

		// Starter set V1 : Cooking, Herbalism, Mining, FirstAid (tous value=1
		// pour signaler un debut concret au joueur), Lockpicking (value=0,
		// debutant seulement si trainer apprend).
		entry[1u] = SkillBookEntry{1u, 1u, kInitialCap, 0u}; // Cooking
		entry[2u] = SkillBookEntry{2u, 1u, kInitialCap, 0u}; // Herbalism
		entry[3u] = SkillBookEntry{3u, 1u, kInitialCap, 0u}; // Mining
		entry[4u] = SkillBookEntry{4u, 1u, kInitialCap, 0u}; // FirstAid
		entry[5u] = SkillBookEntry{5u, 0u, kInitialCap, 0u}; // Lockpicking

		LOG_INFO(Net, "[SkillHandler] Seeded starter set for account={} ({} skills)",
			accountId, entry.size());
	}

	// -------------------------------------------------------------------------

	void SkillHandler::HandleListRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		std::vector<SkillBookEntry> entries;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			SeedStarterSetIfNeeded(accountId);
			const auto& map = m_skillsByAccount[accountId];
			entries.reserve(map.size());
			for (const auto& [skillId, e] : map)
			{
				(void)skillId;
				entries.push_back(e);
			}
		}

		LOG_INFO(Net, "[SkillHandler] List account={} count={}", accountId, entries.size());

		auto pkt = BuildSkillsListResponsePacket(0u, entries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------

	void SkillHandler::HandleLearnRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseSkillLearnRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[SkillHandler] Learn parse failed account={}", accountId);
			auto pkt = BuildSkillLearnResponsePacket(
				static_cast<uint8_t>(SkillErrorCode::UnknownSkill), 0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint16_t skillId = parsed->skillId;
		if (skillId == 0u || skillId > kMaxValidSkillId)
		{
			LOG_INFO(Net, "[SkillHandler] Learn UnknownSkill account={} skillId={}",
				accountId, skillId);
			auto pkt = BuildSkillLearnResponsePacket(
				static_cast<uint8_t>(SkillErrorCode::UnknownSkill), 0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		bool alreadyLearned = false;
		uint16_t newValue = 0;
		uint16_t newCap = kInitialCap;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			SeedStarterSetIfNeeded(accountId);
			auto& map = m_skillsByAccount[accountId];
			auto it = map.find(skillId);
			if (it != map.end())
			{
				alreadyLearned = true;
			}
			else
			{
				// Ajout au store V1 avec value=0, cap=75.
				SkillBookEntry e;
				e.skillId = skillId;
				e.value   = 0u;
				e.cap     = kInitialCap;
				e.bonus   = 0u;
				map.emplace(skillId, e);
				newValue = e.value;
				newCap   = e.cap;
			}
		}

		if (alreadyLearned)
		{
			LOG_INFO(Net, "[SkillHandler] Learn AlreadyLearned account={} skillId={}",
				accountId, skillId);
			auto pkt = BuildSkillLearnResponsePacket(
				static_cast<uint8_t>(SkillErrorCode::AlreadyLearned), 0u, requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[SkillHandler] Learn OK account={} skillId={} cap={}",
			accountId, skillId, kInitialCap);

		auto pkt = BuildSkillLearnResponsePacket(0u, kInitialCap, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		// Push UpgradeNotification (delta=0) pour synchroniser le client.
		PushSkillUpgrade(connId, skillId, newValue, newCap, 0);
	}

	// -------------------------------------------------------------------------

	void SkillHandler::HandleUseRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseSkillUseRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[SkillHandler] Use parse failed account={}", accountId);
			auto pkt = BuildSkillUseResponsePacket(
				static_cast<uint8_t>(SkillErrorCode::SkillNotLearned), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint16_t skillId = parsed->skillId;
		const uint64_t targetEntityId = parsed->targetEntityId;

		bool learned = false;
		bool gained  = false;
		uint16_t newValue = 0;
		uint16_t newCap   = 0;
		uint16_t delta    = 0;
		uint8_t  result   = static_cast<uint8_t>(SkillUseResult::Fail);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			SeedStarterSetIfNeeded(accountId);
			auto& map = m_skillsByAccount[accountId];
			auto it = map.find(skillId);
			if (it == map.end())
			{
				learned = false;
			}
			else
			{
				learned = true;
				auto& e = it->second;
				newCap = e.cap;

				// RNG seed lazy au premier usage.
				if (!m_rngSeeded)
				{
					const auto seed = static_cast<std::mt19937::result_type>(
						std::chrono::steady_clock::now().time_since_epoch().count());
					m_rng.seed(seed);
					m_rngSeeded = true;
				}

				// 70% success. V1 : pas de Crit (toujours Fail / Success).
				std::uniform_int_distribution<uint32_t> dist(0u, 99u);
				const uint32_t roll = dist(m_rng);
				if (roll < kUseSuccessThresholdPct)
				{
					result = static_cast<uint8_t>(SkillUseResult::Success);
					// Si already at cap, pas de gain (juste Success).
					if (e.value < e.cap)
					{
						const uint16_t before = e.value;
						const uint32_t after = static_cast<uint32_t>(e.value) + 1u;
						e.value = static_cast<uint16_t>(after > e.cap ? e.cap : after);
						delta = static_cast<uint16_t>(e.value - before);
						gained = (delta > 0u);
					}
					newValue = e.value;
				}
				else
				{
					result = static_cast<uint8_t>(SkillUseResult::Fail);
					newValue = e.value;
				}
			}
		}

		if (!learned)
		{
			LOG_INFO(Net, "[SkillHandler] Use SkillNotLearned account={} skillId={}",
				accountId, skillId);
			auto pkt = BuildSkillUseResponsePacket(
				static_cast<uint8_t>(SkillErrorCode::SkillNotLearned), 0u, 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[SkillHandler] Use account={} skillId={} target={} result={} delta={}",
			accountId, skillId, targetEntityId, static_cast<unsigned>(result), delta);

		auto pkt = BuildSkillUseResponsePacket(0u, result, delta, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		// Push UpgradeNotification si gain effectif > 0.
		if (gained)
		{
			PushSkillUpgrade(connId, skillId, newValue, newCap, static_cast<int16_t>(delta));
		}
	}

	// -------------------------------------------------------------------------

	bool SkillHandler::PushSkillUpgrade(uint32_t connId, uint16_t skillId,
		uint16_t newValue, uint16_t newCap, int16_t delta)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[SkillHandler] PushSkillUpgrade dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			// V1 : on tolere session=0 dans le header push (le client s'est
			// peut-etre deconnecte). On log et on skip.
			LOG_WARN(Net, "[SkillHandler] PushSkillUpgrade: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildSkillUpgradeNotificationPacket(skillId, newValue, newCap, delta, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[SkillHandler] PushSkillUpgrade: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[SkillHandler] PushSkillUpgrade connId={} skillId={} newValue={} newCap={} delta={}",
			connId, skillId, newValue, newCap, static_cast<int>(delta));
		return true;
	}

	// -------------------------------------------------------------------------

	uint64_t SkillHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
