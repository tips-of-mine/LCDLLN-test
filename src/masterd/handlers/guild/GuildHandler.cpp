// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Implementation GuildHandler.

#include "src/masterd/handlers/guild/GuildHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/GuildPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::server
{
	namespace
	{
		/// Noms des rangs WoW par defaut (0=Guild Master ... 9=Initiate).
		/// Static array : adresses stables pour const char* pour le wire.
		const char* const kDefaultRankNames[10] = {
			"Guild Master",
			"Officer",
			"Veteran",
			"Senior",
			"Trusted",
			"Member",
			"Apprentice",
			"Recruit",
			"Probationary",
			"Initiate"
		};
	}

	// -------------------------------------------------------------------------
	// RankName — static helper public.
	// -------------------------------------------------------------------------

	const char* GuildHandler::RankName(uint8_t rankId)
	{
		if (rankId < 10u)
			return kDefaultRankNames[rankId];
		return "?";
	}

	// -------------------------------------------------------------------------
	// SeedV1Guilds — register the 2 hardcoded guilds at boot.
	// -------------------------------------------------------------------------

	void GuildHandler::SeedV1Guilds()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_seeded)
		{
			LOG_DEBUG(Net, "[GuildHandler] SeedV1Guilds already seeded (idempotent skip)");
			return;
		}

		// Guilde 1 : Les Gardiens.
		{
			InMemoryGuild g;
			g.guildId    = 1u;
			g.name       = "Les Gardiens";
			g.motd       = "Soyez courageux";
			g.leaderName = "Aragorn";

			InMemoryGuildMember m1; m1.accountName = "Aragorn"; m1.rankId = 0u; m1.online = true;
			InMemoryGuildMember m2; m2.accountName = "Legolas"; m2.rankId = 1u; m2.online = true;
			InMemoryGuildMember m3; m3.accountName = "Gimli";   m3.rankId = 5u; m3.online = false;
			InMemoryGuildMember m4; m4.accountName = "Frodo";   m4.rankId = 9u; m4.online = false;
			g.members.push_back(m1);
			g.members.push_back(m2);
			g.members.push_back(m3);
			g.members.push_back(m4);

			InMemoryGuildBankItem b1; b1.slotIndex = 0u; b1.itemName = "Iron Ore";       b1.count = 100u;
			InMemoryGuildBankItem b2; b2.slotIndex = 1u; b2.itemName = "Linen Cloth";    b2.count = 250u;
			InMemoryGuildBankItem b3; b3.slotIndex = 2u; b3.itemName = "Mageweave";      b3.count = 80u;
			InMemoryGuildBankItem b4; b4.slotIndex = 3u; b4.itemName = "Health Potion";  b4.count = 30u;
			InMemoryGuildBankItem b5; b5.slotIndex = 4u; b5.itemName = "Mana Potion";    b5.count = 20u;
			g.bank0.push_back(b1);
			g.bank0.push_back(b2);
			g.bank0.push_back(b3);
			g.bank0.push_back(b4);
			g.bank0.push_back(b5);

			m_guilds.push_back(std::move(g));
			m_perms.SetupWowDefaults(1u);
		}

		// Guilde 2 : L'Ombre.
		{
			InMemoryGuild g;
			g.guildId    = 2u;
			g.name       = "L'Ombre";
			g.motd       = "Le pouvoir est tout";
			g.leaderName = "Saruman";

			InMemoryGuildMember m1; m1.accountName = "Saruman";    m1.rankId = 0u; m1.online = true;
			InMemoryGuildMember m2; m2.accountName = "Wormtongue"; m2.rankId = 5u; m2.online = false;
			g.members.push_back(m1);
			g.members.push_back(m2);

			InMemoryGuildBankItem b1; b1.slotIndex = 0u; b1.itemName = "Black Cloth"; b1.count = 50u;
			InMemoryGuildBankItem b2; b2.slotIndex = 1u; b2.itemName = "Soul Shard";  b2.count = 10u;
			g.bank0.push_back(b1);
			g.bank0.push_back(b2);

			m_guilds.push_back(std::move(g));
			m_perms.SetupWowDefaults(2u);
		}

		m_seeded = true;
		LOG_INFO(Net, "[GuildHandler] V1 guilds seeded : Les Gardiens (id=1, 4 members), L'Ombre (id=2, 2 members)");
	}

	// -------------------------------------------------------------------------
	// HandlePacket — dispatch + session validation
	// -------------------------------------------------------------------------

	void GuildHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[GuildHandler] Drop opcode={} : handler not fully wired", opcode);
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
			const uint8_t kUnauth = static_cast<uint8_t>(GuildErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeGuildListRequest:
				pkt = BuildGuildListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeGuildMembersRequest:
				pkt = BuildGuildMembersResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeGuildPermissionsRequest:
				pkt = BuildGuildPermissionsResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeGuildBankRequest:
				pkt = BuildGuildBankResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
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
		case kOpcodeGuildListRequest:
			HandleList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGuildMembersRequest:
			HandleMembers(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGuildPermissionsRequest:
			HandlePermissions(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGuildBankRequest:
			HandleBank(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------
	// FindGuildLocked — m_mutex held by caller.
	// -------------------------------------------------------------------------

	const InMemoryGuild* GuildHandler::FindGuildLocked(uint32_t guildId) const
	{
		for (const auto& g : m_guilds)
		{
			if (g.guildId == guildId) return &g;
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// HandleList
	// -------------------------------------------------------------------------

	void GuildHandler::HandleList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		std::vector<GuildSummary> summaries;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			summaries.reserve(m_guilds.size());
			for (const auto& g : m_guilds)
			{
				GuildSummary s;
				s.guildId     = g.guildId;
				s.name        = g.name;
				s.motd        = g.motd;
				s.memberCount = static_cast<uint32_t>(g.members.size());
				s.leaderName  = g.leaderName;
				summaries.push_back(std::move(s));
			}
		}

		LOG_INFO(Net, "[GuildHandler] List account={} count={}",
			accountId, summaries.size());

		auto pkt = BuildGuildListResponsePacket(0u, summaries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleMembers
	// -------------------------------------------------------------------------

	void GuildHandler::HandleMembers(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseGuildMembersRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[GuildHandler] Members parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildGuildMembersResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint32_t guildId = parsed->guildId;
		std::vector<GuildMember> members;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const InMemoryGuild* g = FindGuildLocked(guildId);
			if (g)
			{
				found = true;
				members.reserve(g->members.size());
				for (const auto& mem : g->members)
				{
					GuildMember wm;
					wm.accountName = mem.accountName;
					wm.rankId      = mem.rankId;
					wm.rankName    = RankName(mem.rankId);
					wm.online      = mem.online;
					members.push_back(std::move(wm));
				}
			}
		}

		if (!found)
		{
			LOG_INFO(Net, "[GuildHandler] Members UnknownGuild account={} guildId={}",
				accountId, guildId);
			auto pkt = BuildGuildMembersResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[GuildHandler] Members account={} guildId={} count={}",
			accountId, guildId, members.size());

		auto pkt = BuildGuildMembersResponsePacket(0u, members, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandlePermissions
	// -------------------------------------------------------------------------

	void GuildHandler::HandlePermissions(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseGuildPermissionsRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[GuildHandler] Permissions parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildGuildPermissionsResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint32_t guildId = parsed->guildId;
		std::vector<GuildRankPerms> ranks;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const InMemoryGuild* g = FindGuildLocked(guildId);
			if (g)
			{
				found = true;
				// Itere les rangs 0..9 V1 (rank fixed).
				ranks.reserve(10u);
				for (uint8_t r = 0u; r < 10u; ++r)
				{
					GuildRankPerms p;
					p.rankId   = r;
					p.rankName = RankName(r);
					p.mask     = m_perms.GetMask(guildId, r);
					ranks.push_back(std::move(p));
				}
			}
		}

		if (!found)
		{
			LOG_INFO(Net, "[GuildHandler] Permissions UnknownGuild account={} guildId={}",
				accountId, guildId);
			auto pkt = BuildGuildPermissionsResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[GuildHandler] Permissions account={} guildId={} ranks=10", accountId, guildId);

		auto pkt = BuildGuildPermissionsResponsePacket(0u, ranks, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// HandleBank
	// -------------------------------------------------------------------------

	void GuildHandler::HandleBank(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseGuildBankRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[GuildHandler] Bank parse failed account={} size={}",
				accountId, payloadSize);
			auto pkt = BuildGuildBankResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint32_t guildId  = parsed->guildId;
		const uint8_t  tabIndex = parsed->tabIndex;

		// V1 : seul le tab 0 est supporte. Tab > 0 => NoPermission.
		if (tabIndex != 0u)
		{
			LOG_INFO(Net, "[GuildHandler] Bank NoPermission account={} guildId={} tab={}",
				accountId, guildId, static_cast<unsigned>(tabIndex));
			auto pkt = BuildGuildBankResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::NoPermission), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		std::vector<GuildBankItem> items;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const InMemoryGuild* g = FindGuildLocked(guildId);
			if (g)
			{
				found = true;
				items.reserve(g->bank0.size());
				for (const auto& b : g->bank0)
				{
					GuildBankItem wi;
					wi.slotIndex = b.slotIndex;
					wi.itemName  = b.itemName;
					wi.count     = b.count;
					items.push_back(std::move(wi));
				}
			}
		}

		if (!found)
		{
			LOG_INFO(Net, "[GuildHandler] Bank UnknownGuild account={} guildId={}",
				accountId, guildId);
			auto pkt = BuildGuildBankResponsePacket(
				static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {},
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[GuildHandler] Bank account={} guildId={} tab=0 count={}",
			accountId, guildId, items.size());

		auto pkt = BuildGuildBankResponsePacket(0u, items, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------
	// PushMotdUpdate
	// -------------------------------------------------------------------------

	bool GuildHandler::PushMotdUpdate(uint32_t connId, uint32_t guildId, const std::string& newMotd)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[GuildHandler] PushMotdUpdate dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[GuildHandler] PushMotdUpdate: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildGuildMotdUpdateNotificationPacket(guildId, newMotd, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[GuildHandler] PushMotdUpdate: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[GuildHandler] PushMotdUpdate connId={} guildId={} motdLen={}",
			connId, guildId, newMotd.size());
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	uint64_t GuildHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
