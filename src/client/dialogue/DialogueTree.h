#pragma once

#include <string>
#include <vector>

namespace engine::client
{
	/// Action déclenchée par un choix de dialogue.
	enum class DialogueAction
	{
		Continue,      ///< Navigue vers \c nextNodeId.
		AcceptQuest,   ///< Accepte la quête \c questId puis navigue/ferme.
		CompleteQuest, ///< Complète la quête \c questId puis navigue/ferme.
		End            ///< Termine le dialogue.
	};

	/// Une ligne prononcée par le PNJ (ou une didascalie).
	struct DialogueLine
	{
		std::string text;
		bool        isCue = false; ///< Affichée en italique atténué (ex. « Il sourit »).
	};

	/// Un choix de réponse proposé au joueur.
	struct DialogueChoice
	{
		std::string    text;                              ///< Libellé (word-wrap au rendu).
		std::string    nextNodeId;                        ///< Nœud suivant si action == Continue.
		DialogueAction action  = DialogueAction::Continue;
		int            questId = -1;                      ///< >=0 => lié à une quête (journalisé).
		std::string    icon;                              ///< Optionnel (ex. "⚔️").
	};

	/// Un nœud de dialogue : lignes du PNJ + 1..5 choix.
	struct DialogueNode
	{
		std::string                 id;
		std::vector<DialogueLine>   lines;
		std::vector<DialogueChoice> choices;
	};

	/// Arbre de dialogue complet d'un PNJ.
	struct DialogueTree
	{
		std::string               startNodeId;
		std::vector<DialogueNode> nodes;

		/// Retourne le nœud d'id donné, ou nullptr si absent.
		const DialogueNode* FindNode(const std::string& id) const;
	};

	/// Convertit l'ancien format (liste de lignes en boucle) en arbre à nœud unique
	/// terminé par un choix « Au revoir » (action End). Rétro-compatibilité config.
	DialogueTree BuildTreeFromLegacyLines(const std::vector<std::string>& lines);

	/// Résultat de validation/normalisation d'un arbre.
	struct DialogueValidationResult
	{
		bool                     ok = true;
		std::vector<std::string> errors;
	};

	/// Valide et normalise un arbre chargé depuis la config :
	///  - startNodeId par défaut = premier nœud si vide ;
	///  - chaque nœud doit avoir 1..5 choix (sinon erreur) ;
	///  - chaque choix Continue doit référencer un nœud existant (sinon erreur) ;
	///  - un choix sans action explicite et sans nextNodeId valide est ramené à End.
	/// En cas d'erreur, \c ok = false et \c errors décrit les problèmes ; l'appelant
	/// peut alors retomber sur un nœud unique de secours.
	DialogueValidationResult NormalizeDialogueTree(DialogueTree& tree);

} // namespace engine::client
