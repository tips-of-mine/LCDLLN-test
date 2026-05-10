// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Implementation Parse/Build des
// payloads Guild.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client (envoyees via SendGenericRequestAsync
//     qui ajoute le header).
//   - Build*ResponsePacket / Build*NotificationPacket utilise PacketBuilder
//     pour assembler le paquet complet header + payload, pret a passer a
//     NetServer::Send.
//   - Parse* lit le payload nu (sans header).

#include "src/shared/network/GuildPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	namespace
	{
		/// Ecrit un GuildSummary (guildId, name, motd, memberCount, leaderName).
		bool WriteGuildSummary(ByteWriter& w, const GuildSummary& g)
		{
			if (!w.WriteU32(g.guildId))     return false;
			if (!w.WriteString(g.name))     return false;
			if (!w.WriteString(g.motd))     return false;
			if (!w.WriteU32(g.memberCount)) return false;
			if (!w.WriteString(g.leaderName)) return false;
			return true;
		}

		/// Lit un GuildSummary.
		bool ReadGuildSummary(ByteReader& r, GuildSummary& out)
		{
			if (!r.ReadU32(out.guildId))     return false;
			if (!r.ReadString(out.name))     return false;
			if (!r.ReadString(out.motd))     return false;
			if (!r.ReadU32(out.memberCount)) return false;
			if (!r.ReadString(out.leaderName)) return false;
			return true;
		}

		/// Ecrit un GuildMember (accountName, rankId, rankName, online).
		bool WriteGuildMember(ByteWriter& w, const GuildMember& m)
		{
			if (!w.WriteString(m.accountName)) return false;
			if (!w.WriteBytes(&m.rankId, 1u))  return false;
			if (!w.WriteString(m.rankName))    return false;
			const uint8_t onlineByte = m.online ? 1u : 0u;
			if (!w.WriteBytes(&onlineByte, 1u)) return false;
			return true;
		}

		/// Lit un GuildMember.
		bool ReadGuildMember(ByteReader& r, GuildMember& out)
		{
			if (!r.ReadString(out.accountName)) return false;
			uint8_t rankByte = 0;
			if (!r.ReadBytes(&rankByte, 1u))    return false;
			out.rankId = rankByte;
			if (!r.ReadString(out.rankName))    return false;
			uint8_t onlineByte = 0;
			if (!r.ReadBytes(&onlineByte, 1u))  return false;
			out.online = (onlineByte != 0u);
			return true;
		}

		/// Ecrit un GuildRankPerms (rankId, rankName, mask).
		bool WriteGuildRankPerms(ByteWriter& w, const GuildRankPerms& p)
		{
			if (!w.WriteBytes(&p.rankId, 1u)) return false;
			if (!w.WriteString(p.rankName))   return false;
			if (!w.WriteU32(p.mask))          return false;
			return true;
		}

		/// Lit un GuildRankPerms.
		bool ReadGuildRankPerms(ByteReader& r, GuildRankPerms& out)
		{
			uint8_t rankByte = 0;
			if (!r.ReadBytes(&rankByte, 1u)) return false;
			out.rankId = rankByte;
			if (!r.ReadString(out.rankName)) return false;
			if (!r.ReadU32(out.mask))        return false;
			return true;
		}

		/// Ecrit un GuildBankItem (slotIndex, itemName, count).
		bool WriteGuildBankItem(ByteWriter& w, const GuildBankItem& it)
		{
			if (!w.WriteU32(it.slotIndex)) return false;
			if (!w.WriteString(it.itemName)) return false;
			if (!w.WriteU32(it.count))     return false;
			return true;
		}

		/// Lit un GuildBankItem.
		bool ReadGuildBankItem(ByteReader& r, GuildBankItem& out)
		{
			if (!r.ReadU32(out.slotIndex)) return false;
			if (!r.ReadString(out.itemName)) return false;
			if (!r.ReadU32(out.count))     return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// GUILD_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<GuildListRequestPayload> ParseGuildListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return GuildListRequestPayload{};
	}

	std::vector<uint8_t> BuildGuildListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// GUILD_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<GuildListResponsePayload> ParseGuildListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.guilds.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			GuildSummary g;
			if (!ReadGuildSummary(r, g)) return std::nullopt;
			out.guilds.push_back(std::move(g));
		}
		return out;
	}

	std::vector<uint8_t> BuildGuildListResponsePayload(uint8_t error, const std::vector<GuildSummary>& guilds)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(guilds.size()))) return {};
			for (const auto& g : guilds)
			{
				if (!WriteGuildSummary(w, g)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGuildListResponsePacket(uint8_t error, const std::vector<GuildSummary>& guilds,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(guilds.size()))) return {};
			for (const auto& g : guilds)
			{
				if (!WriteGuildSummary(w, g)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGuildListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GUILD_MEMBERS — Request
	// -------------------------------------------------------------------------

	std::optional<GuildMembersRequestPayload> ParseGuildMembersRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildMembersRequestPayload out;
		if (!r.ReadU32(out.guildId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGuildMembersRequestPayload(uint32_t guildId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(guildId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// GUILD_MEMBERS — Response
	// -------------------------------------------------------------------------

	std::optional<GuildMembersResponsePayload> ParseGuildMembersResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildMembersResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.members.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			GuildMember m;
			if (!ReadGuildMember(r, m)) return std::nullopt;
			out.members.push_back(std::move(m));
		}
		return out;
	}

	std::vector<uint8_t> BuildGuildMembersResponsePayload(uint8_t error, const std::vector<GuildMember>& members)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(members.size()))) return {};
			for (const auto& m : members)
			{
				if (!WriteGuildMember(w, m)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGuildMembersResponsePacket(uint8_t error, const std::vector<GuildMember>& members,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(members.size()))) return {};
			for (const auto& m : members)
			{
				if (!WriteGuildMember(w, m)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGuildMembersResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GUILD_PERMISSIONS — Request
	// -------------------------------------------------------------------------

	std::optional<GuildPermissionsRequestPayload> ParseGuildPermissionsRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildPermissionsRequestPayload out;
		if (!r.ReadU32(out.guildId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGuildPermissionsRequestPayload(uint32_t guildId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(guildId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// GUILD_PERMISSIONS — Response
	// -------------------------------------------------------------------------

	std::optional<GuildPermissionsResponsePayload> ParseGuildPermissionsResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildPermissionsResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.ranks.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			GuildRankPerms p;
			if (!ReadGuildRankPerms(r, p)) return std::nullopt;
			out.ranks.push_back(std::move(p));
		}
		return out;
	}

	std::vector<uint8_t> BuildGuildPermissionsResponsePayload(uint8_t error, const std::vector<GuildRankPerms>& ranks)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(ranks.size()))) return {};
			for (const auto& p : ranks)
			{
				if (!WriteGuildRankPerms(w, p)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGuildPermissionsResponsePacket(uint8_t error, const std::vector<GuildRankPerms>& ranks,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(ranks.size()))) return {};
			for (const auto& p : ranks)
			{
				if (!WriteGuildRankPerms(w, p)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGuildPermissionsResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GUILD_BANK — Request
	// -------------------------------------------------------------------------

	std::optional<GuildBankRequestPayload> ParseGuildBankRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 guildId (4) + uint8 tabIndex (1) = 5.
		if (!payload || payloadSize < 5u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildBankRequestPayload out;
		if (!r.ReadU32(out.guildId)) return std::nullopt;
		uint8_t tabByte = 0;
		if (!r.ReadBytes(&tabByte, 1u)) return std::nullopt;
		out.tabIndex = tabByte;
		return out;
	}

	std::vector<uint8_t> BuildGuildBankRequestPayload(uint32_t guildId, uint8_t tabIndex)
	{
		std::vector<uint8_t> buf(5u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(guildId)) return {};
		if (!w.WriteBytes(&tabIndex, 1u)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// GUILD_BANK — Response
	// -------------------------------------------------------------------------

	std::optional<GuildBankResponsePayload> ParseGuildBankResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildBankResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.items.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			GuildBankItem it;
			if (!ReadGuildBankItem(r, it)) return std::nullopt;
			out.items.push_back(std::move(it));
		}
		return out;
	}

	std::vector<uint8_t> BuildGuildBankResponsePayload(uint8_t error, const std::vector<GuildBankItem>& items)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(items.size()))) return {};
			for (const auto& it : items)
			{
				if (!WriteGuildBankItem(w, it)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGuildBankResponsePacket(uint8_t error, const std::vector<GuildBankItem>& items,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(items.size()))) return {};
			for (const auto& it : items)
			{
				if (!WriteGuildBankItem(w, it)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGuildBankResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GUILD_MOTD_UPDATE_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<GuildMotdUpdateNotificationPayload> ParseGuildMotdUpdateNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 guildId (4) + uint16 string length (2) = 6 (string vide possible).
		if (!payload || payloadSize < 6u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GuildMotdUpdateNotificationPayload out;
		if (!r.ReadU32(out.guildId))      return std::nullopt;
		if (!r.ReadString(out.newMotd))   return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGuildMotdUpdateNotificationPayload(uint32_t guildId, const std::string& newMotd)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(guildId))   return {};
		if (!w.WriteString(newMotd)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGuildMotdUpdateNotificationPacket(uint32_t guildId, const std::string& newMotd,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteU32(guildId))   return {};
		if (!w.WriteString(newMotd)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGuildMotdUpdateNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
