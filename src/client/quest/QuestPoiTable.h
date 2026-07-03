#pragma once

#include "src/shared/core/Config.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// Position 2D (plan XZ, unités monde) d'un point d'intérêt de quête, tel
	/// qu'affiché sur la minimap (marqueur cible : PNJ à rejoindre, cluster de
	/// mobs à chasser, etc.).
	struct QuestPoiPosition
	{
		float x = 0.0f;
		float z = 0.0f;
	};

	/// Table client des positions minimap associées à une cible de quête
	/// (`targetId`, ex. `"mob:100"` ou `"npc:elder_marn"`), résolue depuis le
	/// contenu data-driven `quests/quest_poi.json`. Sert à poser les marqueurs
	/// de POI sur la minimap pour l'étape de quête en cours.
	///
	/// Pur côté lecture : ne modifie aucun état de jeu.
	class QuestPoiTable final
	{
	public:
		/// Charge `quests/quest_poi.json` (relatif à `paths.content`). Rejette
		/// (log + retourne false, table laissée inchangée) si le fichier est
		/// absent/invalide, ou si une valeur n'est pas un tableau de paires
		/// numériques `[x, z]`.
		bool Load(const engine::core::Config& cfg);

		/// Liste des positions POI associées à \p targetId, ou `nullptr` si
		/// cette cible n'a aucune position connue.
		const std::vector<QuestPoiPosition>* Positions(std::string_view targetId) const;

	private:
		std::unordered_map<std::string, std::vector<QuestPoiPosition>> m_positionsByTarget;
	};
}
