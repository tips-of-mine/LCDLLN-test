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
		if (code == '.' || code == 'r')
			return Fbm(x, z, 3500.0, 2, seed + 77) > 0.22 ? 't' : code;
		if (code == 'T' || code == 'R' || code == 'L')
			return Fbm(x, z, 2600.0, 2, seed + 78) < -0.32 ? '.' : code;
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
		bool   path = false;          ///< chemin local (lieu procédural → réseau)
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

	/// Point de départ par race (consommé par le master à la création).
	struct StartLocationDef
	{
		std::string race;
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
	/// de la position globale. Terrain.cpp
	double MacroHeight(const WorldMaster& m, double x, double z);
	/// Code de biome du lore sous (x, z), bords bruités. Terrain.cpp
	char MacroBiome(const WorldMaster& m, double x, double z);
	/// Code du lore de la case (n, e) (' ' = mer hors grille). Terrain.cpp
	char LoreCode(const WorldMaster& m, int n, int e);

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
		std::vector<Road> roads;        ///< réseau inter-villes (profil en long global)
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
	bool LoadWorldMaster(const std::string& contentRoot, const std::string& worldId,
		WorldMaster& out, std::string& err);

	// Couche macro (une fois).
	/// Grille grossière, sites des villes/POI, lacs. Terrain.cpp
	void BuildMacroTerrain(GlobalWorld& g);
	/// Rivières (descente sur la grille grossière remplie). Terrain.cpp
	void BuildMacroRivers(GlobalWorld& g);
	/// Réseau routier inter-villes + profils en long. Roads.cpp
	void BuildMacroRoads(GlobalWorld& g);

	// Par cellule.
	/// Initialise le contexte d'une cellule (cadrage, éléments globaux recoupant
	/// la cellule). WorldGen.cpp
	void InitCell(World& w, const GlobalWorld& g, engine::world::cells::CellCoord c);
	/// Lieux procéduraux (hameaux, fermes, camps, ruines, mines). Places.cpp
	void BuildPlaces(World& w);
	/// Altitude de base + biome de la cellule. Terrain.cpp
	void BuildCellTerrain(World& w);
	/// Plateaux urbains. Terrain.cpp
	void FlattenSettlements(World& w);
	/// Rivières et lacs (creusement, carte de distance). Terrain.cpp
	void CarveWater(World& w);
	/// Routes globales découpées + chemins locaux, aplanissement. Roads.cpp
	void ApplyRoads(World& w);
	/// Rues, lots, bâtiments, remparts, mobilier, graphe PNJ. Settlements.cpp
	void BuildSettlements(World& w);
	/// Arbres, sous-bois, rochers, herbes. Vegetation.cpp
	void BuildVegetation(World& w);
	/// Spawners PNJ/créatures, respawns, départs, interactibles. Population.cpp
	void BuildPopulation(World& w);
	/// Import du contenu historique (si la ville d'ancrage est dans la cellule). Population.cpp
	bool ImportLegacyContent(World& w, std::string& err);
	/// Écriture des sorties d'une cellule. Output.cpp
	bool WriteCellOutputs(World& w, std::string& err);
	/// Écriture des sorties globales (lieux, carte, départs, POI de quêtes). Output.cpp
	bool WriteGlobalOutputs(GlobalWorld& g, const std::vector<World*>& cells, std::string& err);
	/// Modèles de maisons paramétriques (bibliothèque buildings/templates). Output.cpp
	bool WriteHouseTemplates(const std::string& outputRoot, GenReport& report, std::string& err);

	/// Ajoute à \p ids les cellules des routes reliant les villes de départ
	/// (`connect_start_cities`, cf. WorldMaster). Nécessite g.cities (sites) et
	/// g.roads (BuildMacroRoads). WorldGen.cpp
	void AddStartCorridors(GlobalWorld& g, std::vector<std::string>& ids);

	/// Décode un identifiant de cellule "N003_W009" → (n, e). WorldMaster.cpp
	bool ParseCellId(const std::string& id, int& n, int& e);

	/// Distance d'un point à un segment (plan XZ).
	double DistPointSegment(Vec2 p, Vec2 a, Vec2 b, double* outT = nullptr);
	/// Lissage de Chaikin / rééchantillonnage d'une polyligne. Terrain.cpp
	std::vector<Vec2> SmoothPolyline(const std::vector<Vec2>& in, int iterations);
	std::vector<Vec2> ResamplePolyline(const std::vector<Vec2>& in, double stepM);
}
