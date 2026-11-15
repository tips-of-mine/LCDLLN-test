#pragma once

// Lignes maritimes et bateaux — v1.0.124 (lignes, pontons), v1.0.125 (décision du propriétaire
// du 15/11/2026 : « à bord en temps réel », bateaux visibles, passeur retiré ; LORE_BD §16.51).
//
// tools/world_gen écrit world/<monde>/sea_lines.json (schéma 2) : pour chaque ligne, ses
// escales (ville, ponton = point de débarquement à terre, quai = bateau à l'arrêt), ses
// étapes (trajet du bateau de quai à quai, sens aller), le nombre de bateaux, l'arrêt à quai
// et le prix d'une étape en or.
//
// BoatFleet en déduit l'horaire : chaque bateau parcourt le cycle de sa ligne (boucle :
// 0 → 1 → … → n-1 → 0 ; aller-retour : 0 → … → n-1 → … → 0), s'arrête `stop_seconds` à chaque
// quai et file à vitesse constante entre deux. Les bateaux d'une ligne sont espacés d'une
// fraction égale du cycle. La pose d'un bateau est une fonction PURE du temps (pas d'état) :
// le shard la recalcule à chaque tick.
//
// Repère d'un bateau (dromon de 30 m) : origine au centre de la coque, à la ligne de
// flottaison ; +Z = proue, +X = tribord… vu du dessus avec la convention de Mat4::RotateY :
// monde = (x + lx·cos(cap) + lz·sin(cap), z − lx·sin(cap) + lz·cos(cap)) ; cap 0 = proue vers +Z.
//
// Module PUR (Config + WorldCells, aucune dépendance réseau ni ServerApp) : testé seul
// (sea_lines_tests). Pas thread-safe : thread de tick du shard.

