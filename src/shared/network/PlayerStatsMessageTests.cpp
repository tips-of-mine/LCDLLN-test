/**
 * R1-B — Round-trip test for the PLAYER_STATS gameplay message.
 *
 * Vérifie qu'un PlayerStatsMessage encodé puis décodé restitue tous ses champs.
 * Test pur (pas de DB / pas de réseau). NDEBUG-safe : pas d'assert ; retourne 0 en
 * cas de succès, 1 à la première erreur.
 */

#include "src/shared/network/ServerProtocol.h"

#include <cstdio>
#include <string>

using engine::server::PlayerStatsMessage;
using engine::server::EncodePlayerStats;
using engine::server::DecodePlayerStats;

namespace
{
	/// Compare deux flottants à 1e-3 près (tolérance round-trip IEEE-754).
	bool FloatNear(float a, float b)
	{
		const float diff = (a > b) ? (a - b) : (b - a);
		return diff <= 1e-3f;
	}

	/// Encode/décode \p src et vérifie l'égalité champ-à-champ. Renvoie true si OK.
	bool RoundTrip(const PlayerStatsMessage& src, const char* label)
	{
		const std::vector<std::byte> packet = EncodePlayerStats(src);

		PlayerStatsMessage out;
		if (!DecodePlayerStats(packet, out))
		{
			fprintf(stderr, "[FAIL] %s: DecodePlayerStats returned false\n", label);
			return false;
		}

		if (out.clientId != src.clientId)
		{
			fprintf(stderr, "[FAIL] %s: clientId %u != %u\n", label, out.clientId, src.clientId);
			return false;
		}
		if (out.maxHealth != src.maxHealth)
		{
			fprintf(stderr, "[FAIL] %s: maxHealth %u != %u\n", label, out.maxHealth, src.maxHealth);
			return false;
		}
		if (out.resource != src.resource)
		{
			fprintf(stderr, "[FAIL] %s: resource %u != %u\n", label, out.resource, src.resource);
			return false;
		}
		if (out.stamina != src.stamina)
		{
			fprintf(stderr, "[FAIL] %s: stamina %u != %u\n", label, out.stamina, src.stamina);
			return false;
		}
		if (out.damage != src.damage)
		{
			fprintf(stderr, "[FAIL] %s: damage %u != %u\n", label, out.damage, src.damage);
			return false;
		}
		if (!FloatNear(out.accuracy, src.accuracy))
		{
			fprintf(stderr, "[FAIL] %s: accuracy %f != %f\n", label, out.accuracy, src.accuracy);
			return false;
		}
		if (!FloatNear(out.range, src.range))
		{
			fprintf(stderr, "[FAIL] %s: range %f != %f\n", label, out.range, src.range);
			return false;
		}
		if (!FloatNear(out.critRate, src.critRate))
		{
			fprintf(stderr, "[FAIL] %s: critRate %f != %f\n", label, out.critRate, src.critRate);
			return false;
		}
		if (!FloatNear(out.critMult, src.critMult))
		{
			fprintf(stderr, "[FAIL] %s: critMult %f != %f\n", label, out.critMult, src.critMult);
			return false;
		}
		if (!FloatNear(out.speedWalk, src.speedWalk))
		{
			fprintf(stderr, "[FAIL] %s: speedWalk %f != %f\n", label, out.speedWalk, src.speedWalk);
			return false;
		}
		if (!FloatNear(out.speedRun, src.speedRun))
		{
			fprintf(stderr, "[FAIL] %s: speedRun %f != %f\n", label, out.speedRun, src.speedRun);
			return false;
		}
		if (!FloatNear(out.speedSprint, src.speedSprint))
		{
			fprintf(stderr, "[FAIL] %s: speedSprint %f != %f\n", label, out.speedSprint, src.speedSprint);
			return false;
		}
		if (!FloatNear(out.perception, src.perception))
		{
			fprintf(stderr, "[FAIL] %s: perception %f != %f\n", label, out.perception, src.perception);
			return false;
		}
		if (!FloatNear(out.stealth, src.stealth))
		{
			fprintf(stderr, "[FAIL] %s: stealth %f != %f\n", label, out.stealth, src.stealth);
			return false;
		}
		if (out.resourceKey != src.resourceKey)
		{
			fprintf(stderr, "[FAIL] %s: resourceKey \"%s\" != \"%s\"\n",
				label, out.resourceKey.c_str(), src.resourceKey.c_str());
			return false;
		}

		return true;
	}
}

int main()
{
	// Cas 1 — valeurs distinctes pour chaque champ + resourceKey non vide.
	PlayerStatsMessage full;
	full.clientId = 42;
	full.maxHealth = 3078;
	full.resource = 1200;
	full.stamina = 800;
	full.damage = 250;
	full.accuracy = 88.5f;
	full.range = 30.0f;
	full.critRate = 7.5f;
	full.critMult = 1.8f;
	full.speedWalk = 2.0f;
	full.speedRun = 5.0f;
	full.speedSprint = 8.0f;
	full.perception = 12.5f;
	full.stealth = 9.0f;
	full.resourceKey = "ferveur";

	if (!RoundTrip(full, "full"))
	{
		return 1;
	}

	// Cas 2 — resourceKey vide (chaîne taillée de longueur 0).
	PlayerStatsMessage empty = full;
	empty.resourceKey = "";

	if (!RoundTrip(empty, "emptyResourceKey"))
	{
		return 1;
	}

	printf("[OK] PlayerStatsMessage round-trip\n");
	return 0;
}
