#pragma once

// Monde jouable (chantier « Royaumes ») — points de départ par race.
//
// `game/data/world/start_locations.json` (GÉNÉRÉ par tools/world_gen) associe à
// chaque race jouable sa ville de départ et la position d'apparition d'un
// NOUVEAU personnage. Lu par le master (CharacterCreateHandler) à la création ;
// ensuite, la dernière position sauvegardée prime (characters.spawn_*).
//
// Format :
//   { "schema_version": 1, "world": "royaumes", "default_race": "humains",
//     "races": { "<race>": { "city", "city_name", "x", "y", "z", "yaw_deg" }, … } }
//
// Module pur (Config + Log uniquement) → testé en CI (start_locations_tests).

#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::core { class Config; }

namespace engine::world
{
	struct StartLocation
	{
		std::string race;
		std::string cityId;
		std::string cityName;
		double x = 0.0, y = 0.0, z = 0.0;
		double yawDeg = 0.0;
	};

	class StartLocationTable final
	{
	public:
		/// Parse le JSON en mémoire. Remplace le contenu. \return false si JSON
		/// invalide, schema_version ≠ 1 ou aucune race exploitable.
		bool LoadFromText(std::string_view json, std::string& outError);
		/// Charge `world/start_locations.json` depuis `paths.content`.
		/// \return false (table vide) si absent/invalide — non bloquant.
		bool LoadFromContent(const engine::core::Config& config, std::string& outError);

		/// Point de départ de la race (identifiant exact, ex. "humains"), ou celui
		/// de `default_race` si la race est inconnue, sinon nullptr.
		const StartLocation* Resolve(std::string_view race) const;
		std::size_t Count() const { return m_byRace.size(); }

	private:
		std::unordered_map<std::string, StartLocation> m_byRace;
		std::string m_defaultRace;
	};
}