#include "src/shared/world/BoatDeck.h"
#include "src/shared/world/WorldCells.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::server
{
	/// Escale d'une ligne : ville, ponton (débarquement) et quai (bateau à l'arrêt).
	struct SeaLineStop
	{
		std::string cityId;   ///< ville desservie
		std::string name;     ///< nom affiché
		double x = 0.0, y = 0.0, z = 0.0; ///< ponton : point de débarquement (repère GLOBAL, m)
		float yawDeg = 0.0f;  ///< regard du ponton vers la mer
		double dockX = 0.0, dockZ = 0.0;  ///< quai : centre du bateau à l'arrêt (m)
		float dockYawDeg = 0.0f;          ///< cap du bateau à quai (parallèle à la côte, ±180° indifférent)
	};

	/// Trajet d'une étape (sens aller : escale from → to), polyligne monde (m).
	struct SeaLineLeg
	{
		size_t from = 0, to = 0;
		std::vector<double> xs, zs;
	};

	/// Ligne maritime chargée.
	struct SeaLine
	{
		std::string id;
		std::string name;
		engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes;
		bool loop = false;
		uint32_t priceGoldPerLeg = 10; ///< or d'une étape, payé au départ du quai
		uint32_t boats = 0;            ///< bateaux sur la ligne (0 : schéma 1, pas de bateau)
		double stopSeconds = 30.0;     ///< arrêt à quai (s)
		double seaLevelM = 40.0;       ///< niveau de la mer de la carte (ligne de flottaison)
		std::vector<SeaLineStop> stops;
		std::vector<SeaLineLeg> legs;
	};

	class SeaLines final
	{
	public:
		/// Ajoute les lignes d'un texte sea_lines.json (carte = champ "world"), schéma 1 (pontons
		/// seuls, sans bateau) ou 2 (quais, étapes, bateaux). Les escales sans ponton sont
		/// ignorées ; une ligne de moins de 2 escales aussi. Une ligne du schéma 2 dont une étape
		/// manque (ou n'a pas 2 points) n'a pas de bateau (boats = 0).
		/// \return false (rien ajouté) si JSON invalide, schéma inconnu ou monde inconnu.
		bool AddFromText(std::string_view json, std::string& outError);
		/// Charge world/<monde>/sea_lines.json de chaque carte depuis `paths.content`
		/// (remplace le contenu ; un fichier absent n'est pas une erreur).
		bool LoadForCellWorld(const engine::core::Config& config, std::string& outError);

		void Clear() { m_lines.clear(); }
		size_t Count() const { return m_lines.size(); }
		const std::vector<SeaLine>& All() const { return m_lines; }
		const SeaLine* Find(std::string_view id) const;

	private:
		std::vector<SeaLine> m_lines;
	};

	/// Pose d'un bateau à un instant.
	struct BoatPose
	{
		double x = 0.0, z = 0.0;  ///< centre de la coque (repère GLOBAL, m)
		double y = 40.0;          ///< ligne de flottaison (niveau de la mer, m)
		float yawRad = 0.0f;      ///< cap (0 = +Z)
		float speed = 0.0f;       ///< vitesse (m/s), 0 à quai
		bool docked = false;      ///< à l'arrêt à un quai
		size_t stop = 0;          ///< à quai : escale du quai ; en mer : escale d'arrivée
		size_t fromStop = 0;      ///< en mer : escale quittée (= stop à quai)
		double secondsLeft = 0.0; ///< à quai : avant le départ ; en mer : avant l'arrivée
		/// Nombre de départs de quai depuis l'origine des temps (croît de 1 à chaque départ) :
		/// le shard y détecte le départ (paiement de l'étape des passagers).
		int64_t departures = 0;
	};

	/// Horaire de tous les bateaux (voir l'en-tête du fichier).
	class BoatFleet final
	{
	public:
		// Pont du dromon (repère du bateau, m) : src/shared/world/BoatDeck.h (partagé avec le client).
		static constexpr float kDeckY = engine::world::boat::kDeckY;
		static constexpr float kDeckHalfX = engine::world::boat::kDeckHalfX;
		static constexpr float kDeckMinZ = engine::world::boat::kDeckMinZ;
		static constexpr float kDeckMaxZ = engine::world::boat::kDeckMaxZ;
		static constexpr float kStandHeight = engine::world::boat::kStandHeight;
		static constexpr double kBoardRadiusM = engine::world::boat::kBoardRadiusM;
		static constexpr double kTurnM = 40.0;     ///< longueur des virages (m) aux sommets et aux quais

		/// Construit l'horaire des lignes de \p lines qui ont des bateaux (boats ≥ 1, étapes
		/// complètes). \param speedMps vitesse des bateaux (m/s, bornée à [1, 60]).
		void Build(const SeaLines& lines, double speedMps);
		/// Nombre de bateaux (toutes lignes).
		size_t Count() const { return m_boats.size(); }
		/// Ligne du bateau \p boat.
		const SeaLine& LineOf(size_t boat) const { return *m_cycles[m_boats[boat].cycle].line; }
		/// Rang du bateau dans sa ligne (0, 1…).
		uint32_t RankOf(size_t boat) const { return m_boats[boat].rank; }
		/// Durée d'un cycle complet de la ligne du bateau (s).
		double CycleSeconds(size_t boat) const { return m_cycles[m_boats[boat].cycle].period; }
		/// Pose du bateau \p boat à l'instant \p t (s, horloge du shard). Fonction pure.
		BoatPose Evaluate(size_t boat, double t) const;

		/// Repère du bateau → monde (XZ) pour la pose \p p.
		static void LocalToWorld(const BoatPose& p, double lx, double lz, double& outX, double& outZ);
		/// Monde → repère du bateau (XZ) pour la pose \p p.
		static void WorldToLocal(const BoatPose& p, double wx, double wz, double& outLx, double& outLz);
		/// Ramène (lx, lz) sur la surface praticable du pont. \return true si le point a bougé.
		static bool ClampToDeck(float& lx, float& lz);

	private:
		struct Visit
		{
			size_t stop = 0, next = 0;  ///< quai, puis escale d'arrivée
			std::vector<double> xs, zs, cum; ///< trajet vers `next` et abscisses cumulées
			double length = 0.0;
			double arrivalYaw = 0.0;    ///< cap à quai à l'arrivée (cap du quai, ±π le plus proche)
		};
		struct Cycle
		{
			const SeaLine* line = nullptr;
			std::vector<Visit> visits;
			double period = 0.0;
		};
		struct Boat
		{
			size_t cycle = 0;
			uint32_t rank = 0;
			double offset = 0.0; ///< avance dans le cycle (s)
		};
		std::vector<Cycle> m_cycles;
		std::vector<Boat> m_boats;
		double m_speed = 8.0;
	};
}
