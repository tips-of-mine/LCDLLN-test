#pragma once

// Monde « Royaumes » — découpage du monde continu en CELLULES de 10 km × 10 km.
//
// Convention (docs/world_cell_grid_convention.md, docs/world/ROYAUMES.md) :
//   - 1 case de la carte du lore = 1 cellule = 1 zone moteur de 10 km de côté ;
//   - repère GLOBAL unique en mètres : X = est, Z = sud (le nord est en -Z),
//     origine (0,0) = CENTRE de la cellule de l'Oracle `cell_n000_e000` ;
//   - la cellule (n, e) — n > 0 au nord, e > 0 à l'est — couvre
//       x ∈ [e·10000 − 5000, e·10000 + 5000[  et  z ∈ [−n·10000 − 5000, −n·10000 + 5000[ ;
//   - identifiant disque : `cell_<n|s><DDD>_<e|w><DDD>` (minuscules, 3 chiffres) ;
//     identifiant du lore : `N003_W009` (majuscules) ;
//   - identifiant numérique de zone serveur (uint32, sans collision avec les
//     zones historiques 0, 1, 2…) : kCellZoneIdBase + (n+500)·1000 + (e+500).
//
// La cellule d'un point se DÉDUIT de sa position : le client et le serveur la
// calculent chacun de leur côté (aucun champ réseau, aucune colonne DB).
//
// Module PUR (aucune dépendance moteur) : utilisé par le client, le shard, le
// master et tools/world_gen ; testé en CI (world_cells_tests).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::world::cells
{
	/// Côté d'une cellule (m). Doit rester égal à engine::world::kZoneSize (client).
	inline constexpr double kCellSizeMeters = 10000.0;
	/// Demi-côté (m) : les coordonnées LOCALES d'une cellule sont dans [−5000, 5000[.
	inline constexpr double kCellHalfMeters = kCellSizeMeters * 0.5;

	/// Bornes de la carte du lore (indices inclus) : 33 rangées N018 → S014,
	/// 40 colonnes W019 → E020.
	inline constexpr int kMinN = -14;
	inline constexpr int kMaxN = 18;
	inline constexpr int kMinE = -19;
	inline constexpr int kMaxE = 20;

	/// Base des identifiants numériques de zone « cellule ». Les zones historiques
	/// (feyhin = 1, zone_0/1/2…) restent sous cette valeur.
	inline constexpr uint32_t kCellZoneIdBase = 1000000u;

	/// Coordonnées entières d'une cellule (n > 0 au nord, e > 0 à l'est).
	struct CellCoord
	{
		int n = 0;
		int e = 0;
		bool operator==(const CellCoord& o) const { return n == o.n && e == o.e; }
		bool operator!=(const CellCoord& o) const { return !(*this == o); }
	};

	/// Position (m, repère global) d'un point du plan XZ.
	struct CellPoint
	{
		double x = 0.0;
		double z = 0.0;
	};

	/// Cellule contenant le point global (x, z). Toujours défini (hors carte inclus).
	CellCoord CellFromPosition(double x, double z);
	/// Centre global de la cellule.
	CellPoint CellCenter(CellCoord c);
	/// Coin minimal (x min, z min) de la cellule : origine de sa heightmap.
	CellPoint CellMinCorner(CellCoord c);
	/// Global → local à la cellule \p c (centre = 0, plage [−5000, 5000[ si dedans).
	CellPoint ToLocal(CellCoord c, double x, double z);
	/// Local à la cellule \p c → global.
	CellPoint ToGlobal(CellCoord c, double localX, double localZ);

	/// Vrai si la cellule appartient à la carte du lore (bornes kMin/kMax).
	bool IsInLoreGrid(CellCoord c);

	/// "cell_n003_w009". \pre |n|, |e| ≤ 999.
	std::string CellDirName(CellCoord c);
	/// Décode "cell_n003_w009" (minuscules strictes). nullopt si invalide.
	std::optional<CellCoord> ParseCellDirName(std::string_view s);
	/// "N003_W009" (identifiant de la carte du lore).
	std::string LoreCellId(CellCoord c);
	/// Décode "N003_W009" (majuscules strictes). nullopt si invalide.
	std::optional<CellCoord> ParseLoreCellId(std::string_view s);

	/// Identifiant numérique de zone serveur de la cellule. \pre |n|,|e| < 500.
	uint32_t CellZoneId(CellCoord c);
	/// Vrai si \p zoneId encode une cellule (≥ kCellZoneIdBase).
	bool IsCellZoneId(uint32_t zoneId);
	/// Décode un identifiant de zone cellule. nullopt si ce n'en est pas un.
	std::optional<CellCoord> CellFromZoneId(uint32_t zoneId);

	/// Distance (m) du point global (x, z) au bord le plus proche de SA cellule
	/// (0 sur la frontière, 5000 au centre). Sert au préchargement des voisines.
	double DistanceToCellBorder(double x, double z);

	/// Distance (m) du point global (x, z) au rectangle de la cellule \p c
	/// (0 si le point est dedans). Sert à l'activation des régions voisines.
	double DistanceToCell(CellCoord c, double x, double z);

	// ------------------------------------------------------------------
	// Monde « une seule carte » : les cellules sont des RÉGIONS d'une même
	// carte. Deux entités de cellules différentes se voient, se combattent et
	// se parlent comme si elles étaient dans la même zone ; seule l'activation
	// (simulation, spawners) est découpée par cellule.
	// ------------------------------------------------------------------

	/// Nom de `world.active_zone` qui active le monde en cellules.
	inline constexpr std::string_view kCellWorldName = "royaumes";

	/// Identifiant de CARTE commun à toutes les cellules (grille d'intérêt,
	/// comparaisons « même carte »). Hors de la plage des identifiants de
	/// cellule et des zones historiques.
	inline constexpr uint32_t kCellWorldMapId = 999999u;

	/// Vrai si \p activeZone (valeur de `world.active_zone`) désigne le monde en cellules.
	bool IsCellWorld(std::string_view activeZone);

	/// Carte d'une zone : kCellWorldMapId pour une cellule, la zone elle-même sinon
	/// (zones historiques feyhin / zone_0 / zone_1 / zone_2).
	uint32_t MapIdOfZone(uint32_t zoneId);

	/// Vrai si les deux zones appartiennent à la même carte (deux cellules
	/// quelconques, ou deux zones historiques identiques).
	bool SameMap(uint32_t zoneA, uint32_t zoneB);

	/// Vrai si le dossier `zones/<dirName>` appartient au monde \p activeZone :
	/// monde en cellules → uniquement les dossiers `cell_*` ; sinon → tous les
	/// dossiers SAUF `cell_*` (les cellules générées ne polluent pas les zones
	/// historiques). Utilisé par les chargeurs serveur (spawners, événements, récolte).
	bool ZoneDirBelongsToWorld(std::string_view dirName, std::string_view activeZone);
}
