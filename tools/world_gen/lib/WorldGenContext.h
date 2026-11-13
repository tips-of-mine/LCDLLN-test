#pragma once

// tools/world_gen — contextes de génération.
//
// Deux niveaux (monde découpé en cellules de 10 km, src/shared/world/WorldCells.h) :
//   - GlobalWorld : couche MACRO calculée une fois par exécution sur tout le
//     continent (relief de base, sites des villes, rivières, lacs, routes,
//     points de départ). Tout ce qui traverse une frontière de cellule vit ici,
//     ce qui garantit des cellules voisines raccord au mètre près.
//   - World : contexte d'UNE cellule (heightmap 2049², bâti, décor, PNJ),
//     construit à partir de GlobalWorld puis écrit sur disque.
// Ordre des étapes : RunWorldGen (WorldGen.cpp).

#include "WorldGenTypes.h"
#include "Random.h"

#include <zone_builder/JsonDocument.h>

#include "src/shared/world/WorldCells.h"
#include "src/shared/world/MapFeatures.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace tools::world_gen
{
	/// Grille scalaire régulière couvrant le terrain (échantillons aux NOEUDS :
	/// l'échantillon (i,j) est à (originX + i*step, originZ + j*step)).
	template <typename T>
	struct Grid
	{
		int width = 0;
		int height = 0;
		double originX = 0.0;
		double originZ = 0.0;
		double step = 1.0;
		std::vector<T> data;

		void Init(int w, int h, double ox, double oz, double s, T fill)
		{
			width = w; height = h; originX = ox; originZ = oz; step = s;
			data.assign(static_cast<size_t>(w) * static_cast<size_t>(h), fill);
		}
		T& At(int i, int j) { return data[static_cast<size_t>(j) * width + i]; }
		const T& At(int i, int j) const { return data[static_cast<size_t>(j) * width + i]; }
		bool InBounds(int i, int j) const { return i >= 0 && j >= 0 && i < width && j < height; }
		double WorldX(int i) const { return originX + i * step; }
		double WorldZ(int j) const { return originZ + j * step; }
		/// Indice d'échantillon le plus proche (non borné).
		int IndexX(double x) const { return static_cast<int>(std::lround((x - originX) / step)); }
		int IndexZ(double z) const { return static_cast<int>(std::lround((z - originZ) / step)); }
	};

	/// Couverture végétale EFFECTIVE d'un point : le code du lore, nuancé à
	/// l'échelle du kilomètre — bois et bosquets dans les plaines, clairières et
	/// prairies dans les forêts — pour casser l'uniformité d'une case de 10 km.
	/// Partagé par la végétation et le splat (cohérence sol / arbres).
	inline char EffectiveCover(char code, double x, double z, uint64_t seed)
	{
		if (code == '.' || code == 'r' || code == 'p')
			return Fbm(x, z, 3500.0, 2, seed + 77) > 0.22 ? 't' : code;
		if (code == 'T' || code == 'R' || code == 'L')
			return Fbm(x, z, 2600.0, 2, seed + 78) < -0.45 ? '.' : code; // clairières (FORET.1 : moitié moins)
		return code;
	}

	/// Échantillonnage bilinéaire borné d'une grille de doubles.
	double SampleBilinear(const Grid<double>& g, double x, double z);

	/// Tronçon de route (polyligne monde) entre deux agglomérations.
	struct Road
	{
		std::string fromId, toId;
		std::vector<Vec2> points;     ///< polyligne lissée, pas ~4 m
		std::vector<double> profileY; ///< altitude lissée de la chaussée par point
		double widthM = 5.0;          ///< largeur de chaussée
		bool   major = false;         ///< grande route (entre villes importantes)
		bool   path = false;          ///< chemin local (lieu procédural → réseau) ou sentier du lore
		/// Route du lore tracée (v1.0.105) : identifiant de la route du lore (`lore_roads`
		/// du master) dont ce tronçon est issu ; vide = route du générateur ou chemin local.
		/// fromId / toId : ville ou POI du master, vide pour une jonction ou une case seule.
		std::string loreId;
	};

	/// Rivière (polyligne descendante jusqu'à la mer).
	struct River
	{
		std::string name;
		std::vector<Vec2> points;
		std::vector<double> waterY;   ///< altitude de la surface par point (décroissante)
		std::vector<double> widthM;
		size_t globalStart = 0;       ///< index du 1er point dans la rivière globale (tronçon de cellule)
	};

	/// Lac (disque déformé) à surface plate.
	struct Lake
	{
		std::string name;
		Vec2   center;
		double radiusM = 20.0;
		double waterY = 0.0;
		std::vector<Vec2> polygon;    ///< CCW dans XZ
	};

	/// Un bâtiment posé : référence vers un modèle de la bibliothèque.
	struct BuildingPlacementDef
	{
		std::string templateType;     ///< fichier buildings/templates/<type>.json
		std::string variantId;
		std::string displayName;
		Vec2   position;
		double y = 0.0;               ///< offset vertical (0 = posé au sol)
		double yawDeg = 0.0;
		double footprintRadius = 5.0; ///< rayon d'encombrement (occupation)
		/// FIX.12 — emprise rectangulaire (m, repère local du bâtiment) pour la
		/// carte du monde ; 0 = inconnue (la carte dérive alors un carré du rayon).
		double halfW = 0.0;
		double halfD = 0.0;
		std::string cityId;
		Vec2   doorPoint;             ///< point de rue devant la porte (monde)
		bool   isInn = false;
	};

	/// Instance de décor à écrire dans scatter.bin (chemin de mesh en clair ;
	/// la table d'indices est construite à l'écriture, triée).
	struct ScatterDef
	{
		std::string mesh;
		Vec2   position;
		double yOffset = 0.0;
		double yawDeg = 0.0;
		double scale = 1.0;
		double collisionRadius = 0.0;
		double tiltDeg = 0.0;
		uint8_t flags = 0;
		uint8_t cullClass = 0;
	};

	/// Lumière ponctuelle posée par le générateur (v1.0.107 : torches du gouffre).
	/// DONNÉE seulement : écrite dans zone.json (`world.point_lights`), pas encore lue
	/// par le client (DynamicLightSystem ne lit qu'un fichier global,
	/// lights/dynamic_lights.json) ; le mesh de la torche, lui, est un vrai décor
	/// (scatter.bin).
	struct PointLightDef
	{
		std::string id;
		std::string type = "torch";   ///< type DynamicLightType du client (torch)
		Vec2   position;              ///< position GLOBALE (m)
		double y = 0.0;               ///< altitude de la flamme (m)
		double radiusM = 7.0;         ///< portée (m)
		double intensity = 1.6;
		double r = 1.0, g = 0.55, b = 0.18; ///< couleur (flamme orangée)
	};

	/// Zone d'ambiance locale (v1.0.107 : « sombre » au fond du gouffre). DONNÉE
	/// seulement (zone.json, `world.ambiance_zones`) : le client n'a pas de réglage
	/// d'ambiance par zone ; à brancher (assombrissement, brouillard bas) côté rendu.
	struct AmbianceZoneDef
	{
		std::string id;               ///< identifiant du relief (gouffre)
		std::string label;            ///< nom du lieu
		Vec2   center;                ///< centre GLOBAL (m)
		double radiusM = 0.0;         ///< rayon horizontal couvert (m)
		double floorY = 0.0;          ///< altitude du fond (m)
		double topY = 0.0;            ///< altitude au-dessus de laquelle la zone cesse (m)
		double darkness = 0.0;        ///< 0 = lumière du jour, 1 = nuit noire
	};

	/// Noeud du graphe de rues d'une agglomération (déplacements des PNJ).
	struct StreetNode
	{
		Vec2 p;
		std::vector<int> links;
	};

	/// Résultat de mise en page d'une agglomération.
	struct SettlementLayout
	{
		std::string cityId;
		std::vector<StreetNode> nodes;      ///< graphe de rues (nodes[0] = place)
		std::vector<int> plazaRing;         ///< boucle de ronde autour de la place
		std::vector<Vec2> stallFronts;      ///< emplacements de marchands
		std::vector<Vec2> gatePoints;       ///< portes (sorties de route)
		std::vector<int> buildingIndices;   ///< indices dans World::buildings
		Vec2 innDoor;                       ///< porte de l'auberge (ou place)
		Vec2 graveyard;                     ///< cimetière (point de résurrection)
		Vec2 startSpot;                     ///< point de départ joueur (si ville de départ)
		double startYawDeg = 0.0;
	};

	/// Spawner serveur (format zones/<zone>/spawners.json + extension routine).
	struct SpawnerDef
	{
		std::string id;
		uint32_t archetypeId = 0;
		Vec2   position;
		double y = 0.0;
		uint32_t count = 1;
		uint32_t respawnSec = 60;
		double leashM = 24.0;
		// --- routine (PNJ d'ambiance) ; kind vide = IA de créature historique ---
		std::string routineKind;            ///< tour|patrol|post|wander
		std::vector<Vec2> path;
		double wanderRadiusM = 0.0;
		double pauseMinSec = 0.0, pauseMaxSec = 0.0;
		std::string schedule;               ///< always|day|night
		double pauseChance = 0.35;          ///< probabilité de pause à chaque point
		bool pingPong = false;
		bool hasFacePoint = false;          ///< poste : point regardé à l'arrêt
		Vec2 facePoint;
		// --- ETAPE4.4 : agenda de métier (prioritaire sur la routine côté shard) ---
		std::string agendaProfession;       ///< fermier|artisan|marchand|aubergiste|pretre|flaneur ; vide = pas d'agenda
		std::map<std::string, std::vector<Vec2>> agendaPlaces; ///< home|work|tavern|market|square|temple → points
	};

	/// Point de respawn (cimetière / auberge).
	struct RespawnDef
	{
		std::string type;                   ///< graveyard|inn
		Vec2 position;
		double y = 0.0;
		std::string faction;                ///< "-" = neutre
	};

	/// Point de départ d'une faction de jeu (consommé par le master à la création,
	/// v1.0.61) ; la race est celle de la ville (repli par race de start_locations.json).
	struct StartLocationDef
	{
		std::string faction;   ///< faction de jeu (entrée de CityDef::startFor)
		std::string race;      ///< race de la ville de départ (CityDef::race)
		std::string cityId;
		Vec2 position;
		double y = 0.0;
		double yawDeg = 0.0;
	};

	/// Occupation fine (1 m) CREUSE : seules les tuiles de 64 m touchées sont
	/// allouées (une cellule de 10 km pleine pèserait 100 Mo).
	class SparseOccupancy
	{
	public:
		static constexpr int kTile = 64;
		void Set(int x, int z);
		bool Get(int x, int z) const;
		void Clear() { m_tiles.clear(); }
	private:
		static int64_t Key(int tx, int tz) { return (static_cast<int64_t>(tz) << 32) ^ static_cast<uint32_t>(tx); }
		std::unordered_map<int64_t, std::vector<uint8_t>> m_tiles;
	};

	// =========================================================================
	// Couche MACRO (tout le continent)
	// =========================================================================

	/// Relief de base (avant villes, eau et routes) : fonction PURE du master et
	/// de la position globale. Comprend les reliefs locaux du master (ReliefOffset :
	/// colline, crevasse v1.0.72, massif v1.0.77) et, appliqués en dernier, les
	/// gouffres (v1.0.107, jamais de relèvement). Aux Enfers, la palette (v1.0.83)
	/// ne change pas l'altitude. \return altitude (m) en (x, z) globaux. Terrain.cpp
	double MacroHeight(const WorldMaster& m, double x, double z);
	/// Code de biome du lore sous (x, z), bords bruités. Terrain.cpp
	char MacroBiome(const WorldMaster& m, double x, double z);
	/// Code du lore de la case (n, e) (' ' = mer hors grille). Terrain.cpp
	char LoreCode(const WorldMaster& m, int n, int e);
	/// v1.0.83 : la carte du master est-elle les Enfers ? Le générateur y applique
	/// la palette infernale (splat sans herbe ni neige, pas de broussailles ni de
	/// conifères, carte du monde en roche rouge, basalte, cendre et lave) ; les
	/// Royaumes gardent la palette historique, octet pour octet.
	inline bool IsInfernalMap(const WorldMaster& m) { return m.cellMap == engine::world::cells::CellMap::Enfers; }
	/// v1.0.83 : présence de lave en (x, z), dans [0, 1] (0 hors des Enfers) :
	/// mers de lave autour des lieux `volcan` (≈ 1,7 km de rayon, bord bruité) et
	/// fleuves de lave sinueux (≈ 80-120 m de large) dans le biome volcanique `V`,
	/// écartés des villes et des lieux (≈ 0,9 km). Fonction PURE du master et de la
	/// position (raccord exact entre cellules). Pas de matériau émissif côté client :
	/// la lave n'est qu'une couleur (carte du monde) et un sol de terre (splat). Terrain.cpp
	double InfernalLavaAt(const WorldMaster& m, double x, double z);
	/// Élévation (m) apportée en (x, z) par UN relief local (`reliefs` du master,
	/// v1.0.72) : dôme d'une colline, massif entaillé d'une crevasse ; massif de
	/// crêtes sans ville (v1.0.77), avant le masque des terres émergées. Terrain.cpp
	double ReliefContribution(const ReliefDef& r, double x, double z);
	/// Crevasse : largeur (m) de la pente douce extérieure du massif, de part et
	/// d'autre de `foot_radius_m` (au moins `wall_m`, 40 % du rayon). Terrain.cpp
	double CrevasseOuterSlopeM(const ReliefDef& r);
	/// Somme des reliefs locaux du master en (x, z) ; incluse dans MacroHeight.
	/// \param baseH altitude (m) du relief sans les reliefs locaux : les massifs
	///        (v1.0.77) ne s'appliquent que là où elle dépasse la mer (côte inchangée). Terrain.cpp
	double ReliefOffset(const WorldMaster& m, double x, double z, double baseH);
	/// Gouffre (v1.0.107) : altitude (m) en (x, z) une fois la fosse et sa galerie
	/// creusées dans le relief \p h (fond plat à r.floorY, sol de la galerie en pente
	/// régulière de r.floorY, à sa sortie du fond, jusqu'à r.entranceY). Ne relève jamais : rend au plus \p h ;
	/// hors de la boîte du gouffre, rend \p h exactement. Fonction pure. Terrain.cpp
	double GouffreHeight(const ReliefDef& r, double x, double z, double h);
	/// Gouffre : rayon (m) du fond plat dans la direction \p angleRad (atan2(dz, dx)),
	/// contour lobé de ± 13 % autour de floor_radius_m. Terrain.cpp
	double GouffreFloorRadiusAt(const ReliefDef& r, double angleRad);
	/// Gouffre : distance (m) de \p p à l'axe de la galerie ; \p outAlong reçoit
	/// l'abscisse curviligne (m, depuis le fond) du point le plus proche. Terrain.cpp
	double GouffreGalleryDistance(const ReliefDef& r, Vec2 p, double* outAlong);
	/// Vrai si \p p est dans le gouffre \p r (fond et parois) ou dans sa galerie
	/// (sol et parois), élargis de \p marginM. Faux pour un autre type de relief. Terrain.cpp
	bool InGouffre(const ReliefDef& r, Vec2 p, double marginM);
	/// InGouffre pour tous les gouffres du master. Terrain.cpp
	bool InAnyGouffre(const WorldMaster& m, Vec2 p, double marginM);
	/// Relief local ancré sur la ville \p cityId (nullptr si aucun). Terrain.cpp
	const ReliefDef* ReliefOfCity(const WorldMaster& m, const std::string& cityId);

	struct GlobalWorld
	{
		WorldMaster master;
		std::string contentRoot;
		std::string outputRoot;

		/// Emprise de la grille du lore (m, repère global).
		double minX = 0.0, minZ = 0.0, maxX = 0.0, maxZ = 0.0;
		/// Grille grossière (250 m) du relief de base sur tout le continent.
		Grid<double> coarse;

		std::vector<CityDef> cities;    ///< villes du lore, sites résolus
		std::vector<PoiDef> pois;       ///< lieux remarquables, centres résolus
		std::vector<River> rivers;      ///< polylignes globales (pas 6 m)
		std::vector<Lake> lakes;        ///< lacs du lore + lacs manuels
		/// Réseau routier final (profil en long global) : routes du lore tracées
		/// (v1.0.105, Road::loreId non vide) + routes du générateur gardées pour relier
		/// les villes de départ ; sans `lore_roads` au master, le seul réseau du générateur.
		std::vector<Road> roads;
		/// Réseau calculé par le générateur seul (Kruskal + voisines, comportement
		/// historique), avant le tri v1.0.105. Sert aux corridors des villes de départ
		/// (AddStartCorridors) : les cellules jouables ne dépendent pas des routes du lore.
		/// Vide quand le master n'a pas de `lore_roads` (g.roads est alors ce réseau).
		std::vector<Road> generatorRoads;
		std::vector<Vec2> startCenters; ///< centres des villes de départ

		GenReport report;

		const CityDef* FindCity(const std::string& id) const;
		void Warn(const std::string& msg) { report.warnings.push_back(msg); }
	};

	// =========================================================================
	// Contexte d'UNE cellule
	// =========================================================================

	struct World
	{
		const GlobalWorld* g = nullptr;
		WorldMaster master;        ///< copie du master (seed, échelle, imports)
		std::string contentRoot;   ///< lecture (master, imports, existence des meshes)
		std::string outputRoot;    ///< écriture des sorties (= contentRoot en usage normal)

		engine::world::cells::CellCoord cell;
		std::string cellName;      ///< "cell_n003_w009"
		uint32_t zoneId = 0;       ///< identifiant numérique serveur de la cellule

		// Cadrage du terrain (coin min de la cellule, côté 10 km).
		double terrainOriginX = 0.0;
		double terrainOriginZ = 0.0;
		double terrainSizeM = 10000.0;

		// Champs à la résolution de la heightmap (≈ 4,9 m pour 2049²).
		Grid<double> height;        ///< altitude finale (m)
		Grid<char>   biome;         ///< code de cellule du lore (bords bruités)
		Grid<float>  roadDist;      ///< distance à la chaussée la plus proche (m)
		Grid<float>  riverDist;     ///< distance au lit de rivière le plus proche (m)
		Grid<uint8_t> urban;        ///< 1 = emprise urbaine, 2 = place/rue
		/// FORET.1 — densité forestière (0..255) de la cellule, kForestMaskResolution²
		/// texels sur terrainSizeM (BuildForestMask) ; écrite dans terrain_forest.frms.
		std::vector<uint8_t> forestMask;
		int forestMaskRes = 0;
		SparseOccupancy occupied;   ///< occupation fine (1 m) pour bâtiments/props

		std::vector<CityDef> cities;  ///< agglomérations DE la cellule (lore + procédurales)
		std::vector<PoiDef> pois;     ///< lieux remarquables de la cellule
		std::vector<Road> roads;      ///< tronçons globaux découpés + chemins locaux
		std::vector<River> rivers;    ///< tronçons de rivières traversant la cellule
		std::vector<Lake> lakes;      ///< lacs recoupant la cellule
		std::vector<BuildingPlacementDef> buildings;
		std::vector<ScatterDef> scatter;
		std::vector<SettlementLayout> layouts;
		std::vector<SpawnerDef> spawners;
		std::vector<RespawnDef> respawns;
		std::vector<StartLocationDef> starts;
		std::vector<InteractableDef> interactables; ///< résolus (positions absolues)
		std::vector<PointLightDef> pointLights;     ///< lumières ponctuelles (v1.0.107, données)
		std::vector<AmbianceZoneDef> ambianceZones; ///< zones d'ambiance locales (v1.0.107, données)
		bool hasSea = false;          ///< une partie de la cellule est sous la mer

		/// Réglages `world.*` importés (brouillard, props, DOF…) recopiés dans zone.json.
		tools::zone_builder::JsonValue importedWorldConfig;
		/// Lignes de respawn importées (déjà au format serveur, zone id corrigé).
		std::vector<std::string> importedRespawnLines;

		GenReport report;

		/// Cellule du lore contenant le point ; false hors grille du master.
		bool CellAt(Vec2 p, int& n, int& e) const;
		/// Altitude terrain bilinéaire courante.
		double HeightAt(Vec2 p) const { return SampleBilinear(height, p.x, p.z); }
		/// Pente locale en degrés (différences finies sur la heightmap).
		double SlopeDegAt(Vec2 p) const;
		/// Le point est-il sous l'eau (mer, lac, lit de rivière) ?
		bool IsWater(Vec2 p) const;
		/// Le point est-il dans la cellule, à \p marginM du bord au moins ?
		bool InPlayableBounds(Vec2 p, double marginM = 64.0) const;
		/// Vrai si le point appartient à CETTE cellule (bord min inclus, max exclu) :
		/// un objet n'est écrit que par une seule cellule.
		bool OwnsPoint(Vec2 p) const;

		/// Marque un disque comme occupé dans la grille fine d'occupation.
		void Occupy(Vec2 c, double radius);
		/// Vrai si le disque recouvre une zone déjà occupée.
		bool IsOccupied(Vec2 c, double radius) const;
		/// Rectangle orienté (demi-dimensions hx en X local, hz en Z local, yaw en
		/// degrés, convention RotateY du moteur) : marquage / test d'occupation.
		void OccupyRect(Vec2 c, double hx, double hz, double yawDeg);
		bool IsOccupiedRect(Vec2 c, double hx, double hz, double yawDeg) const;

		const CityDef* FindCity(const std::string& id) const;
		void Warn(const std::string& msg) { report.warnings.push_back(cellName + " : " + msg); }
	};

	// ---- Étapes du pipeline (une par fichier .cpp) ----------------------------
	/// Charge et valide le master (+ résolution des cellules). WorldMaster.cpp
	/// Clés ajoutées sur la branche carte v1.0.72-v1.0.107 : `reliefs` (colline,
	/// crevasse, massif, gouffre avec altitudes absolues précalculées), `position`
	/// manuelle des villes et POI (≤ 4300 m par axe, refusée pour une ville de relief
	/// sauf gouffre), `offset_x_m` / `offset_z_m` des POI, `secret` des villes (interdit
	/// pour une ville de départ) et des POI (v1.0.110, interdit pour un portail relié),
	/// `lore_roads` (extrémité secrète refusée) (validation stricte : id unique, type
	/// road | imperial_road | path, extrémité connue ou vide, cases dans la grille).
	/// \return false + \p err au premier défaut (aucune sortie écrite).
	bool LoadWorldMaster(const std::string& contentRoot, const std::string& worldId,
		WorldMaster& out, std::string& err);

	// Couche macro (une fois).
	/// Grille grossière, sites des villes/POI, lacs. Terrain.cpp
	/// Une ville ou un POI à position manuelle (`position` du master, v1.0.95) est
	/// posé exactement à cette position ; les autres gardent la règle historique.
	void BuildMacroTerrain(GlobalWorld& g);
	/// Rivières (descente sur la grille grossière remplie). Terrain.cpp
	void BuildMacroRivers(GlobalWorld& g);
	/// Réseau routier inter-villes + profils en long. Roads.cpp
	/// v1.0.105 : avec `lore_roads` au master, les routes du lore sont tracées case
	/// par case (g.roads) ; le réseau du générateur (g.generatorRoads) ne garde que
	/// les routes des corridors de départ sans équivalent au lore.
	void BuildMacroRoads(GlobalWorld& g);

	// Par cellule.
	/// Initialise le contexte d'une cellule (cadrage, éléments globaux recoupant
	/// la cellule). WorldGen.cpp
	void InitCell(World& w, const GlobalWorld& g, engine::world::cells::CellCoord c);
	/// Lieux procéduraux (hameaux, fermes, camps, ruines, mines). Places.cpp
	/// Mine posée d'abord au fond d'une crevasse (v1.0.72) ; aucun lieu dans un
	/// gouffre ni sa galerie (v1.0.107) ; une ville secrète n'influence le peuple des
	/// lieux que dans sa propre cellule ; mines de la Légion (race `demons`) aux Enfers.
	/// Effet de bord : remplit les lieux de \p w.
	void BuildPlaces(World& w);
	/// Altitude de base + biome de la cellule, puis comblement des cuvettes
	/// intérieures sous le niveau de la mer (EAU.5). Terrain.cpp
	void BuildCellTerrain(World& w);
	/// EAU.5 — Comble les cuvettes isolées sous la mer de la cellule
	/// (FillLandlockedSea, relevées à mer + 0,5 m) puis recalcule hasSea.
	/// Appelée après le relief de base, puis après les routes (déblais). Terrain.cpp
	void FillSeaPockets(World& w);
	/// EAU.5 — Comble les cuvettes sous \p seaLevel qui ne communiquent pas avec
	/// la mer : la nappe de la mer couvre toute la cellule, une cuvette isolée
	/// y faisait apparaître une « mare de mer » (eau de mer, écume) en pleine
	/// terre. Une cuvette communique avec la mer si elle touche, par des
	/// échantillons sous la mer (voisinage 4), un échantillon de biome marin
	/// (' ' ou '-') ou le bord de la grille (prudence : la mer peut être dans
	/// la cellule voisine). Les autres sont relevées à \p fillY.
	/// \param biome même cadrage que \p height ; vide = seul le bord compte.
	/// \return nombre d'échantillons relevés.
	size_t FillLandlockedSea(Grid<double>& height, const Grid<char>& biome, double seaLevel, double fillY);
	/// EAU.5 — Rayon (m) du polygone du lac dans la direction \p angleRad
	/// (atan2(dz, dx)) : interpolation entre les sommets, posés à angles
	/// réguliers par BuildMacroTerrain. Sans polygone : radiusM.
	double LakeRadiusAt(const Lake& lake, double angleRad);
	/// EAU.5 — Niveau d'eau d'un lac (m) : moyenne du relief − 0,8 m (ancienne
	/// règle), mais jamais plus haut que le BAS de sa rive (15e centile du relief
	/// de base, 10 m au-delà du polygone) − 0,3 m : un lac à flanc de pente n'est
	/// plus « perché » (il aurait fallu une digue de dizaines de mètres). Au moins
	/// 0,5 m au-dessus de la mer. Fonction pure (raccord exact entre cellules).
	/// \param meanHeight moyenne du relief de base sur le disque du lac.
	double LakeWaterLevel(const WorldMaster& m, const Lake& lake, double meanHeight);
	/// EAU.5 — Creuse la cuvette du lac et façonne ses rives d'après le
	/// POLYGONE (et non plus un cercle) : le fond est sous l'eau dans le
	/// polygone, sauf une bande de 6 m où il remonte jusqu'à 0,3 m au-dessus de
	/// l'eau au ras du bord (le bord de la nappe reste enfoui sous la rive). Un
	/// relief plus bas (lac à flanc de pente) est relevé en digue naturelle
	/// (crête de 8 m, puis pente ≈ 19°) : la nappe ne déborde plus en « plaque » au-dessus du vide.
	/// \param riverDist distance aux lits de rivière (m) ; la digue n'est pas
	///        élevée à moins de 12 m d'un lit (une rivière qui sort du lac
	///        resterait sinon barrée). Grille vide = aucune rivière.
	/// \param floorY fond minimal (m) : niveau de la mer + 0,1 (la nappe de la
	///        mer d'une cellule côtière apparaîtrait sinon sous l'eau du lac).
	/// \param urban emprise urbaine (≠ 0 : plateau jamais retouché hors du lac).
	///        Grille vide = aucune.
	void CarveLake(Grid<double>& height, const Grid<float>& riverDist, const Grid<uint8_t>& urban, const Lake& lake, double floorY);
	/// Plateaux urbains. Terrain.cpp
	void FlattenSettlements(World& w);
	/// Rivières et lacs (creusement, carte de distance). Terrain.cpp
	void CarveWater(World& w);
	/// Routes globales découpées + chemins locaux, aplanissement. Roads.cpp
	void ApplyRoads(World& w);
	/// Rues, lots, bâtiments, remparts, mobilier, graphe PNJ. Settlements.cpp
	/// Champs clôturés hors les murs, état `reconstruction` ; v1.0.107 : torches,
	/// éboulis, lumières (w.pointLights) et zone sombre (w.ambianceZones) des gouffres.
	/// Effet de bord : w.scatter, bâtiments, occupation fine de la cellule.
	void BuildSettlements(World& w);
	/// Arbres, sous-bois, rochers, herbes. Vegetation.cpp
	/// Aux Enfers (v1.0.83) : seulement arbres morts et rochers (ni herbe, ni fleur,
	/// ni broussaille). Effet de bord : remplit w.scatter.
	void BuildVegetation(World& w);
	/// FORET.1 — carte de densité des forêts et lisières (codes T, R, L, t de la
	/// couverture effective), exclusions comprises : eau, routes, bâti et champs
	/// (grille d'occupation), emprise urbaine, pentes, neige. Le client y pose les
	/// arbres de remplissage (src/client/world/instances/ForestMask.h).
	/// Effet de bord : remplit w.forestMask. À appeler après BuildSettlements et
	/// avant BuildVegetation (les rochers posés ensuite n'y creusent pas de trous).
	/// Aux Enfers (v1.0.83) : carte vide (aucune forêt de remplissage).
	void BuildForestMask(World& w);
	/// Vrai si un code de couverture effective est une forêt dont les arbres sont
	/// posés par le client (carte de densité) au lieu de scatter.bin.
	bool IsFillerForestCode(char code);
	/// Spawners PNJ/créatures, respawns, départs, interactibles. Population.cpp
	/// Un cimetière neutre et une auberge par ville (secrète comprise) dans
	/// respawn_points.txt ; ni faune ni monstre dans un gouffre et sa galerie (v1.0.107).
	/// Effet de bord : remplit les spawners et points de réapparition de \p w.
	void BuildPopulation(World& w);
	/// Import du contenu historique (si la ville d'ancrage est dans la cellule). Population.cpp
	bool ImportLegacyContent(World& w, std::string& err);
	/// SND.4 — côté de la grille de biomes exportée dans zone.json (40 → blocs de 250 m).
	inline constexpr int kBiomeGridSize = 40;
	/// SND.4 — codes de biome du lore (grid.legend) échantillonnés au centre de
	/// chaque bloc de la cellule ; ligne 0 = Z minimal. À appeler avant la
	/// libération de w.biome. Output.cpp
	std::string BuildBiomeGrid(const World& w, int size);
	/// Écriture des sorties d'une cellule. Output.cpp
	/// Effet de bord : écrit zones/<c>/ et instances/zone_<c>/ (sauf dryRun) ; v1.0.107 :
	/// `world.point_lights` et `world.ambiance_zones` du zone.json pour un gouffre
	/// (clés absentes ailleurs ; pas encore lues par le client).
	/// \return false + \p err si une écriture échoue.
	bool WriteCellOutputs(World& w, std::string& err);
	/// Écriture des sorties globales (lieux, carte, départs, POI de quêtes). Output.cpp
	/// v1.0.83 : palette infernale de la carte des Enfers ; v1.0.107 : une ville
	/// secrète est absente de `cities` de places.json, de la carte du continent et de
	/// map_features.bin ; v1.0.110 : villes ET lieux secrets écrits dans
	/// `secret_places.json` (réservé au serveur, retiré du paquet client), plus de
	/// `secret_cities` dans places.json ; le fichier est supprimé s'il n'y a aucun
	/// secret. Effet de bord : écrit world/<monde>/ (sauf dryRun).
	/// \return false + \p err si une écriture échoue.
	bool WriteGlobalOutputs(GlobalWorld& g, const std::vector<World*>& cells, std::string& err);
	/// FIX.12 — tracés de la carte du monde (world/<w>/map_features.bin) : routes
	/// inter-villes (g.roads), chemins locaux des cellules (Road::path), rues des
	/// agglomérations (arêtes du graphe SettlementLayout) et emprises des
	/// bâtiments. Polylignes allégées (pas ≥ 10 m). Pur (aucune écriture). Output.cpp
	engine::world::mapfeatures::MapFeatures BuildMapFeatures(const GlobalWorld& g, const std::vector<World*>& cells);
	/// Modèles de maisons paramétriques (bibliothèque buildings/templates). Output.cpp
	bool WriteHouseTemplates(const std::string& outputRoot, GenReport& report, std::string& err);

	/// Ajoute à \p ids les cellules des routes reliant les villes de départ
	/// (`connect_start_cities`, cf. WorldMaster). Nécessite g.cities (sites) et
	/// le réseau du générateur g.generatorRoads (BuildMacroRoads ; g.roads s'il est
	/// vide, ex. tests qui posent leurs routes à la main). WorldGen.cpp
	void AddStartCorridors(GlobalWorld& g, std::vector<std::string>& ids);
	/// Routes de \p roads qui relient les villes de départ (arbre couvrant « au plus
	/// près » par la route, chemins locaux exclus), dans l'ordre de parcours.
	/// \param warn  signale (g.Warn) les villes de départ injoignables.
	/// Déterministe ; ne modifie g que par les avertissements. WorldGen.cpp
	std::vector<size_t> StartCorridorRoads(GlobalWorld& g, const std::vector<Road>& roads, bool warn);
	/// Cellules (identifiants du lore) traversées par les routes \p chosen de
	/// \p roads, + la voisine quand la route passe à moins de corridor_margin_m de
	/// la frontière ; ajoutées à \p ids (sans doublon, ordre de parcours).
	/// \return nombre de cellules ajoutées. WorldGen.cpp
	size_t AddCorridorCells(const GlobalWorld& g, const std::vector<Road>& roads,
		const std::vector<size_t>& chosen, std::vector<std::string>& ids);

	/// Décode un identifiant de cellule "N003_W009" → (n, e). WorldMaster.cpp
	bool ParseCellId(const std::string& id, int& n, int& e);

	/// Distance d'un point à un segment (plan XZ).
	double DistPointSegment(Vec2 p, Vec2 a, Vec2 b, double* outT = nullptr);
	/// Lissage de Chaikin / rééchantillonnage d'une polyligne. Terrain.cpp
	std::vector<Vec2> SmoothPolyline(const std::vector<Vec2>& in, int iterations);
	std::vector<Vec2> ResamplePolyline(const std::vector<Vec2>& in, double stepM);
}
