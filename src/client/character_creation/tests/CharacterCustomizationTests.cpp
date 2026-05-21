// src/client/character_creation/tests/CharacterCustomizationTests.cpp
//
// CHAR-MODEL.25 — Tests du système de customisation de personnage.
// Charge les vraies configs game/data/configuration/races/*.json (générées
// depuis races.json, alignées sur les ids de race existants) et vérifie :
//   - chargement de toutes les races + helpers d'accès ;
//   - génération de customisations par défaut / aléatoires VALIDES ;
//   - détection des customisations invalides (race, genre, bornes, index, id) ;
//   - résolution en assets concrets (mesh, attachements, scaling, collision) ;
//   - ordre des tailles par race (nain < humain < orc) ;
//   - round-trip ToJson / FromJson.
//
// CTest tourne avec WORKING_DIRECTORY = CMAKE_SOURCE_DIR, donc les chemins
// "game/data/..." sont résolubles tels quels.

#include "src/client/character_creation/CharacterCustomization.h"
#include "src/client/character_creation/CharacterCustomizationSystem.h"

#include <cmath>
#include <cstdio>
#include <string>

using engine::client::CharacterCustomization;
using engine::client::CharacterCustomizationSystem;
using engine::client::ResolvedCharacterAssets;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond)                                                              \
	do                                                                             \
	{                                                                              \
		if (!(cond))                                                               \
		{                                                                          \
			std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond);  \
			++g_failed;                                                            \
		}                                                                          \
	} while (0)

	void InitSystem(CharacterCustomizationSystem& sys)
	{
		REQUIRE(sys.Initialize("game/data/configuration"));
	}

	void Test_LoadsAllExistingRaces()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);
		// Au moins les 8 races existantes de races.json.
		REQUIRE(sys.RaceCount() >= 8);

		const char* expected[] = {"humains", "elfes",         "orcs",   "nains",
		                          "demons",  "morts_vivants", "corrompus", "divins"};
		for (const char* id : expected)
		{
			REQUIRE(sys.GetRaceConfig(id) != nullptr);
		}

		const auto races = sys.GetAvailableRaces();
		REQUIRE(!races.empty());
	}

	void Test_DefaultAndRandomAreValid()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);
		for (const std::string& race : sys.GetAvailableRaces())
		{
			for (const std::string& gender : {std::string("male"), std::string("female")})
			{
				CharacterCustomization def = sys.MakeDefaultCustomization(race, gender);
				REQUIRE(def.raceId == race);
				REQUIRE(sys.ValidateCustomization(def));

				// Plusieurs tirages aléatoires doivent tous être valides.
				for (int i = 0; i < 25; ++i)
				{
					CharacterCustomization rnd = sys.GenerateRandomCustomization(race, gender);
					if (!sys.ValidateCustomization(rnd))
					{
						const auto errs = sys.GetValidationErrors(rnd);
						std::fprintf(stderr, "[FAIL] random %s/%s invalid: %s\n", race.c_str(),
						             gender.c_str(), errs.empty() ? "?" : errs[0].c_str());
						++g_failed;
						break;
					}
				}
			}
		}
	}

	void Test_RejectsInvalid()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);

		CharacterCustomization bad;
		bad.raceId = "does_not_exist";
		bad.gender = "male";
		REQUIRE(!sys.ValidateCustomization(bad));

		CharacterCustomization base = sys.MakeDefaultCustomization("humains", "male");
		REQUIRE(sys.ValidateCustomization(base));

		// Genre invalide.
		CharacterCustomization g = base;
		g.gender = "other";
		REQUIRE(!sys.ValidateCustomization(g));

		// heightScale hors limites.
		CharacterCustomization h = base;
		h.bodyMetrics.heightScale = 5.0f;
		REQUIRE(!sys.ValidateCustomization(h));

		// headIndex hors limites.
		CharacterCustomization idx = base;
		idx.headIndex = 9999;
		REQUIRE(!sys.ValidateCustomization(idx));

		// skinToneId inconnu.
		CharacterCustomization sk = base;
		sk.skinToneId = "nope";
		REQUIRE(!sys.ValidateCustomization(sk));
	}

	void Test_Resolve()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);
		CharacterCustomization c = sys.MakeDefaultCustomization("humains", "male");
		ResolvedCharacterAssets a = sys.ResolveCustomization(c);

		REQUIRE(a.valid);
		REQUIRE(!a.bodyMeshPath.empty());
		REQUIRE(a.attachments.size() >= 2); // tête + cheveux au minimum
		REQUIRE(a.boneScales.size() == 7);
		REQUIRE(a.collisionRadius > 0.0f);
		REQUIRE(a.collisionHeight > 0.0f);
		REQUIRE(!a.skinHex.empty());

		// Invalide -> résolution invalide.
		CharacterCustomization bad;
		bad.raceId = "nope";
		REQUIRE(!sys.ResolveCustomization(bad).valid);
	}

	void Test_RacialFeatureResolves()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);
		// Les démons ont des cornes : un index de corne doit produire un attachement.
		const auto* demons = sys.GetRaceConfig("demons");
		REQUIRE(demons != nullptr);
		REQUIRE(demons->racialFeatures.count("horns") == 1);

		CharacterCustomization c = sys.MakeDefaultCustomization("demons", "male");
		// Index 1 = première corne non-"none" (index 0 == none dans la config).
		c.hornIndex = 1u;
		REQUIRE(sys.ValidateCustomization(c));

		ResolvedCharacterAssets a = sys.ResolveCustomization(c);
		REQUIRE(a.valid);
		bool hasHorn = false;
		for (const auto& at : a.attachments)
			if (at.kind == "horns")
				hasHorn = true;
		REQUIRE(hasHorn);
	}

	void Test_HeightOrderingByRace()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);
		const auto* nain   = sys.GetRaceConfig("nains");
		const auto* humain = sys.GetRaceConfig("humains");
		const auto* orc    = sys.GetRaceConfig("orcs");
		REQUIRE(nain && humain && orc);

		auto effective = [](const engine::client::RaceConfiguration* r) {
			return r->physicalLimits.height.baseMeters * r->physicalLimits.height.scaleDefault;
		};
		REQUIRE(effective(nain) < effective(humain));
		REQUIRE(effective(humain) < effective(orc));
	}

	void Test_ProportionPresets()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);

		// Les presets de body_proportions.json sont chargés.
		const auto& presets = sys.GetProportionPresets();
		REQUIRE(presets.size() >= 6);
		bool hasAverage = false;
		for (const auto& p : presets)
			if (p.id == "average")
				hasAverage = true;
		REQUIRE(hasAverage);

		// Application sur une race aux limites larges (humains) : 'average'
		// donne des ratios neutres dans les bornes.
		engine::client::CharacterBodyMetrics mh = sys.DefaultMetricsForRace("humains");
		REQUIRE(sys.ApplyProportionPreset("humains", "average", mh));
		REQUIRE(std::fabs(mh.heightScale - 1.0f) < 1e-3f);

		// Application sur les nains (taille max 0.85) : un preset 'grand'
		// (heightScale 1.08) doit être CLAMPÉ aux limites de la race.
		engine::client::CharacterBodyMetrics mn = sys.DefaultMetricsForRace("nains");
		REQUIRE(sys.ApplyProportionPreset("nains", "tall_slender", mn));
		const auto* nains = sys.GetRaceConfig("nains");
		REQUIRE(nains != nullptr);
		REQUIRE(mn.heightScale <= static_cast<float>(nains->physicalLimits.height.scaleMax) + 1e-4f);
		REQUIRE(mn.heightScale >= static_cast<float>(nains->physicalLimits.height.scaleMin) - 1e-4f);

		// Après application d'un preset, les métriques restent valides.
		CharacterCustomization c = sys.MakeDefaultCustomization("nains", "male");
		c.bodyMetrics = mn;
		REQUIRE(sys.ValidateCustomization(c));

		// Race ou preset inconnu -> false.
		engine::client::CharacterBodyMetrics tmp;
		REQUIRE(!sys.ApplyProportionPreset("nope", "average", tmp));
		REQUIRE(!sys.ApplyProportionPreset("humains", "nope", tmp));
	}

	void Test_JsonRoundTrip()
	{
		CharacterCustomizationSystem sys;
		InitSystem(sys);
		CharacterCustomization c = sys.GenerateRandomCustomization("demons", "female");
		c.hornIndex = 2u;
		c.tailIndex = 1u;

		const std::string json = c.ToJson();
		CharacterCustomization back = CharacterCustomization::FromJson(json);

		REQUIRE(back.raceId == c.raceId);
		REQUIRE(back.gender == c.gender);
		REQUIRE(back.bodyTypeId == c.bodyTypeId);
		REQUIRE(back.headIndex == c.headIndex);
		REQUIRE(back.hairStyleIndex == c.hairStyleIndex);
		REQUIRE(back.skinToneId == c.skinToneId);
		REQUIRE(back.hairColorId == c.hairColorId);
		REQUIRE(back.eyeColorId == c.eyeColorId);
		REQUIRE(std::fabs(back.bodyMetrics.heightScale - c.bodyMetrics.heightScale) < 1e-3f);
		REQUIRE(back.hornIndex.has_value() && back.hornIndex.value() == 2u);
		REQUIRE(back.tailIndex.has_value() && back.tailIndex.value() == 1u);
		// Round-trip doit rester valide.
		REQUIRE(sys.ValidateCustomization(back));
	}
} // namespace

int main()
{
	Test_LoadsAllExistingRaces();
	Test_DefaultAndRandomAreValid();
	Test_RejectsInvalid();
	Test_Resolve();
	Test_RacialFeatureResolves();
	Test_HeightOrderingByRace();
	Test_ProportionPresets();
	Test_JsonRoundTrip();

	if (g_failed == 0)
		std::printf("[OK] CharacterCustomizationTests: all assertions passed\n");
	else
		std::fprintf(stderr, "[FAILED] CharacterCustomizationTests: %d assertion(s) failed\n",
		             g_failed);
	return g_failed == 0 ? 0 : 1;
}
