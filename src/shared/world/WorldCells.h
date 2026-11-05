#pragma once

// Monde en cellules — découpage des cartes continues en CELLULES de 10 km × 10 km.
//
// Deux CARTES coexistent (registre \ref CellMap) : le continent « Royaumes » et
// « Les Enfers » (petite carte volcanique de 5 cellules en croix, docs/world/ENFERS.md).
// Chaque carte a son repère GLOBAL propre (même convention ci-dessous), son préfixe
// de dossier (`cell_` / `enfers_`), sa plage d'identifiants de zone et son
// identifiant de carte : deux points de même (x, z) sur deux cartes différentes
// ne se voient jamais.
//
// Convention (docs/world_cell_grid_convention.md, docs/world/ROYAUMES.md) :
//   - 1 case de la carte du lore = 1 cellule = 1 zone moteur de 10 km de côté ;
//   - repère GLOBAL unique en mètres : X = est, Z = sud (le nord est en -Z),
//     origine (0,0) = CENTRE de la cellule N000_E000 de la carte (Royaumes :
//     l'Oracle `cell_n000_e000` ; Enfers : la Citadelle `enfers_n000_e000`) ;
//   - la cellule (n, e) — n > 0 au nord, e > 0 à l'est — couvre
//       x ∈ [e·10000 − 5000, e·10000 + 5000[  et  z ∈ [−n·10000 − 5000, −n·10000 + 5000[ ;
//   - identifiant disque : `cell_<n|s><DDD>_<e|w><DDD>` (minuscules, 3 chiffres)
//     pour les Royaumes, `enfers_<n|s><DDD>_<e|w><DDD>` pour les Enfers ;
//     identifiant du lore : `N003_W009` (majuscules) ;
//   - identifiant numérique de zone serveur (uint32, sans collision avec les
//     zones historiques 0, 1, 2…) : base de la carte + (n+500)·1000 + (e+500)
//     (Royaumes : 1 000 000, Enfers : 2 000 000) — la carte se déduit de l'id.
//
// La cellule d'un point se DÉDUIT de sa position et de la carte courante : le
// client et le serveur la calculent chacun de leur côté (aucun champ réseau,
// aucune colonne DB ; la carte d'un personnage vient de son zone_id persisté).
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

	/// Cartes jouables découpées en cellules. L'ordre (valeurs) est figé : il sert
	/// d'indice dans le registre interne (WorldCells.cpp).
	enum class CellMap : uint8_t
	{
		Royaumes = 0, ///< le continent (carte historique, valeur par défaut partout)
		Enfers = 1,   ///< seconde carte : « Les Enfers » (5 cellules en croix)
	};
	/// Nombre de cartes du registre.
	inline constexpr int kCellMapCount = 2;

	/// Bornes de la carte du lore ROYAUMES (indices inclus) : 42 rangées N023 → S018,
	/// 46 colonnes W019 → E026 (agrandie en lore v1.0.52 : extrême nord, côte
	/// orientale et confins du Midi). Doit couvrir la grille du master
	/// (game/data/world/royaumes/world_master.json) hors colonnes de marge.
	inline constexpr int kMinN = -18;
	inline constexpr int kMaxN = 23;
	inline constexpr int kMinE = -19;
	inline constexpr int kMaxE = 26;

	/// Bornes de la carte des ENFERS (indices inclus) : croix de 5 cellules centrée
	/// sur N000_E000, dans un carré −1..1 (les coins ne sont pas générés).
	inline constexpr int kEnfersMinN = -1;
	inline constexpr int kEnfersMaxN = 1;
	inline constexpr int kEnfersMinE = -1;
	inline constexpr int kEnfersMaxE = 1;

	/// Base des identifiants numériques de zone « cellule » des Royaumes. Les zones
	/// historiques (feyhin = 1, zone_0/1/2…) restent sous cette valeur.
	inline constexpr uint32_t kCellZoneIdBase = 1000000u;
	/// Base des identifiants de zone des cellules des Enfers (plage disjointe de
	/// celle des Royaumes : [2 000 000, 3 000 000[).
	inline constexpr uint32_t kEnfersZoneIdBase = 2000000u;
	/// Largeur d'une plage d'identifiants de zone (une carte) : 1000 × 1000 cellules.
	inline constexpr uint32_t kCellZoneIdSpan = 1000000u;

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

	/// Vrai si la cellule appartient à la grille de la carte \p map (Royaumes :
	/// bornes kMin/kMax du lore ; Enfers : carré −1..1).
	bool IsInLoreGrid(CellCoord c, CellMap map = CellMap::Royaumes);

	/// Nom de dossier de la cellule : "cell_n003_w009" (Royaumes) ou
	/// "enfers_n000_e000" (Enfers). \pre |n|, |e| ≤ 999.
	std::string CellDirName(CellCoord c, CellMap map = CellMap::Royaumes);
	/// Décode un nom de dossier de la carte \p map ("cell_n003_w009" pour les
	/// Royaumes, "enfers_n000_e000" pour les Enfers ; minuscules strictes).
	/// nullopt si invalide ou si le préfixe est celui d'une autre carte.
	std::optional<CellCoord> ParseCellDirName(std::string_view s, CellMap map = CellMap::Royaumes);
	/// Carte d'un nom de dossier de cellule (n'importe quelle carte), nullopt si
	/// \p s n'est pas un dossier de cellule valide.
	std::optional<CellMap> CellMapOfDirName(std::string_view s);
	/// "N003_W009" (identifiant de la carte du lore).
	std::string LoreCellId(CellCoord c);
	/// Décode "N003_W009" (majuscules strictes). nullopt si invalide.
	std::optional<CellCoord> ParseLoreCellId(std::string_view s);

	/// Identifiant numérique de zone serveur de la cellule \p c de la carte \p map.
	/// \pre |n|,|e| < 500.
	uint32_t CellZoneId(CellCoord c, CellMap map = CellMap::Royaumes);
	/// Vrai si \p zoneId encode une cellule d'une carte QUELCONQUE (Royaumes ou Enfers).
	bool IsCellZoneId(uint32_t zoneId);
	/// Décode un identifiant de zone cellule (toutes cartes). nullopt si ce n'en est pas un.
	std::optional<CellCoord> CellFromZoneId(uint32_t zoneId);
	/// Carte d'un identifiant de zone cellule. nullopt si ce n'est pas une cellule.
	std::optional<CellMap> CellMapOfZoneId(uint32_t zoneId);

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

	/// Nom de `world.active_zone` qui active le monde en cellules (le continent ;
	/// la carte des Enfers lui est TOUJOURS associée, elle n'est jamais « active_zone »).
	inline constexpr std::string_view kCellWorldName = "royaumes";

	/// Identifiant de CARTE commun à toutes les cellules des Royaumes (grille
	/// d'intérêt, comparaisons « même carte »). Hors de la plage des identifiants
	/// de cellule et des zones historiques.
	inline constexpr uint32_t kCellWorldMapId = 999999u;
	/// Identifiant de CARTE des cellules des Enfers (même rôle que kCellWorldMapId).
	inline constexpr uint32_t kEnfersMapId = 999998u;

	/// Vrai si \p activeZone (valeur de `world.active_zone`) désigne le monde en cellules.
	bool IsCellWorld(std::string_view activeZone);

	/// Identifiant de monde (sous-dossier `world/<id>/` du contenu) de la carte :
	/// "royaumes" ou "enfers".
	std::string_view WorldIdOf(CellMap map);
	/// Carte d'un identifiant de monde ("royaumes", "enfers"), nullopt sinon.
	std::optional<CellMap> CellMapFromWorldId(std::string_view worldId);
	/// Identifiant de carte (kCellWorldMapId, kEnfersMapId) de la carte \p map.
	uint32_t MapIdOf(CellMap map);
	/// Carte d'un identifiant de carte (inverse de MapIdOf), nullopt sinon.
	std::optional<CellMap> CellMapFromMapId(uint32_t mapId);
	/// Carte d'une zone quelconque : la carte de la cellule, ou Royaumes par défaut
	/// (zone historique, identifiant inconnu). Utile pour les chemins de contenu.
	CellMap CellMapOfZoneOr(uint32_t zoneId, CellMap fallback = CellMap::Royaumes);

	/// Carte d'une zone : MapIdOf(carte) pour une cellule (kCellWorldMapId pour les
	/// Royaumes, kEnfersMapId pour les Enfers), la zone elle-même sinon (zones
	/// historiques feyhin / zone_0 / zone_1 / zone_2).
	uint32_t MapIdOfZone(uint32_t zoneId);

	/// Vrai si les deux zones appartiennent à la même carte (deux cellules de la
	/// même carte, ou deux zones historiques identiques). Faux entre une cellule
	/// des Royaumes et une cellule des Enfers.
	bool SameMap(uint32_t zoneA, uint32_t zoneB);

	/// Vrai si le dossier `zones/<dirName>` appartient au monde \p activeZone :
	/// monde en cellules → uniquement les dossiers de cellules (`cell_*` des
	/// Royaumes et `enfers_*` des Enfers) ; sinon → tous les dossiers SAUF ceux
	/// des cellules (les cellules générées ne polluent pas les zones historiques).
	/// Utilisé par les chargeurs serveur (spawners, événements, récolte).
	bool ZoneDirBelongsToWorld(std::string_view dirName, std::string_view activeZone);

	/// Région du monde en cellules : une cellule D'UNE carte donnée. Identité unique
	/// toutes cartes confondues (deux cellules (0,0) de deux cartes sont distinctes).
	/// Construite implicitement depuis une CellCoord ou {n, e} (carte Royaumes par défaut).
	struct MapCell
	{
		CellCoord cell{};
		CellMap map = CellMap::Royaumes;
		MapCell() = default;
		MapCell(CellCoord c, CellMap m = CellMap::Royaumes) : cell(c), map(m) {}
		/// Raccourci {n, e[, carte]} (accolades : `IsActive({ 3, -9 })`).
		MapCell(int n, int e, CellMap m = CellMap::Royaumes) : cell{ n, e }, map(m) {}
		bool operator==(const MapCell& o) const { return map == o.map && cell == o.cell; }
		bool operator!=(const MapCell& o) const { return !(*this == o); }
	};
	/// Identifiant de zone de la région (CellZoneId(cell, map)) : clé unique et
	/// réversible (MapCellFromZoneId). \pre |n|,|e| < 500.
	uint32_t MapCellZoneId(const MapCell& region);
	/// Inverse de MapCellZoneId. nullopt si \p zoneId n'est pas une cellule.
	std::optional<MapCell> MapCellFromZoneId(uint32_t zoneId);
	/// "N003_W009" (Royaumes) ou "ENFERS:N000_E000" — journaux uniquement.
	std::string MapCellLabel(const MapCell& region);
}
