#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// Association entre un PNJ et une quête : soit ce PNJ donne la quête
	/// (`role == Giver`), soit il la conclut (`role == TurnIn`). Un même PNJ
	/// peut apparaître dans les deux rôles pour la même quête (donneur ET
	/// receveur, cas le plus courant).
	struct QuestGiverLink
	{
		std::string questId;
		uint8_t role = 0; ///< 0 = donneur (giver), 1 = receveur (turnIn).
	};

	/// Rôle d'un lien PNJ/quête. Miroir explicite des valeurs numériques de
	/// \ref QuestGiverLink::role, pour lisibilité côté appelants UI.
	enum class QuestGiverRole : uint8_t
	{
		Giver = 0,
		TurnIn = 1,
	};

	/// Table client des PNJ pourvoyeurs/receveurs de quêtes, résolue depuis le
	/// contenu data-driven `quests/quest_givers.json`. Sert à afficher les
	/// marqueurs de quête au-dessus des PNJ et à proposer le bon dialogue
	/// (donner / rendre) au clic sur une cible.
	///
	/// Pur côté lecture : ne modifie aucun état de jeu.
	class QuestGiverTable final
	{
	public:
		/// Charge `quests/quest_givers.json` (relatif à `paths.content`).
		/// Rejette (log + retourne false, table laissée inchangée) si le fichier
		/// est absent/invalide, si un `role` n'est pas 0 ou 1, ou si un
		/// `questId` est vide.
		bool Load(const engine::core::Config& cfg);

		/// Liste des liens quête associés au PNJ \p npcTargetId, ou `nullptr`
		/// si ce PNJ n'a aucune quête associée.
		const std::vector<QuestGiverLink>* ForNpc(std::string_view npcTargetId) const;

	private:
		std::unordered_map<std::string, std::vector<QuestGiverLink>> m_linksByNpc;
	};
}
