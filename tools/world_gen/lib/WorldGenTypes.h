#pragma once

// tools/world_gen — types partagés du générateur de monde « Royaumes ».
//
// Le générateur est un outil OFFLINE déterministe : à partir de la source de
// vérité `game/data/world/<world>/world_master.json` (+ imports explicites du
// contenu existant), il produit toutes les données runtime du monde jouable
// (terrain, eau, décor, bâtiments, PNJ, points de départ, carte).
// Aucune dépendance Vulkan/GLFW (règle d'architecture : pas d'engine_core).
//
// Repère monde GLOBAL : X = est, Z = sud (le nord est en -Z, convention du
// client), Y = altitude (m). L'origine (0,0) est le centre de la cellule de
// l'Oracle `cell_n000_e000` ; 1 case du lore = 1 cellule de 10 km
// (src/shared/world/WorldCells.h).

#include <cstdint>
#include <string>
#include <vector>

#include <zone_builder/JsonDocument.h>

#include "src/shared/world/WorldCells.h"

namespace tools::world_gen
{
	/// Point 2D monde (plan XZ), mètres.
	struct Vec2
	{
		double x = 0.0;
		double z = 0.0;
	};

	inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.z + b.z }; }
	inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.z - b.z }; }
	inline Vec2 operator*(Vec2 a, double s) { return { a.x * s, a.z * s }; }

	/// Échelle et cadrage du monde (section `scale` du master).
	struct WorldScale
	{
		double cellSizeM = 10000.0;  ///< taille d'une cellule du lore (m) — figée à 10 km
		int    gridCols = 40;        ///< colonnes de grid.rows
		int    westE = -19;          ///< indice E de la colonne la plus à l'ouest
		int    northN = 18;          ///< indice N de la rangée la plus au nord
		double heightScaleM = 1200.0;///< altitude max encodable (r16h normalisé)
		double seaLevelM = 40.0;     ///< niveau de la mer (m)
		int    heightmapRes = 2049;  ///< échantillons par côté d'une cellule (2^k + 1)
	};

	/// Ville / village / camp (section `cities`).
	struct CityDef
	{
		std::string id;
		std::string name;
		std::string cell;        ///< "N003_W009"
		std::string kind;        ///< capitale|grande_ville|ville|forteresse|village|hameau|camp
		std::string race;        ///< humains|elfes|orcs|nains|demons|neutre
		std::string state;       ///< intact|ruine|reconstruction|camp
		std::string faction;
		/// Factions de jeu dont c'est la ville de départ (v1.0.61 : identifiants de
		/// game/data/races/factions.json, ex. "lumiere", "legion" ; avant : races).
		/// Une faction n'a qu'une ville de départ par carte.
		std::vector<std::string> startFor;
		std::string source;      ///< [BD] … / [INFÉRÉ] …
		std::string description;
		// --- résolu par le générateur ---
		int    n = 0, e = 0;     ///< indices de cellule
		Vec2   center;           ///< centre monde (site choisi dans la cellule)
		double radiusM = 20.0;   ///< rayon urbain (m)
		double plateauY = 50.0;  ///< altitude du plateau aplani (m)
		/// Lieu PROCÉDURAL (enrichissement de cellule) : vide pour une ville du
		/// lore, sinon ferme|bucherons|bandits|ruine|mine|hameau.
		std::string procedural;
	};

	/// Point d'intérêt (section `pois`).
	struct PoiDef
	{
		std::string id;
		std::string name;
		std::string cell;
		std::string kind;        ///< monolithe|faille|ruine|portail|foret|etang|champ_bataille|volcan|gouffre|temple|sanctuaire|port|cite|defile|salle
		std::string description;
		/// Portail (kind "portail") : identifiant du portail APPARIÉ dans l'autre carte
		/// (clé `link`, ex. "enfers:poi_enfers_porte_1"). Vide = portail décoratif.
		std::string link;
		int  n = 0, e = 0;
		Vec2 center;
		/// Altitude du sol au centre (m), relevée sur la heightmap de la cellule
		/// pendant la génération (sortie portals.json). 0 tant que non calculée.
		double y = 0.0;
		/// Décalage (m, repère global : X est, Z sud) du lieu depuis le centre de sa
		/// case (clés `offset_x_m`, `offset_z_m`, v1.0.72), ex. un lieu contre la côte
		/// ouest de la case. 0 = centre de la case (règle historique).
		double offsetX = 0.0, offsetZ = 0.0;
	};

	/// Relief local posé à la main (section `reliefs`, v1.0.72), ancré sur le
	/// centre de la case de la ville `city`, où la ville est alors posée (pas de
	/// recherche de site). Ajouté à MacroHeight : fonction pure de la position,
	/// raccord exact entre cellules voisines.
	///  - `colline` : dôme sous une ville perchée — plateau sommital de rayon
	///    `top_radius_m` à `height_m` au-dessus du relief, pente douce jusqu'au pied
	///    (`foot_radius_m`). Les cours d'eau tracés à la main qui partent de la case
	///    naissent au pied (Terrain.cpp, BuildMacroRivers).
	///  - `crevasse` : cul-de-sac — massif de rayon `foot_radius_m` (contour ondulé,
	///    bord extérieur en pente douce) relevé de `height_m`, entaillé d'une fente à
	///    fond plat (demi-largeur `half_width_m`, parois raides de `wall_m`) qui va de
	///    `back_m` derrière la ville jusqu'au-delà du massif, vers la case `open_toward`. Les routes ne franchissent pas les
	///    parois (Roads.cpp) ; `mine` (nom) pose une mine au fond (Places.cpp).
	struct ReliefDef
	{
		std::string id;
		std::string type;          ///< colline|crevasse
		std::string cityId;        ///< ville ancrée (posée au centre de sa case)
		double heightM = 0.0;      ///< hauteur du sommet / des parois au-dessus du relief (m)
		double topRadiusM = 0.0;   ///< colline : rayon du plateau sommital (m)
		double footRadiusM = 0.0;  ///< colline : rayon du pied ; crevasse : rayon du massif (m)
		double halfWidthM = 0.0;   ///< crevasse : demi-largeur du fond de la fente (m)
		double wallM = 0.0;        ///< crevasse : largeur horizontale des parois (m)
		double backM = 0.0;        ///< crevasse : longueur du fond derrière la ville (m)
		std::string openToward;    ///< crevasse : case vers laquelle la fente s'ouvre
		std::string mine;          ///< crevasse : nom de la mine posée au fond (vide = aucune)
		// --- résolu au chargement ---
		int  n = 0, e = 0;         ///< case de la ville
		Vec2 center;               ///< centre de la case (= centre de la ville)
		Vec2 dir{ 1.0, 0.0 };      ///< crevasse : direction unitaire de l'ouverture
	};

	/// Région nommée (rectangle de cellules, section `regions`).
	struct RegionDef
	{
		std::string id;
		std::string name;
		int eMin = 0, eMax = 0, nMin = 0, nMax = 0;
	};

	/// PNJ statique interactif importé ou déclaré (donneur de quête, garde…).
	/// Les champs reprennent le schéma `world.interactables.N.*` du client.
	struct InteractableDef
	{
		std::string cityId;      ///< ancrage : position = centre ville + offset (vide = absolu)
		double x = 0.0, z = 0.0; ///< offset (ou absolu si cityId vide)
		double radius = 1.5;
		bool   npc = true;
		std::string label, role, message, mesh;
		double scale = 1.0, rotXDeg = 0.0, yawDeg = 0.0;
		std::string dialogueId, npcTargetId;
		std::vector<std::string> dialogueLines; ///< repli legacy `dialogue.N`
	};

	/// Lac posé à la main (section `extra_lakes`) : offset depuis le centre de
	/// la ville `city` (ou coordonnées globales absolues si `city` est vide).
	struct ExtraLakeDef
	{
		std::string id;
		std::string cityId;
		double x = 0.0, z = 0.0;
		double radiusM = 20.0;
	};

	/// Cours d'eau tracé à la main (section `river_courses`) : la rivière suit les
	/// centres des cellules listées (8-voisines, à code « rivière » dans la grille ;
	/// la dernière peut être la case d'embouchure, côte ou mer), puis descend vers
	/// la mer. Elle remplace la rivière automatique de la composante de cases
	/// qu'elle traverse. `island_city` : la rivière passe par le centre de cette
	/// ville et se divise en deux bras qui l'entourent (île fluviale, ex. Sysigie).
	struct RiverCourseDef
	{
		std::string id;
		std::string name;                 ///< nom affiché (provisoire « [à nommer] » admis)
		std::vector<std::string> cells;   ///< cases traversées, de la source vers l'aval
		std::vector<std::pair<int, int>> cellNE; ///< (n, e) résolus
		double widthM = 32.0;             ///< largeur à l'embouchure (m), 40 % de moins à la source
		std::string islandCity;           ///< ville entourée par deux bras (vide = aucune)
	};

	/// Contenu complet du master après chargement + validation.
	struct WorldMaster
	{
		std::string worldId;
		/// Carte des cellules du monde (WorldCells::CellMap) : dossiers `cell_*` et
		/// zone ids 1 000 000+ pour « royaumes », `enfers_*` et 2 000 000+ pour
		/// « enfers ». Déduite de world_id (identifiant inconnu → Royaumes).
		engine::world::cells::CellMap cellMap = engine::world::cells::CellMap::Royaumes;
		std::string displayName;
		uint64_t    seed = 1;
		WorldScale  scale;
		std::vector<std::string> gridRows;      ///< rangées N→S, `gridCols` caractères
		std::vector<std::pair<char, std::string>> legend;
		std::vector<RegionDef> regions;
		std::vector<CityDef> cities;
		std::vector<PoiDef> pois;
		std::vector<InteractableDef> interactables;
		std::vector<ExtraLakeDef> extraLakes;
		std::vector<RiverCourseDef> riverCourses;
		std::vector<ReliefDef> reliefs;         ///< reliefs locaux (v1.0.72)
		/// Réglages fusionnés PAR-DESSUS zone.json (ex. distances de brouillard).
		tools::zone_builder::JsonValue zoneOverrides;
		// Imports du contenu existant (chemins relatifs au content root).
		std::string importZoneConfig;     ///< zone.json dont on recopie les réglages world.*
		std::string importScenery;        ///< scenery.json (world.scenery.N) → scatter
		std::string importBuildings;      ///< buildings.bin (LCBD) recopié tel quel
		std::string importRespawn;        ///< respawn_points.txt recopié (zone id corrigé)
		std::string importSpawners;       ///< spawners.json historique (positions locales)
		std::string importQuestPoi;       ///< POI de quêtes historiques (positions locales)
		std::string importAnchorCity;     ///< ville d'ancrage : tout l'import est translaté sur son centre
		/// Cellules générées par défaut (identifiants du lore « N003_W009 »).
		std::vector<std::string> generatedCells;
		/// Relier les villes de départ : les cellules traversées par les routes qui
		/// les connectent (arbre couvrant de plus courts chemins sur le réseau routier)
		/// sont ajoutées aux cellules générées — on peut aller à pied d'un royaume à
		/// l'autre. Clé `connect_start_cities` (défaut false).
		bool connectStartCities = false;
		/// Marge (m) autour de la chaussée : la cellule voisine est ajoutée quand la
		/// route passe à moins de cette distance de sa frontière. `corridor_margin_m`.
		double corridorMarginM = 300.0;
	};

	/// Bilan d'une génération (journalisé par la CLI, vérifié par les tests).
	struct GenReport
	{
		uint64_t scatterInstances = 0;
		uint64_t scatterMeshes = 0;
		uint64_t buildings = 0;
		uint64_t roads = 0;
		double   roadLengthM = 0.0;
		uint64_t rivers = 0;
		uint64_t lakes = 0;
		uint64_t spawners = 0;
		uint64_t ambientNpcs = 0;
		uint64_t creatures = 0;
		uint64_t respawnPoints = 0;
		uint64_t interactables = 0;
		uint64_t houseTemplates = 0;
		uint64_t cells = 0;
		uint64_t proceduralPlaces = 0;
		uint64_t seaPocketSamples = 0; ///< EAU.5 : échantillons de cuvettes sous la mer comblés
		uint64_t portals = 0;          ///< portails reliés à une autre carte (portals.json)
		std::vector<std::string> warnings;
		std::vector<std::string> writtenFiles;
	};
}
