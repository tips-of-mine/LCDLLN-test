/**
 * Système de personnages PR2 — tests round-trip du payload CHARACTER_CREATE_REQUEST
 * avec le nouveau champ factionId (et vérification de la rétro-compat sans factionId).
 *
 * Tests d'encodage purs — pas de DB / pas de réseau. Robustes sous NDEBUG :
 * on n'utilise pas assert() mais un retour 0 (succès) / 1 (premier échec).
 */

#include "src/shared/network/CharacterPayloads.h"

#include <cstdio>
#include <string>

using namespace engine::network;

namespace
{
	// Vérifie une condition ; en cas d'échec, imprime sur stderr et signale l'échec
	// via *fail (drapeau partagé) — NDEBUG-safe (pas d'assert).
	void Check(bool cond, const char* msg, int* fail)
	{
		if (!cond)
		{
			std::fprintf(stderr, "[FAIL] %s\n", msg);
			*fail = 1;
		}
	}

	// Round-trip complet : build avec factionId, parse, vérifie tous les champs.
	void TestRoundTripWithFaction(int* fail)
	{
		CharacterCustomization custom{};
		custom.faceType  = 3;
		custom.hairStyle = 7;

		auto buf = BuildCharacterCreateRequestPayload(
			"Hero", "elfes", "voleur_tenebreux", custom, "female", "elfe");
		Check(!buf.empty(), "build (avec factionId) non vide", fail);

		auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
		Check(parsed.has_value(), "parse (avec factionId) OK", fail);
		if (parsed)
		{
			Check(parsed->name == "Hero", "name round-trip", fail);
			Check(parsed->raceId == "elfes", "raceId round-trip", fail);
			Check(parsed->classId == "voleur_tenebreux", "classId round-trip", fail);
			Check(parsed->customization.faceType == 3, "faceType round-trip", fail);
			Check(parsed->customization.hairStyle == 7, "hairStyle round-trip", fail);
			Check(parsed->gender == "female", "gender round-trip", fail);
			Check(parsed->factionId == "elfe", "factionId round-trip", fail);
		}
	}

	// Rétro-compat : build SANS factionId (arg défaut omis), parse, vérifie que
	// factionId est vide et que les autres champs sont intacts.
	void TestBackwardCompatNoFaction(int* fail)
	{
		CharacterCustomization custom{};
		custom.skinColorIdx = 2;

		// Builder appelé sans le nouvel argument factionId (valeur par défaut {}).
		auto buf = BuildCharacterCreateRequestPayload(
			"Hero", "elfes", "voleur_tenebreux", custom, "female");
		Check(!buf.empty(), "build (sans factionId) non vide", fail);

		auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
		Check(parsed.has_value(), "parse (sans factionId) OK", fail);
		if (parsed)
		{
			Check(parsed->name == "Hero", "name intact", fail);
			Check(parsed->raceId == "elfes", "raceId intact", fail);
			Check(parsed->classId == "voleur_tenebreux", "classId intact", fail);
			Check(parsed->customization.skinColorIdx == 2, "skinColorIdx intact", fail);
			Check(parsed->gender == "female", "gender intact", fail);
			Check(parsed->factionId.empty(), "factionId vide quand omis", fail);
		}
	}
}

int main()
{
	int fail = 0;
	TestRoundTripWithFaction(&fail);
	TestBackwardCompatNoFaction(&fail);
	return fail;
}
