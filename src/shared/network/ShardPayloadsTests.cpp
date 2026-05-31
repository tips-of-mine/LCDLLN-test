/**
 * Tests unitaires — round-trip du payload SHARD_HEARTBEAT enrichi (protocole v9).
 * Pur (pas de réseau, pas de DB). Retourne 0 si OK, non-zéro sinon.
 *
 * Couvre :
 *  - heartbeat legacy (0 joueur) : build/parse, players vide.
 *  - heartbeat enrichi (N joueurs) : chaque champ {accountId, characterId, level, zoneId}
 *    fait l'aller-retour.
 */

#include "src/shared/network/ShardPayloads.h"

#include <cstdlib>
#include <iostream>

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

int main()
{
	TestHeartbeatLegacyNoPlayers();
	TestHeartbeatWithPlayers();
	TestHeartbeatTruncatedRejected();
	std::cerr << (s_failCount == 0 ? "[OK] all shard_payloads tests passed\n" : "[FAIL] some tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
