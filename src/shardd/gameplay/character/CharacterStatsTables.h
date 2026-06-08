#pragma once
// CharacterStatsTables : tables de stats chargées depuis le JSON embarqué
// (character_stats.json) + la taxonomie embarquée (factions.json). Construites
// une fois au boot via FromEmbedded(). Aucun accès disque (anti-triche).

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::server::gameplay
{
	/// Base d'une stat interpolée linéairement lvl1 -> lvl100.
	struct StatBase { double lvl1 = 0.0; double lvl100 = 0.0; };

	/// Multiplicateurs d'un profil d'archétype (class_profiles).
	struct ClassProfile
	{
		double hp=1, resource=1, damage=1, accuracy=1, range=1,
		       crit_rate=1, crit_mult=1, speed=1, perception=1, stealth=1;
	};

	/// Multiplicateurs d'une race (race_profiles). speed_walk/speed_run séparés.
	struct RaceProfile
	{
		double hp=1, resource=1, damage=1, accuracy=1, range=1,
		       crit_rate=1, crit_mult=1, speed_walk=1, speed_run=1, perception=1, stealth=1;
	};

	/// Multiplicateurs de sexe pour un profil (clés absentes = 1.0 au calcul).
	struct SexMods { std::unordered_map<std::string,double> H; std::unordered_map<std::string,double> F; };

	/// Une classe dans une faction (factions.json) : mapping vers profil + ressource.
	struct ClassEntry { std::string id; std::string profile; std::string resource; };

	/// Une faction (factions.json).
	struct FactionEntry { std::string id; std::string race; bool selectable=false;
	                      std::unordered_map<std::string, ClassEntry> classesById; };

	/// Ensemble des tables, prêt pour le calcul.
	struct CharacterStatsTables
	{
		uint32_t levelMax = 100;
		double xpBase = 0.0, xpFactor = 0.0;

		std::unordered_map<std::string, StatBase> bases; // hp,resource,damage,accuracy,range,crit_rate,stamina
		double critMultBase = 1.5;
		double critRateCap = 10.0;
		double speedWalkBase = 2.0, speedRunBase = 5.0, speedSprintBase = 8.0;
		double perceptionLvl1 = 10.0, perceptionPerLevel = 0.5;

		std::unordered_map<std::string, ClassProfile> classProfiles;
		std::unordered_map<std::string, RaceProfile>  raceProfiles;
		std::unordered_map<std::string, SexMods>      sexProfiles; // clé = profil
		std::unordered_map<std::string, FactionEntry> factions;    // clé = factionId

		/// Construit les tables depuis les chaînes JSON embarquées.
		/// \return nullopt si un JSON est invalide ou incomplet.
		static std::optional<CharacterStatsTables> FromEmbedded(
			const char* statsJson, const char* factionsJson);
	};
}
