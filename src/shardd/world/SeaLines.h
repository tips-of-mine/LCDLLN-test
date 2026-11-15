#pragma once

// Lignes maritimes — passeur au ponton, v1.0.124 (décision du propriétaire du 15/11/2026,
// LORE_BD §16.50).
//
// tools/world_gen écrit world/<monde>/sea_lines.json : pour chaque ligne (4 aux Royaumes),
// ses escales (ville, point d'arrivée au ponton, regard) et le prix d'une étape en or. Le
// serveur l'applique à une demande SeaTravelRequest (dialogue du capitaine) :
//   - le joueur doit être vivant et à kPontonRadiusM au plus du ponton d'une escale de la
//     ligne (escale de départ) ;
//   - l'escale d'arrivée doit être une autre escale de la ligne ;
//   - prix = étapes × prix d'une étape (plus court sens si la ligne boucle) ;
//   - arrivée au ponton de l'escale d'arrivée.
//
// Module PUR (Config + WorldCells, aucune dépendance réseau ni ServerApp) : testé seul
// (sea_lines_tests). Pas thread-safe : thread de tick du shard.

#include "src/shared/world/WorldCells.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::server
{
	/// Escale d'une ligne : ville et point d'arrivée au ponton.
	struct SeaLineStop
	{
		std::string cityId;   ///< ville desservie
		std::string name;     ///< nom affiché
		double x = 0.0, y = 0.0, z = 0.0; ///< point d'arrivée (repère GLOBAL de la carte, m)
		float yawDeg = 0.0f;  ///< regard vers la mer
	};

	/// Ligne maritime chargée.
	struct SeaLine
	{
		std::string id;
		std::string name;
		engine::world::cells::CellMap map = engine::world::cells::CellMap::Royaumes;
		bool loop = false;
		uint32_t priceGoldPerLeg = 10;
		std::vector<SeaLineStop> stops;
	};

	/// Résultat de la validation d'une traversée.
	enum class SeaTravelRefusal : uint8_t
	{
		None,          ///< accepté
		UnknownLine,   ///< ligne inconnue
		BadStop,       ///< escale d'arrivée inexistante
		NotAtPonton,   ///< le joueur n'est au ponton d'aucune escale de la ligne
		SameStop,      ///< déjà à l'escale d'arrivée
	};

	/// Traversée validée : prix et point d'arrivée.
	struct SeaTravelPlan
	{
		const SeaLine* line = nullptr;
		size_t fromStop = 0, toStop = 0;
		uint32_t legs = 0;
		uint32_t priceGold = 0;
		const SeaLineStop* arrival = nullptr;
	};

	class SeaLines final
	{
	public:
		static constexpr double kPontonRadiusM = 30.0; ///< distance max au ponton de départ (m)

		/// Ajoute les lignes d'un texte sea_lines.json (carte = champ "world"). Les escales sans
		/// position sont ignorées ; une ligne de moins de 2 escales positionnées aussi.
		/// \return false (rien ajouté) si JSON invalide, schema_version ≠ 1 ou monde inconnu.
		bool AddFromText(std::string_view json, std::string& outError);
		/// Charge world/<monde>/sea_lines.json de chaque carte depuis `paths.content`
		/// (remplace le contenu ; un fichier absent n'est pas une erreur).
		bool LoadForCellWorld(const engine::core::Config& config, std::string& outError);

		void Clear() { m_lines.clear(); }
		size_t Count() const { return m_lines.size(); }
		const std::vector<SeaLine>& All() const { return m_lines; }
		const SeaLine* Find(std::string_view id) const;

		/// Étapes de l'escale \p from à \p to (plus court sens si la ligne boucle).
		static uint32_t Legs(const SeaLine& line, size_t from, size_t to);

		/// Valide une traversée demandée depuis la position GLOBALE (x, z) de la carte \p map.
		/// \return le refus éventuel ; \p outPlan rempli si accepté.
		SeaTravelRefusal Plan(std::string_view lineId, size_t toStop, engine::world::cells::CellMap map,
			double x, double z, SeaTravelPlan& outPlan) const;

	private:
		std::vector<SeaLine> m_lines;
	};
}
