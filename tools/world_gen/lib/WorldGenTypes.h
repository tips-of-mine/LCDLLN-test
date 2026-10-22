#pragma once

// tools/world_gen — types partagés du générateur de monde « Royaumes ».
//
// Le générateur est un outil OFFLINE déterministe : à partir de la source de
// vérité `game/data/world/<world>/world_master.json` (+ imports explicites du
// contenu existant), il produit toutes les données runtime du monde jouable
// (terrain, eau, décor, bâtiments, PNJ, points de départ, carte).
// Aucune dépendance Vulkan/GLFW (règle d'architecture : pas d'engine_core).
//
// Repère monde : X = est, Z = sud (le nord est en -Z, convention du client),
// Y = altitude (m). L'origine (0,0) est le centre de la cellule `origin_cell`.

#include <cstdint>
#include <string>
#include <vector>

#include <zone_builder/JsonDocument.h>

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
		double cellSizeM = 128.0;  ///< taille d'une cellule du lore en mètres
		int    gridCols = 40;      ///< colonnes du lore (= cellules par côté du terrain)
		int    westE = -19;        ///< indice E de la colonne la plus à l'ouest
		int    northN = 18;        ///< indice N de la rangée la plus au nord
		int    padNorthRows = 3;   ///< rangées de mer ajoutées au nord (terrain carré)
		int    originN = 0;        ///< cellule centrée sur (0,0) : indice N
		int    originE = 0;        ///< cellule centrée sur (0,0) : indice E
		double heightScaleM = 400.0; ///< altitude max encodable (r16h normalisé)
		double seaLevelM = 40.0;   ///< niveau de la mer (m)
	};

	/// Ville / village / camp (section `cities`).
	struct CityDef
	{
		std::string id;
		std::string name;
		std::string cell;        ///< "N003_W009"
		std::string kind;        ///< capitale|grande_ville|ville|forteresse|village|hameau|camp
		std::string race;        ///< humains|elfes|orcs|nains|demons|neutre
		std::string state;       ///< intact|ruine|camp
		std::string faction;
		std::vector<std::string> startFor; ///< races dont c'est la ville de départ
		std::string source;      ///< [BD] … / [INFÉRÉ] …
		std::string description;
		// --- résolu par le générateur ---
		int    n = 0, e = 0;     ///< indices de cellule
		Vec2   center;           ///< centre monde
		double radiusM = 20.0;   ///< rayon urbain (m)
		double plateauY = 50.0;  ///< altitude du plateau aplani (m)
	};

	/// Point d'intérêt (section `pois`).
	struct PoiDef
	{
		std::string id;
		std::string name;
		std::string cell;
		std::string kind;        ///< monolithe|faille|ruine|portail
		std::string description;
		int  n = 0, e = 0;
		Vec2 center;
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

	/// Lac posé à la main (section `extra_lakes`), coordonnées monde absolues.
	struct ExtraLakeDef
	{
		std::string id;
		double x = 0.0, z = 0.0;
		double radiusM = 20.0;
	};

	/// Contenu complet du master après chargement + validation.
	struct WorldMaster
	{
		std::string worldId;
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
		/// Réglages fusionnés PAR-DESSUS zone.json (ex. distances de brouillard).
		tools::zone_builder::JsonValue zoneOverrides;
		// Imports du contenu existant (chemins relatifs au content root).
		std::string importZoneConfig;     ///< zone.json dont on recopie les réglages world.*
		std::string importScenery;        ///< scenery.json (world.scenery.N) → scatter
		std::string importBuildings;      ///< buildings.bin (LCBD) recopié tel quel
		std::string importRespawn;        ///< respawn_points.txt recopié (zone id corrigé)
		std::string importAnchorCity;     ///< ville d'ancrage du contenu importé
		uint32_t    gameplayZoneId = 1;   ///< zone gameplay serveur (spawners/respawn)
		uint32_t    zoneNumericId = 1;    ///< id numérique du manifest (ZoneNameRegistry)
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
		std::vector<std::string> warnings;
		std::vector<std::string> writtenFiles;
	};
}
