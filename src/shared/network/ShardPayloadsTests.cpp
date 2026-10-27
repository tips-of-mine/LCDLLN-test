/**
 * Tests unitaires — round-trip du payload SHARD_HEARTBEAT enrichi (protocole v9).
 * Pur (pas de réseau, pas de DB). Retourne 0 si OK, non-zéro sinon.
 *
 * Couvre :
 *  - heartbeat legacy (0 joueur) : build/parse, players vide.
 *  - heartbeat enrichi (N joueurs) : chaque champ {accountId, characterId, level, zoneId}
 *    fait l'aller-retour.
 */

#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ShardPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ByteReader.h"

#include <cstdint>
#include <cstdlib>
#include <array>
#include <iostream>
#include <string>
#include <vector>

using engine::network::ShardPlayerPresence;
using engine::network::BuildShardHeartbeatPayload;
using engine::network::ParseShardHeartbeatPayload;

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

static void TestHeartbeatLegacyNoPlayers()
{
	auto buf = BuildShardHeartbeatPayload(7u, 3u, 123456u);
	Assert(buf.size() == 16u, "legacy heartbeat fait 16 octets (pas de tableau joueurs)");
	auto parsed = ParseShardHeartbeatPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "parse legacy OK");
	if (parsed)
	{
		Assert(parsed->shard_id == 7u, "shard_id round-trip");
		Assert(parsed->current_load == 3u, "current_load round-trip");
		Assert(parsed->timestamp == 123456u, "timestamp round-trip");
		Assert(parsed->players.empty(), "players vide en legacy");
	}
}

static void TestHeartbeatWithPlayers()
{
	std::vector<ShardPlayerPresence> players = {
		{ 1ull, 100ull, 7u, 42u },
		{ 2ull, 200ull, 1u, 0u },
		{ 9999ull, 123456ull, 60u, 1337u },
	};
	auto buf = BuildShardHeartbeatPayload(5u, 3u, 999u, players);
	Assert(buf.size() == 16u + 2u + 3u * 24u, "taille = 16 + 2 + 3*24");
	auto parsed = ParseShardHeartbeatPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "parse enrichi OK");
	if (parsed)
	{
		Assert(parsed->shard_id == 5u, "shard_id round-trip (enrichi)");
		Assert(parsed->current_load == 3u, "current_load round-trip (enrichi)");
		Assert(parsed->players.size() == 3u, "3 joueurs parsés");
		if (parsed->players.size() == 3u)
		{
			Assert(parsed->players[0].accountId == 1ull && parsed->players[0].characterId == 100ull
				&& parsed->players[0].level == 7u && parsed->players[0].zoneId == 42u, "joueur 0 round-trip complet");
			Assert(parsed->players[2].accountId == 9999ull && parsed->players[2].characterId == 123456ull
				&& parsed->players[2].level == 60u && parsed->players[2].zoneId == 1337u, "joueur 2 round-trip complet");
		}
	}
}

static void TestHeartbeatTruncatedRejected()
{
	// Tableau annoncé (count=1) mais octets joueur tronqués -> parse échoue.
	std::vector<ShardPlayerPresence> players = { { 1ull, 100ull, 7u, 42u } };
	auto buf = BuildShardHeartbeatPayload(5u, 0u, 0u, players);
	buf.resize(buf.size() - 4u); // ampute le dernier u32 (zoneId)
	auto parsed = ParseShardHeartbeatPayload(buf.data(), buf.size());
	Assert(!parsed.has_value(), "payload tronqué rejeté (nullopt)");
}

// Roadmap-7 (2026-07-19) — round-trip complet du push d'admission avec guilde :
// Build (paquet entier) → PacketView (extraction payload) → Parse.
static void TestAdmitCharacterRoundTripWithGuild()
{
	auto pkt = engine::network::BuildAdmitCharacterPacket(42ull, 1337ull, "Aria", "female", 7ull);
	Assert(!pkt.empty(), "admit: build OK");
	engine::network::PacketView view;
	Assert(engine::network::PacketView::Parse(pkt.data(), pkt.size(), view)
		== engine::network::PacketParseResult::Ok, "admit: paquet parsable");
	auto parsed = engine::network::ParseAdmitCharacterPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value(), "admit: parse payload OK");
	if (parsed)
	{
		Assert(parsed->account_id == 42ull, "admit: account_id round-trip");
		Assert(parsed->character_id == 1337ull, "admit: character_id round-trip");
		Assert(parsed->character_name == "Aria", "admit: name round-trip");
		Assert(parsed->gender == "female", "admit: gender round-trip");
		Assert(parsed->guild_id == 7ull, "admit: guild_id round-trip");
	}
}

