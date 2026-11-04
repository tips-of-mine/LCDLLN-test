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
// Seconde carte (Les Enfers, docs/world/ENFERS.md) : le monde « royaumes » fusionne
// la table de `world/enfers/start_locations.json` (départ des démons, cellules des
// Enfers). Chaque départ et chaque cellule générée porte sa CARTE ; le champ
// "world" du fichier la désigne ("royaumes" par défaut).
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
		/// Carte du départ (Royaumes, ou Enfers pour les démons).
		cells::CellMap map = cells::CellMap::Royaumes;
	};

	class StartLocationTable final
	{
	public:
		/// Parse le JSON en mémoire. Remplace le contenu. La carte des départs et des
		/// cellules est celle du champ "world" ("enfers" → Enfers, sinon Royaumes).
		/// \return false si JSON invalide, schema_version ≠ 1 ou aucune race exploitable.
		bool LoadFromText(std::string_view json, std::string& outError);
		/// Fusionne la table d'une AUTRE carte (même format) : ses départs et ses
		/// cellules générées s'ajoutent ; `default_race` reste celui de la table
		/// principale. \return false (contenu inchangé) si le texte est invalide, si
		/// sa carte est déjà chargée ou si une de ses races a déjà un départ.
		bool MergeFromText(std::string_view json, std::string& outError);
		/// Charge `world/<world>/start_locations.json` depuis `paths.content`.
		/// Monde « royaumes » : fusionne aussi `world/enfers/start_locations.json`
		/// s'il existe (absent = pas de seconde carte, non bloquant ; invalide =
		/// ignoré, raison dans \p outWarning).
		/// \return false (table vide) si le fichier principal est absent/invalide — non bloquant.
		bool LoadForWorld(const engine::core::Config& config, std::string_view world, std::string& outError);
		/// Même chose, avec l'avertissement de la fusion des Enfers (vide si tout va bien).
		bool LoadForWorld(const engine::core::Config& config, std::string_view world, std::string& outError,
			std::string& outWarning);

		/// Point de départ de la race (identifiant exact, ex. "humains"), ou celui
		/// de `default_race` si la race est inconnue, sinon nullptr.
		const StartLocation* Resolve(std::string_view race) const;
		std::size_t Count() const { return m_byRace.size(); }
		/// Tous les départs chargés (ordre quelconque) — validation au boot du shard.
		std::vector<StartLocation> All() const;

		/// Cellules générées (jouables) de la carte \p map, dans l'ordre du fichier.
		/// Vide si le fichier ne les liste pas (ancien format) ou si la carte n'est
		/// pas chargée.
		const std::vector<cells::CellCoord>& GeneratedCells(cells::CellMap map = cells::CellMap::Royaumes) const;
		/// Vrai si \p c est une cellule générée de la carte \p map. Royaumes sans
		/// liste (ancien format) : toujours vrai (aucune restriction). Autre carte
		/// sans liste (non chargée) : toujours faux (personne n'y entre).
		/// O(1) : appelée à chaque Input du joueur (mur invisible côté serveur).
		bool IsGeneratedCell(cells::CellCoord c, cells::CellMap map = cells::CellMap::Royaumes) const;
		/// Vrai si au moins une cellule de la carte \p map est générée (carte chargée).
		bool HasMap(cells::CellMap map) const { return !GeneratedCells(map).empty(); }

	private:
		/// Vide la table (toutes cartes).
		void Clear();

		std::unordered_map<std::string, StartLocation> m_byRace;
		std::string m_defaultRace;
		/// Cellules générées par carte (indice = valeur de cells::CellMap).
		std::vector<cells::CellCoord> m_generatedCells[cells::kCellMapCount];
		/// Mêmes cellules, toutes cartes, indexées par identifiant de zone (unique
		/// entre cartes : recherche O(1)).
		std::unordered_set<uint32_t> m_generatedCellIds;
	};
}
