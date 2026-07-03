#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor::world::quests
{
	/// Une étape éditable d'une quête (miroir authoring de `QuestStepDefinition`
	/// côté serveur, mais en texte brut : pas d'enum `QuestStepType` ici, la
	/// validation du token `type` est différée à la Tâche 2 (Validate)).
	struct EditedStep
	{
		std::string type;             ///< ex. "kill", "collect", "talk", "enter".
		std::string target;           ///< identifiant cible (ex. "mob:100", "npc:elder_marn").
		uint32_t requiredCount = 1;   ///< quantité requise pour compléter l'étape.
	};

	/// Un item de récompense éditable (itemId + quantité).
	struct EditedRewardItem
	{
		uint32_t itemId = 0;
		uint32_t quantity = 1;
	};

	/// Une quête telle qu'éditée dans l'outil d'authoring : fusion des données
	/// mécaniques (`quest_definitions.json`) et des textes lisibles
	/// (`quest_texts.<lang>.json`), pour affichage/édition unifiés dans l'éditeur.
	struct EditedQuest
	{
		std::string id;
		std::string giver;
		std::string turnIn;
		std::vector<std::string> prereqs;
		std::vector<EditedStep> steps;
		uint32_t rewardXp = 0;
		uint32_t rewardGold = 0;
		std::vector<EditedRewardItem> rewardItems;

		// Textes lisibles, fusionnés depuis quest_texts.<lang>.json. Vides si la
		// quête n'a pas (encore) d'entrée de texte pour la langue chargée.
		std::string title;
		std::string description;
		std::vector<std::string> stepLabels; ///< un libellé brut par étape (même ordre que `steps`).
	};

	/// Entrées/sorties JSON de l'éditeur de quêtes (authoring, SP4).
	///
	/// Pur côté lecture pour cette tâche (Load uniquement) : Validate (Tâche 2)
	/// et Save (Tâche 3) viendront s'y ajouter. Ne modifie aucun état global ;
	/// toute l'E/S passe par des lectures disque explicites au moment de l'appel.
	class QuestEditIo
	{
	public:
		/// Charge les quêtes éditables depuis `<contentRoot>/quests/quest_definitions.json`
		/// (données mécaniques, JSON pur) puis fusionne les textes lisibles
		/// trouvés dans `<contentRoot>/quests/quest_texts.fr.json` (titre,
		/// description, libellés d'étape), appariés par `id` de quête.
		///
		/// \param contentRoot chemin de dossier de contenu (filesystem, pas relatif
		///        à une Config) ; ex. "game/data" en jeu, ou un dossier temporaire
		///        en test.
		/// \param out rempli avec une `EditedQuest` par quête trouvée dans
		///        quest_definitions.json (dans l'ordre du fichier). Vidé en cas
		///        d'échec.
		/// \param outError message d'erreur lisible en cas d'échec (fichier
		///        manquant, JSON invalide, champ requis absent). Vide en cas de
		///        succès.
		/// \return false si quest_definitions.json est absent/invalide ou mal
		///         formé ; true sinon (quest_texts.fr.json manquant n'est PAS
		///         une erreur : les champs texte restent vides).
		///
		/// Effet de bord : lecture disque (aucune écriture). Peut être appelée
		/// depuis n'importe quel thread tant que `contentRoot` n'est pas modifié
		/// concurremment sur disque.
		bool Load(const std::string& contentRoot, std::vector<EditedQuest>& out, std::string& outError) const;
	};
}