// Roadmap-7 — compat master ANCIEN (sans guild_id) : payload amputé de ses 16
// derniers octets (u64 guild_id + u64 owner_generation de queue, ETAPE1.2)
// → parse OK, guild_id = 0.
static void TestAdmitCharacterLegacyWithoutGuild()
{
	auto pkt = engine::network::BuildAdmitCharacterPacket(42ull, 1337ull, "Aria", "male", 9ull);
	engine::network::PacketView view;
	Assert(engine::network::PacketView::Parse(pkt.data(), pkt.size(), view)
		== engine::network::PacketParseResult::Ok, "admit legacy: paquet parsable");
	std::vector<uint8_t> legacyPayload(view.Payload(), view.Payload() + view.PayloadSize() - 16u);
	auto parsed = engine::network::ParseAdmitCharacterPayload(legacyPayload.data(), legacyPayload.size());
	Assert(parsed.has_value(), "admit legacy: parse OK sans guild_id");
	if (parsed)
	{
		Assert(parsed->character_name == "Aria", "admit legacy: name intact");
		Assert(parsed->gender == "male", "admit legacy: gender intact");
		Assert(parsed->guild_id == 0ull, "admit legacy: guild_id retombe a 0");
	}
	// Payload minimal 16 octets (master TA.3 d'origine : ni nom, ni genre, ni guilde).
	std::vector<uint8_t> minimal(16u, 0u);
	auto parsedMin = engine::network::ParseAdmitCharacterPayload(minimal.data(), minimal.size());
	Assert(parsedMin.has_value(), "admit legacy: payload 16 octets parse OK");
	if (parsedMin)
	{
		Assert(parsedMin->character_name.empty() && parsedMin->gender.empty()
			&& parsedMin->guild_id == 0ull, "admit legacy 16o: champs optionnels par defaut");
	}
}

// ETAPE1.2 — génération du verrou de propriété : round-trip, et compat master
// sans le champ (payload amputé des 8 derniers octets) → owner_generation = 0,
// guild_id intact.
static void TestAdmitCharacterOwnerGeneration()
{
	auto pkt = engine::network::BuildAdmitCharacterPacket(42ull, 1337ull, "Aria", "female", 7ull, 12ull);
	engine::network::PacketView view;
	Assert(engine::network::PacketView::Parse(pkt.data(), pkt.size(), view)
		== engine::network::PacketParseResult::Ok, "admit owner: paquet parsable");
	auto parsed = engine::network::ParseAdmitCharacterPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->owner_generation == 12ull && parsed->guild_id == 7ull,
		"admit owner: owner_generation round-trip");
	std::vector<uint8_t> withoutGeneration(view.Payload(), view.Payload() + view.PayloadSize() - 8u);
	auto legacy = engine::network::ParseAdmitCharacterPayload(withoutGeneration.data(), withoutGeneration.size());
	Assert(legacy.has_value() && legacy->owner_generation == 0ull && legacy->guild_id == 7ull,
		"admit owner: master sans generation -> 0, guild_id intact");
}

