#pragma once

// Monde jouable (chantier « Royaumes ») — points de départ par race.
//
// `game/data/world/<monde>/start_locations.json` (GÉNÉRÉ par tools/world_gen)
// associe à chaque race jouable sa ville de départ et la position d'apparition
// d'un NOUVEAU personnage (coordonnées GLOBALES). Lu :
//   - par le master (CharacterCreateHandler) à la création du personnage ;
//   - par le shard, qui y trouve aussi la liste des cellules générées (mur
//     invisible côté serveur, relocalisation d'une position hors monde).
// Ensuite, la dernière position sauvegardée prime (characters.spawn_*).
//
// Format :
//   { "schema_version": 1, "world": "royaumes", "default_race": "humains",
//     "generated_cells": "N003_W009,N011_E008,…",          (optionnel)
//     "races": { "<race>": { "city", "city_name", "x", "y", "z", "yaw_deg" }, … } }
//
// Module pur (Config + Log + WorldCells) → testé en CI (start_locations_tests).

#include "src/shared/world/WorldCells.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
		/// Charge `world/<world>/start_locations.json` depuis `paths.content`.
		/// \return false (table vide) si absent/invalide — non bloquant.
		bool LoadForWorld(const engine::core::Config& config, std::string_view world, std::string& outError);

		/// Point de départ de la race (identifiant exact, ex. "humains"), ou celui
		/// de `default_race` si la race est inconnue, sinon nullptr.
		const StartLocation* Resolve(std::string_view race) const;
		std::size_t Count() const { return m_byRace.size(); }
		/// Tous les départs chargés (ordre quelconque) — validation au boot du shard.
		std::vector<StartLocation> All() const;

		/// Cellules générées (jouables) du monde, dans l'ordre du fichier. Vide si
		/// le fichier ne les liste pas (ancien format) : aucune restriction alors.
		const std::vector<cells::CellCoord>& GeneratedCells() const { return m_generatedCells; }
		/// Vrai si \p c est une cellule générée — ou si la liste est vide.
		/// O(1) : appelée à chaque Input du joueur (mur invisible côté serveur).
		bool IsGeneratedCell(cells::CellCoord c) const;

	private:
		std::unordered_map<std::string, StartLocation> m_byRace;
		std::string m_defaultRace;
		std::vector<cells::CellCoord> m_generatedCells;
		/// Mêmes cellules, indexées par identifiant de zone (recherche O(1)).
		std::unordered_set<uint32_t> m_generatedCellIds;
	};
}
