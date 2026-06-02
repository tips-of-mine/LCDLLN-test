#pragma once

#include "src/client/dialogue/DialogueTree.h"

#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::client
{
	/// Charge l'arbre de dialogue d'un interactable depuis la config.
	/// \param cfg config du jeu.
	/// \param base préfixe de clé de l'interactable, ex. "world.interactables.0."
	///        (avec le point final).
	/// \param legacyLines lignes de l'ancien champ `dialogue` (fallback si pas d'arbre).
	/// \return un DialogueTree normalisé. Si le bloc `dialogue_tree` est absent OU
	///         invalide, retombe sur BuildTreeFromLegacyLines(legacyLines) (qui, si
	///         vide, produit un nœud unique avec un seul « Au revoir »).
	/// Effet de bord : logue les erreurs de validation.
	DialogueTree LoadDialogueTree(const engine::core::Config& cfg,
	                              const std::string& base,
	                              const std::vector<std::string>& legacyLines);

	/// Convertit une chaîne d'action config en enum (défaut : Continue).
	DialogueAction ParseDialogueAction(const std::string& s);

} // namespace engine::client