// ETAPE3.1 — clé de session UDP en queue : round-trip, absence (master antérieur),
// queue de mauvaise taille refusée. La requête EnterWorld client -> master porte la
// même queue (même règles).
static void TestAdmitCharacterUdpSessionKey()
{
	std::array<uint8_t, 32> key{};
	for (size_t i = 0; i < key.size(); ++i)
		key[i] = static_cast<uint8_t>(i * 7u + 1u);
	auto pkt = engine::network::BuildAdmitCharacterPacket(42ull, 1337ull, "Aria", "female", 7ull, 12ull, &key);
	engine::network::PacketView view;
	Assert(engine::network::PacketView::Parse(pkt.data(), pkt.size(), view)
		== engine::network::PacketParseResult::Ok, "admit key: paquet parsable");
	auto parsed = engine::network::ParseAdmitCharacterPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->has_udp_session_key && parsed->udp_session_key == key
		&& parsed->owner_generation == 12ull, "admit key: round-trip");
	std::vector<uint8_t> withoutKey(view.Payload(), view.Payload() + view.PayloadSize() - 32u);
	auto legacy = engine::network::ParseAdmitCharacterPayload(withoutKey.data(), withoutKey.size());
	Assert(legacy.has_value() && !legacy->has_udp_session_key && legacy->owner_generation == 12ull,
		"admit key: master sans cle -> pas de cle, generation intacte");
	std::vector<uint8_t> truncatedKey(view.Payload(), view.Payload() + view.PayloadSize() - 1u);
	Assert(!engine::network::ParseAdmitCharacterPayload(truncatedKey.data(), truncatedKey.size()).has_value(),
		"admit key: cle tronquee refusee");

	auto req = engine::network::BuildCharacterEnterWorldRequestPayload(1337ull, "Aria", &key);
	auto parsedReq = engine::network::ParseCharacterEnterWorldRequestPayload(req.data(), req.size());
	Assert(parsedReq.has_value() && parsedReq->hasUdpSessionKey && parsedReq->udpSessionKey == key
		&& parsedReq->characterName == "Aria", "enter world key: round-trip");
	auto reqLegacy = engine::network::BuildCharacterEnterWorldRequestPayload(1337ull, "Aria");
	auto parsedLegacy = engine::network::ParseCharacterEnterWorldRequestPayload(reqLegacy.data(), reqLegacy.size());
	Assert(parsedLegacy.has_value() && !parsedLegacy->hasUdpSessionKey, "enter world key: client sans cle");
	req.pop_back();
	Assert(!engine::network::ParseCharacterEnterWorldRequestPayload(req.data(), req.size()).has_value(),
		"enter world key: cle tronquee refusee");
}

// ETAPE5.1 — notification de perte de shard : aller-retour, opcode, payload tronqué.
static void TestShardLostNotification()
{
	using namespace engine::network;
	auto pkt = BuildShardLostNotificationPacket(9u, ShardLostReason::HeartbeatTimeout, 77ull);
	PacketView view;
	Assert(!pkt.empty() && PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "shard_lost: paquet parsable");
	Assert(view.Opcode() == kOpcodeShardLostNotification && view.RequestId() == 0u, "shard_lost: opcode 211, push (request_id 0)");
	auto parsed = ParseShardLostNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed && parsed->shardId == 9u && parsed->reason == ShardLostReason::HeartbeatTimeout, "shard_lost: round-trip");
	Assert(!ParseShardLostNotificationPayload(view.Payload(), 4u), "shard_lost: payload tronqué refusé");
	Assert(!ParseShardLostNotificationPayload(nullptr, 5u), "shard_lost: payload nul refusé");
}

// ETAPE5.3 — ordre d'arrêt ordonné master → shard : aller-retour, motif tronqué.
static void TestMasterToShardShutdown()
{
	using namespace engine::network;
	auto pkt = BuildMasterToShardShutdownPacket("maintenance");
	PacketView view;
	Assert(!pkt.empty() && PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "shutdown: paquet parsable");
	Assert(view.Opcode() == kOpcodeMasterToShardShutdown && view.RequestId() == 0u, "shutdown: opcode 212, push");
	auto reason = ParseMasterToShardShutdownPayload(view.Payload(), view.PayloadSize());
	Assert(reason && *reason == "maintenance", "shutdown: motif round-trip");
	const std::string longReason(500, 'x');
	auto pkt2 = BuildMasterToShardShutdownPacket(longReason);
	PacketView v2;
	Assert(PacketView::Parse(pkt2.data(), pkt2.size(), v2) == PacketParseResult::Ok, "shutdown: long motif parsable");
	auto r2 = ParseMasterToShardShutdownPayload(v2.Payload(), v2.PayloadSize());
	Assert(r2 && r2->size() == 200u, "shutdown: motif tronqué à 200 octets");
	Assert(!ParseMasterToShardShutdownPayload(nullptr, 0u), "shutdown: payload nul refusé");
}

