// SND.4 — Tests des ambiances sonores (Ambience) : biomes, lumière du jour,
// volumes cibles par couche selon lieu, heure et météo.

#include "src/client/audio/Ambience.h"

#include <iostream>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	float L(const std::array<float, engine::audio::kAmbienceLayerCount>& t, engine::audio::AmbienceLayer l)
	{
		return t[static_cast<size_t>(l)];
	}
}

int main()
{
	using namespace engine::audio;
	using A = AmbienceLayer;

	// Identifiants : une boucle par couche, tous préfixés amb_.
	for (size_t i = 0; i < kAmbienceLayerCount; ++i)
	{
		const std::string_view id = AmbienceSoundId(static_cast<A>(i));
		Check(id.size() > 4 && id.substr(0, 4) == "amb_", "identifiant de couche " + std::to_string(i));
	}

	// Familles de biomes.
	Check(BiomeFamilyOf('T') == BiomeFamily::Forest, "T = foret");
	Check(BiomeFamilyOf('~') == BiomeFamily::Desert, "~ = desert");
	Check(BiomeFamilyOf('%') == BiomeFamily::Swamp, "% = marais");
	Check(BiomeFamilyOf('*') == BiomeFamily::SnowMountain, "* = montagne enneigee");
	Check(BiomeFamilyOf('^') == BiomeFamily::Mountain, "^ = montagne");
	Check(BiomeFamilyOf('-') == BiomeFamily::Coast, "- = mer peu profonde");
	Check(BiomeFamilyOf('?') == BiomeFamily::Unknown, "code inconnu");
	Check(BiomeHasRiver('r') && !BiomeHasRiver('.'), "riviere");

	// Grille 2×2 couvrant [0, 100[ : "T." / "~%" (ligne j=0 d'abord).
	const std::string grid = "T.~%";
	Check(BiomeAt(grid, 2, 10, 10, 0, 0, 100) == 'T', "BiomeAt (0,0)");
	Check(BiomeAt(grid, 2, 60, 10, 0, 0, 100) == '.', "BiomeAt (1,0)");
	Check(BiomeAt(grid, 2, 10, 60, 0, 0, 100) == '~', "BiomeAt (0,1)");
	Check(BiomeAt(grid, 2, 99, 99, 0, 0, 100) == '%', "BiomeAt (1,1)");
	Check(BiomeAt(grid, 2, -1, 10, 0, 0, 100) == '\0', "BiomeAt hors grille");
	Check(BiomeAt("T", 2, 10, 10, 0, 0, 100) == '\0', "BiomeAt grille trop courte");

	// Lumière du jour.
	Check(DaylightAt(12.0f) == 1.0f, "midi = jour");
	Check(DaylightAt(2.0f) == 0.0f, "2 h = nuit");
	Check(DaylightAt(23.5f) == 0.0f, "23 h 30 = nuit");
	Check(DaylightAt(5.75f) > 0.4f && DaylightAt(5.75f) < 0.6f, "aube progressive");
	Check(DaylightAt(36.0f) == 1.0f, "heure repliee sur 24 h");

	// Forêt de jour : oiseaux, pas de grillons.
	AmbienceInputs in;
	in.biome = 'T';
	in.daylight = 1.0f;
	auto t = AmbienceTargets(in);
	Check(L(t, A::ForestDay) > 0.5f, "foret de jour");
	Check(L(t, A::ForestNight) == 0.0f, "pas de foret de nuit a midi");
	Check(L(t, A::Town) == 0.0f && L(t, A::RainLight) == 0.0f, "foret : ni ville ni pluie");

	// Forêt de nuit.
	in.daylight = 0.0f;
	t = AmbienceTargets(in);
	Check(L(t, A::ForestNight) > 0.5f && L(t, A::ForestDay) == 0.0f, "foret de nuit");

	// Plaine sous la pluie : la faune se tait en partie, la pluie fine joue.
	AmbienceInputs plain;
	plain.biome = '.';
	plain.rain = 0.8f;
	t = AmbienceTargets(plain);
	Check(L(t, A::RainLight) > 0.7f && L(t, A::RainHeavy) == 0.0f, "pluie fine");
	Check(L(t, A::PlainsDay) < 0.5f, "oiseaux attenues sous la pluie");

	// Orage : pluie forte au moins à 0,6, plus de pluie fine.
	plain.storm = true;
	plain.rain = 0.2f;
	t = AmbienceTargets(plain);
	Check(L(t, A::RainHeavy) >= 0.6f && L(t, A::RainLight) == 0.0f, "orage = pluie forte");

	// Ville : rumeur, nature en retrait, plus calme la nuit.
	AmbienceInputs town;
	town.biome = '.';
	town.inTown = true;
	t = AmbienceTargets(town);
	const float townDay = L(t, A::Town);
	Check(townDay > 0.9f, "ville de jour");
	Check(L(t, A::PlainsDay) < 0.5f, "nature en retrait en ville");
	town.daylight = 0.0f;
	t = AmbienceTargets(town);
	Check(L(t, A::Town) < townDay, "ville plus calme la nuit");

	// Montagne enneigée : vent glacé, montagne atténuée ; altitude seule = vent.
	AmbienceInputs snow;
	snow.biome = '*';
	t = AmbienceTargets(snow);
	Check(L(t, A::Snow) == 1.0f && L(t, A::Mountain) < 0.5f, "neige");
	AmbienceInputs high;
	high.biome = '.';
	high.altitude = 600.0f;
	t = AmbienceTargets(high);
	Check(L(t, A::Mountain) == 1.0f, "altitude = vent de montagne");

	// Mer proche : ressac, pas de rivière ; rivière seule sinon.
	AmbienceInputs sea;
	sea.biome = '.';
	sea.seaNearby = true;
	sea.waterNearby = true;
	t = AmbienceTargets(sea);
	Check(L(t, A::Coast) == 1.0f && L(t, A::River) == 0.0f, "mer");
	sea.seaNearby = false;
	t = AmbienceTargets(sea);
	Check(L(t, A::River) > 0.5f && L(t, A::Coast) == 0.0f, "riviere");

	// Monde ancien sans grille de biomes : plaine par défaut.
	t = AmbienceTargets(AmbienceInputs{});
	Check(L(t, A::PlainsDay) >= 0.6f, "biome inconnu = plaine");

	// Tous les volumes restent dans [0, 1].
	AmbienceInputs extreme;
	extreme.biome = '%';
	extreme.treeDensity = 5.0f;
	extreme.rain = 3.0f;
	extreme.altitude = 5000.0f;
	t = AmbienceTargets(extreme);
	for (float v : t)
		Check(v >= 0.0f && v <= 1.0f, "volume borne");

	if (s_failCount == 0)
		std::cout << "ambience_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
