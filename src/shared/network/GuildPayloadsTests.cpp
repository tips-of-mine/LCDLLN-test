/**
 * CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Round-trip tests for GUILD_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/GuildPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::network;

// -----------------------------------------------------------------------------
// GUILD_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildGuildListRequestPayload();
	Assert(buf.empty(), "Guild List request payload empty");
	auto parsed = ParseGuildListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Guild List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildGuildListResponsePayload(0u, {});
	auto parsed = ParseGuildListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->guilds.empty(),
		"Guild List response empty parses");
}

static void TestListResponseSingleGuild()
{
	GuildSummary g;
	g.guildId     = 1u;
	g.name        = "Les Gardiens";
	g.motd        = "Soyez courageux";
	g.memberCount = 4u;
	g.leaderName  = "Aragorn";
	auto buf = BuildGuildListResponsePayload(0u, {g});
	auto parsed = ParseGuildListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Guild List single guild parses");
	if (parsed && parsed->guilds.size() == 1u)
	{
		Assert(parsed->guilds[0].guildId == 1u, "Guild 1 id");
		Assert(parsed->guilds[0].name == "Les Gardiens", "Guild 1 name");
		Assert(parsed->guilds[0].motd == "Soyez courageux", "Guild 1 motd");
		Assert(parsed->guilds[0].memberCount == 4u, "Guild 1 memberCount");
		Assert(parsed->guilds[0].leaderName == "Aragorn", "Guild 1 leader");
	}
	else
	{
		Assert(false, "List should have 1 guild");
	}
}

static void TestListResponseTwoGuilds()
{
	std::vector<GuildSummary> guilds;
	GuildSummary g1;
	g1.guildId     = 1u;
	g1.name        = "Les Gardiens";
	g1.motd        = "Soyez courageux";
	g1.memberCount = 4u;
	g1.leaderName  = "Aragorn";
	guilds.push_back(g1);

	GuildSummary g2;
	g2.guildId     = 2u;
	g2.name        = "L'Ombre";
	g2.motd        = "Le pouvoir est tout";
	g2.memberCount = 2u;
	g2.leaderName  = "Saruman";
	guilds.push_back(g2);

	auto buf = BuildGuildListResponsePayload(0u, guilds);
	auto parsed = ParseGuildListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guilds.size() == 2u, "List 2 guilds parses");
	if (parsed && parsed->guilds.size() == 2u)
	{
		Assert(parsed->guilds[0].name == "Les Gardiens", "G[0] name");
		Assert(parsed->guilds[1].name == "L'Ombre", "G[1] name");
		Assert(parsed->guilds[1].leaderName == "Saruman", "G[1] leader");
	}
}

static void TestListResponseUnauthorized()
{
	auto buf = BuildGuildListResponsePayload(
		static_cast<uint8_t>(GuildErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "Guild List error has only 1 byte");
	auto parsed = ParseGuildListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "List Unauthorized");
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseGuildListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List rejects nullptr");
}

static void TestListResponseEmptyMotd()
{
	GuildSummary g;
	g.guildId     = 99u;
	g.name        = "NoMotd";
	g.motd        = "";
	g.memberCount = 0u;
	g.leaderName  = "";
	auto buf = BuildGuildListResponsePayload(0u, {g});
	auto parsed = ParseGuildListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guilds.size() == 1u
		&& parsed->guilds[0].motd.empty()
		&& parsed->guilds[0].leaderName.empty(),
		"List accepts empty motd / leader");
}

static void TestListResponseLongName()
{
	GuildSummary g;
	g.guildId     = 7u;
	g.name        = std::string(200u, 'A');
	g.motd        = std::string(500u, 'B');
	g.memberCount = 0xFFFFFFFFu;
	g.leaderName  = "X";
	auto buf = BuildGuildListResponsePayload(0u, {g});
	auto parsed = ParseGuildListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guilds.size() == 1u
		&& parsed->guilds[0].name.size() == 200u
		&& parsed->guilds[0].motd.size() == 500u
		&& parsed->guilds[0].memberCount == 0xFFFFFFFFu,
		"List accepts long name + uint32 max memberCount");
}

static void TestListResponsePacket()
{
	GuildSummary g;
	g.guildId     = 42u;
	g.name        = "Test";
	g.motd        = "Hello";
	g.memberCount = 3u;
	g.leaderName  = "Boss";
	auto pkt = BuildGuildListResponsePacket(0u, {g}, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "List packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"List PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGuildListResponse, "Opcode is ListResponse");
	Assert(view.RequestId() == 999u, "List RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "List SessionId preserved");
	auto parsed = ParseGuildListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->guilds.size() == 1u
		&& parsed->guilds[0].guildId == 42u && parsed->guilds[0].leaderName == "Boss",
		"List payload decodes");
}

// -----------------------------------------------------------------------------
// GUILD_MEMBERS
// -----------------------------------------------------------------------------

static void TestMembersRequestRoundTrip()
{
	auto buf = BuildGuildMembersRequestPayload(7u);
	auto parsed = ParseGuildMembersRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guildId == 7u,
		"Members request round-trip guildId=7");
}

static void TestMembersRequestRejectsShort()
{
	auto parsed = ParseGuildMembersRequestPayload(nullptr, 4u);
	Assert(!parsed.has_value(), "Members request rejects nullptr");
	uint8_t shortBuf[3]{};
	auto parsed2 = ParseGuildMembersRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Members request rejects 3 bytes");
}

static void TestMembersResponseEmpty()
{
	auto buf = BuildGuildMembersResponsePayload(0u, {});
	auto parsed = ParseGuildMembersResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->members.empty(),
		"Members empty parses");
}

static void TestMembersResponseFour()
{
	std::vector<GuildMember> members;
	GuildMember m1; m1.accountName = "Aragorn";  m1.rankId = 0u; m1.rankName = "Guild Master"; m1.online = true;  members.push_back(m1);
	GuildMember m2; m2.accountName = "Legolas";  m2.rankId = 1u; m2.rankName = "Officer";       m2.online = true;  members.push_back(m2);
	GuildMember m3; m3.accountName = "Gimli";    m3.rankId = 5u; m3.rankName = "Member";        m3.online = false; members.push_back(m3);
	GuildMember m4; m4.accountName = "Frodo";    m4.rankId = 9u; m4.rankName = "Initiate";      m4.online = false; members.push_back(m4);
	auto buf = BuildGuildMembersResponsePayload(0u, members);
	auto parsed = ParseGuildMembersResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->members.size() == 4u, "Members 4 parses");
	if (parsed && parsed->members.size() == 4u)
	{
		Assert(parsed->members[0].rankId == 0u && parsed->members[0].online == true,
			"M[0] GM online");
		Assert(parsed->members[2].rankId == 5u && parsed->members[2].online == false,
			"M[2] Member offline");
		Assert(parsed->members[3].accountName == "Frodo", "M[3] name");
	}
}

static void TestMembersResponseUnknownGuild()
{
	auto buf = BuildGuildMembersResponsePayload(
		static_cast<uint8_t>(GuildErrorCode::UnknownGuild), {});
	auto parsed = ParseGuildMembersResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Members UnknownGuild");
}

static void TestMembersResponsePacket()
{
	std::vector<GuildMember> members;
	GuildMember m; m.accountName = "Boss"; m.rankId = 0u; m.rankName = "GM"; m.online = true;
	members.push_back(m);
	auto pkt = BuildGuildMembersResponsePacket(0u, members, 1234u, 0xBEEFull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Members PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGuildMembersResponse, "Opcode is MembersResponse");
	Assert(view.RequestId() == 1234u, "Members RequestId preserved");
	auto parsed = ParseGuildMembersResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->members.size() == 1u
		&& parsed->members[0].accountName == "Boss" && parsed->members[0].online == true,
		"Members payload decodes");
}

// -----------------------------------------------------------------------------
// GUILD_PERMISSIONS
// -----------------------------------------------------------------------------

static void TestPermissionsRequestRoundTrip()
{
	auto buf = BuildGuildPermissionsRequestPayload(2u);
	auto parsed = ParseGuildPermissionsRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guildId == 2u,
		"Permissions request round-trip guildId=2");
}

static void TestPermissionsResponseEmpty()
{
	auto buf = BuildGuildPermissionsResponsePayload(0u, {});
	auto parsed = ParseGuildPermissionsResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->ranks.empty(),
		"Permissions empty parses");
}

static void TestPermissionsResponseTenRanks()
{
	std::vector<GuildRankPerms> ranks;
	for (uint8_t i = 0; i < 10u; ++i)
	{
		GuildRankPerms p;
		p.rankId   = i;
		p.rankName = "Rank";
		p.mask     = (i == 0u) ? 0xFFFFFFFFu : (i == 9u) ? 0u : (1u << i);
		ranks.push_back(p);
	}
	auto buf = BuildGuildPermissionsResponsePayload(0u, ranks);
	auto parsed = ParseGuildPermissionsResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->ranks.size() == 10u, "Perms 10 ranks parses");
	if (parsed && parsed->ranks.size() == 10u)
	{
		Assert(parsed->ranks[0].mask == 0xFFFFFFFFu, "Rank 0 mask all");
		Assert(parsed->ranks[9].mask == 0u, "Rank 9 mask none");
		Assert(parsed->ranks[5].mask == (1u << 5), "Rank 5 mask shift");
	}
}

static void TestPermissionsResponseMaskMaxAndZero()
{
	GuildRankPerms p1; p1.rankId = 0u; p1.rankName = "All";  p1.mask = 0xFFFFFFFFu;
	GuildRankPerms p2; p2.rankId = 9u; p2.rankName = "Zero"; p2.mask = 0u;
	auto buf = BuildGuildPermissionsResponsePayload(0u, {p1, p2});
	auto parsed = ParseGuildPermissionsResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->ranks.size() == 2u
		&& parsed->ranks[0].mask == 0xFFFFFFFFu && parsed->ranks[1].mask == 0u,
		"Perms accept mask 0 and 0xFFFFFFFF");
}

static void TestPermissionsResponsePacket()
{
	GuildRankPerms p; p.rankId = 0u; p.rankName = "GM"; p.mask = 0xFFFFFFFFu;
	auto pkt = BuildGuildPermissionsResponsePacket(0u, {p}, 555u, 0xFEEDull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Perms PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGuildPermissionsResponse, "Opcode is PermsResponse");
	Assert(view.RequestId() == 555u, "Perms RequestId preserved");
}

// -----------------------------------------------------------------------------
// GUILD_BANK
// -----------------------------------------------------------------------------

static void TestBankRequestRoundTrip()
{
	auto buf = BuildGuildBankRequestPayload(3u, 0u);
	auto parsed = ParseGuildBankRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guildId == 3u && parsed->tabIndex == 0u,
		"Bank request round-trip guildId=3 tab=0");
}

static void TestBankRequestRejectsShort()
{
	auto parsed = ParseGuildBankRequestPayload(nullptr, 5u);
	Assert(!parsed.has_value(), "Bank request rejects nullptr");
	uint8_t shortBuf[4]{};
	auto parsed2 = ParseGuildBankRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Bank request rejects 4 bytes");
}

static void TestBankResponseEmpty()
{
	auto buf = BuildGuildBankResponsePayload(0u, {});
	auto parsed = ParseGuildBankResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->items.empty(),
		"Bank empty parses");
}

static void TestBankResponseFiveItems()
{
	std::vector<GuildBankItem> items;
	GuildBankItem it1; it1.slotIndex = 0u; it1.itemName = "Iron Ore";       it1.count = 100u; items.push_back(it1);
	GuildBankItem it2; it2.slotIndex = 1u; it2.itemName = "Linen Cloth";    it2.count = 250u; items.push_back(it2);
	GuildBankItem it3; it3.slotIndex = 2u; it3.itemName = "Mageweave";      it3.count = 80u;  items.push_back(it3);
	GuildBankItem it4; it4.slotIndex = 3u; it4.itemName = "Health Potion";  it4.count = 30u;  items.push_back(it4);
	GuildBankItem it5; it5.slotIndex = 4u; it5.itemName = "Mana Potion";    it5.count = 20u;  items.push_back(it5);
	auto buf = BuildGuildBankResponsePayload(0u, items);
	auto parsed = ParseGuildBankResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->items.size() == 5u, "Bank 5 items parses");
	if (parsed && parsed->items.size() == 5u)
	{
		Assert(parsed->items[0].itemName == "Iron Ore" && parsed->items[0].count == 100u,
			"Bank[0] Iron Ore x100");
		Assert(parsed->items[4].itemName == "Mana Potion" && parsed->items[4].count == 20u,
			"Bank[4] Mana Potion x20");
	}
}

static void TestBankResponseNoPermission()
{
	auto buf = BuildGuildBankResponsePayload(
		static_cast<uint8_t>(GuildErrorCode::NoPermission), {});
	auto parsed = ParseGuildBankResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "Bank NoPermission");
}

static void TestBankResponsePacket()
{
	GuildBankItem it; it.slotIndex = 9u; it.itemName = "Foo"; it.count = 7u;
	auto pkt = BuildGuildBankResponsePacket(0u, {it}, 42u, 0x12345ull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Bank PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGuildBankResponse, "Opcode is BankResponse");
}

// -----------------------------------------------------------------------------
// GUILD_MOTD_UPDATE_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestMotdNotifRoundTrip()
{
	auto buf = BuildGuildMotdUpdateNotificationPayload(1u, "Nouveau MOTD");
	auto parsed = ParseGuildMotdUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "MotdUpdate parses");
	if (parsed)
	{
		Assert(parsed->guildId == 1u, "MotdUpdate guildId");
		Assert(parsed->newMotd == "Nouveau MOTD", "MotdUpdate text");
	}
}

static void TestMotdNotifEmptyMotd()
{
	auto buf = BuildGuildMotdUpdateNotificationPayload(99u, "");
	auto parsed = ParseGuildMotdUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->guildId == 99u && parsed->newMotd.empty(),
		"MotdUpdate accepts empty motd");
}

static void TestMotdNotifLongMotd()
{
	const std::string longMotd(1000u, 'M');
	auto buf = BuildGuildMotdUpdateNotificationPayload(7u, longMotd);
	auto parsed = ParseGuildMotdUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->newMotd.size() == 1000u,
		"MotdUpdate accepts long motd 1000 chars");
}

static void TestMotdNotifRejectsShort()
{
	auto parsed = ParseGuildMotdUpdateNotificationPayload(nullptr, 6u);
	Assert(!parsed.has_value(), "MotdUpdate rejects nullptr");
	uint8_t shortBuf[5]{};
	auto parsed2 = ParseGuildMotdUpdateNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MotdUpdate rejects 5 bytes");
}

static void TestMotdNotifPacket()
{
	auto pkt = BuildGuildMotdUpdateNotificationPacket(2u, "L'Ombre Update", 0xFADEull);
	Assert(!pkt.empty(), "MotdUpdate packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"MotdUpdate PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGuildMotdUpdateNotification, "Opcode is MotdUpdate");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xFADEull, "MotdUpdate SessionId preserved");
	auto parsed = ParseGuildMotdUpdateNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->guildId == 2u
		&& parsed->newMotd == "L'Ombre Update",
		"MotdUpdate payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseSingleGuild();
	TestListResponseTwoGuilds();
	TestListResponseUnauthorized();
	TestListResponseRejectsShort();
	TestListResponseEmptyMotd();
	TestListResponseLongName();
	TestListResponsePacket();

	TestMembersRequestRoundTrip();
	TestMembersRequestRejectsShort();
	TestMembersResponseEmpty();
	TestMembersResponseFour();
	TestMembersResponseUnknownGuild();
	TestMembersResponsePacket();

	TestPermissionsRequestRoundTrip();
	TestPermissionsResponseEmpty();
	TestPermissionsResponseTenRanks();
	TestPermissionsResponseMaskMaxAndZero();
	TestPermissionsResponsePacket();

	TestBankRequestRoundTrip();
	TestBankRequestRejectsShort();
	TestBankResponseEmpty();
	TestBankResponseFiveItems();
	TestBankResponseNoPermission();
	TestBankResponsePacket();

	TestMotdNotifRoundTrip();
	TestMotdNotifEmptyMotd();
	TestMotdNotifLongMotd();
	TestMotdNotifRejectsShort();
	TestMotdNotifPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all guild payload tests passed\n"
	                                : "[FAIL] some guild tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
