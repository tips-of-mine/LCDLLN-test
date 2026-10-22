#pragma once

// tools/world_gen — contexte de génération partagé entre les étapes du
// pipeline (terrain → eau → routes → agglomérations → végétation → population
// → écriture). Chaque étape lit les résultats des précédentes et écrit les
// siens ; l'ordre est fixé par RunWorldGen (WorldGen.cpp).

#include "WorldGenTypes.h"

#include <zone_builder/JsonDocument.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
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
	};

	/// Rivière (polyligne descendante jusqu'à la mer).
	struct River
	{
		std::string name;
		std::vector<Vec2> points;
		std::vector<double> waterY;   ///< altitude de la surface par point (décroissante)
		std::vector<double> widthM;
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

	/// Contexte complet.
	struct World
	{
		WorldMaster master;
		std::string contentRoot;   ///< lecture (master, imports, existence des meshes)
		std::string outputRoot;    ///< écriture des sorties (= contentRoot en usage normal)

		// Cadrage du terrain.
		double terrainOriginX = 0.0;
		double terrainOriginZ = 0.0;
		double terrainSizeM = 5120.0;

		// Champs (5 m/échantillon pour le terrain 1025², masques 1024²).
		Grid<double> height;        ///< altitude finale (m), 1025²
		Grid<double> baseHeight;    ///< altitude avant villes/routes (pour les coûts)
		Grid<char>   biome;         ///< code de cellule du lore, 1025² (avec bords bruités)
		Grid<float>  roadDist;      ///< distance à la chaussée la plus proche (m), 1025²
		Grid<float>  riverDist;     ///< distance au lit de rivière le plus proche (m), 1025²
		Grid<uint8_t> urban;        ///< 1 = emprise urbaine, 2 = place/rue, 1025²
		Grid<uint8_t> occupied;     ///< occupation fine (1 m) pour bâtiments/props

		std::vector<Road> roads;
		std::vector<River> rivers;
		std::vector<Lake> lakes;
		std::vector<BuildingPlacementDef> buildings;
		std::vector<ScatterDef> scatter;
		std::vector<SettlementLayout> layouts;
		std::vector<SpawnerDef> spawners;
		std::vector<RespawnDef> respawns;
		std::vector<StartLocationDef> starts;
		std::vector<InteractableDef> interactables; ///< résolus (positions absolues)

		/// Réglages `world.*` importés (brouillard, props, DOF…) recopiés dans zone.json.
		tools::zone_builder::JsonValue importedWorldConfig;
		/// Lignes de respawn importées (déjà au format serveur, zone id corrigé).
		std::vector<std::string> importedRespawnLines;

		GenReport report;

		// --- helpers de coordonnées ---
		Vec2 CellCenter(int n, int e) const
		{
			return { (e - master.scale.originE) * master.scale.cellSizeM,
			         -(n - master.scale.originN) * master.scale.cellSizeM };
		}
		/// Cellule du lore contenant le point (n, e) ; renvoie false hors grille.
		bool CellAt(Vec2 p, int& n, int& e) const;
		/// Code de terrain du lore à (n,e) (' ' = mer hors grille).
		char CellCode(int n, int e) const;
		/// Altitude terrain bilinéaire courante.
		double HeightAt(Vec2 p) const { return SampleBilinear(height, p.x, p.z); }
		/// Pente locale en degrés (différences finies sur la heightmap).
		double SlopeDegAt(Vec2 p) const;
		/// Le point est-il sous l'eau (mer, lac, lit de rivière) ?
		bool IsWater(Vec2 p) const;
		/// Le point est-il dans les limites jouables (marge au bord du terrain) ?
		bool InPlayableBounds(Vec2 p, double marginM = 64.0) const;

		/// Marque un disque comme occupé dans la grille fine d'occupation.
		void Occupy(Vec2 c, double radius);
		/// Vrai si le disque recouvre une zone déjà occupée.
		bool IsOccupied(Vec2 c, double radius) const;
		/// Rectangle orienté (demi-dimensions hx en X local, hz en Z local, yaw en
		/// degrés, convention RotateY du moteur) : marquage / test d'occupation.
		void OccupyRect(Vec2 c, double hx, double hz, double yawDeg);
		bool IsOccupiedRect(Vec2 c, double hx, double hz, double yawDeg) const;

		const CityDef* FindCity(const std::string& id) const;
		void Warn(const std::string& msg) { report.warnings.push_back(msg); }
	};

	// ---- Étapes du pipeline (une par fichier .cpp) ----------------------------
	/// Charge et valide le master (+ résolution des cellules). WorldMaster.cpp
	bool LoadWorldMaster(const std::string& contentRoot, const std::string& worldId,
		WorldMaster& out, std::string& err);
	/// Altitude de base + biome bruité. Terrain.cpp
	void BuildBaseTerrain(World& w);
	/// Rivières et lacs (creusement + géométrie d'eau). Terrain.cpp
	void BuildWater(World& w);
	/// Plateaux urbains. Terrain.cpp
	void FlattenSettlements(World& w);
	/// Réseau routier (A*, lissage, aplanissement). Roads.cpp
	void BuildRoads(World& w);
	/// Rues, lots, bâtiments, remparts, mobilier, graphe PNJ. Settlements.cpp
	void BuildSettlements(World& w);
	/// Arbres, sous-bois, rochers, herbes. Vegetation.cpp
	void BuildVegetation(World& w);
	/// Spawners PNJ/créatures, respawns, départs, interactibles. Population.cpp
	void BuildPopulation(World& w);
	/// Import du contenu existant (scenery, buildings, interactables). Population.cpp
	bool ImportLegacyContent(World& w, std::string& err);
	/// Écriture de toutes les sorties. Output.cpp
	bool WriteOutputs(World& w, std::string& err);
	/// Modèles de maisons paramétriques (bibliothèque buildings/templates). Output.cpp
	bool WriteHouseTemplates(World& w, std::string& err);

	/// Décode un identifiant de cellule "N003_W009" → (n, e). WorldMaster.cpp
	bool ParseCellId(const std::string& id, int& n, int& e);

	/// Distance d'un point à un segment (plan XZ).
	double DistPointSegment(Vec2 p, Vec2 a, Vec2 b, double* outT = nullptr);
}