// ETAPE5.4 — queue de contre-pression du heartbeat : aller-retour, compatibilité.
static void TestHeartbeatBackPressure()
{
	using namespace engine::network;
	ShardBackPressure bp;
	bp.saturated = true;
	bp.tickLoadPercent = 93;
	auto noPlayers = BuildShardHeartbeatPayload(4u, 10u, 99ull, {}, &bp);
	auto a = ParseShardHeartbeatPayload(noPlayers.data(), noPlayers.size());
	Assert(a && a->shard_id == 4u && a->players.empty() && a->hasBackPressure && a->saturated && a->tickLoadPercent == 93u,
		"contre-pression sans joueur : aller-retour");
	std::vector<ShardPlayerPresence> players(2);
	players[0].accountId = 1; players[0].characterId = 11;
	players[1].accountId = 2; players[1].characterId = 22;
	bp.saturated = false;
	auto withPlayers = BuildShardHeartbeatPayload(4u, 10u, 99ull, players, &bp);
	auto b = ParseShardHeartbeatPayload(withPlayers.data(), withPlayers.size());
	Assert(b && b->players.size() == 2u && b->players[1].characterId == 22u && b->hasBackPressure && !b->saturated,
		"contre-pression avec joueurs : aller-retour");
	// Master antérieur : il lit le tableau puis s'arrête (les 4 octets de queue sont ignorés) ;
	// ici on vérifie qu'un heartbeat SANS queue reste sans contre-pression.
	auto legacy = BuildShardHeartbeatPayload(4u, 10u, 99ull, players);
	auto c = ParseShardHeartbeatPayload(legacy.data(), legacy.size());
	Assert(c && c->players.size() == 2u && !c->hasBackPressure, "heartbeat v9 : pas de contre-pression");
	Assert(withPlayers.size() == legacy.size() + 5u, "queue de 5 octets");
	// Lecture « master antérieur » (v9) : 16 octets fixes, u16 count, entrées — la queue
	// est simplement laissée de côté.
	{
		engine::network::ByteReader r(withPlayers.data(), withPlayers.size());
		uint32_t sid = 0, load = 0;
		uint64_t ts = 0;
		uint16_t count = 0;
		bool okOld = r.ReadU32(sid) && r.ReadU32(load) && r.ReadU64(ts) && r.ReadU16(count) && count == 2u;
		for (uint16_t i = 0; okOld && i < count; ++i)
		{
			uint64_t a = 0, ch = 0;
			uint32_t lv = 0, z = 0;
			okOld = r.ReadU64(a) && r.ReadU64(ch) && r.ReadU32(lv) && r.ReadU32(z);
		}
		Assert(okOld && r.Remaining() == 5u, "master v9 : tableau lu, queue ignorée");
	}
	// Version future plus longue : les champs v1 restent lus, le reste est sauté.
	{
		std::vector<uint8_t> future = withPlayers;
		future[future.size() - 4u] = 6u; // longueur 6 (3 octets supplémentaires)
		future[future.size() - 5u] = 2u; // version 2
		future.push_back(0xAA); future.push_back(0xBB); future.push_back(0xCC);
		auto f = ParseShardHeartbeatPayload(future.data(), future.size());
		Assert(f && f->hasBackPressure && !f->saturated && f->tickLoadPercent == 93u, "version future : champs v1 lus");
	}
	// Queue tronquée : pas de contre-pression (le heartbeat reste accepté).
	{
		auto t = ParseShardHeartbeatPayload(withPlayers.data(), withPlayers.size() - 1u);
		Assert(t && t->players.size() == 2u && !t->hasBackPressure, "queue tronquée ignorée");
	}
}

int main()
{
	TestHeartbeatBackPressure();
	TestMasterToShardShutdown();
	TestShardLostNotification();
	TestHeartbeatLegacyNoPlayers();
	TestHeartbeatWithPlayers();
	TestHeartbeatTruncatedRejected();
	TestAdmitCharacterRoundTripWithGuild();
	TestAdmitCharacterLegacyWithoutGuild();
	TestAdmitCharacterOwnerGeneration();
	TestAdmitCharacterUdpSessionKey();
	std::cerr << (s_failCount == 0 ? "[OK] all shard_payloads tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
