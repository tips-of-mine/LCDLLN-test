#pragma once

#include "src/client/dialogue/DialogueTree.h"

#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::client
{
	/// Charge l'arbre de dialogue d'un interactable.
	///
	/// Les dialogues sont stockés dans des fichiers dédiés (un par PNJ/zone) sous
	/// `<paths.content>/dialogues/<id>.json`, dont la racine est `{ start, nodes }`.
	/// La config monde ne porte qu'une référence `<base>dialogue_id`.
	/// \param cfg config du jeu (lue pour `<base>dialogue_id` et `paths.content`).
	/// \param base préfixe de clé de l'interactable, ex. "world.interactables.0."
	///        (avec le point final).
	/// \param legacyLines lignes de l'ancien champ `dialogue` (repli si pas d'id).
	/// \return un DialogueTree normalisé. Si `dialogue_id` est absent, OU si le fichier
	///         est introuvable/invalide, retombe sur BuildTreeFromLegacyLines(legacyLines)
	///         (qui, si vide, produit un nœud unique avec un seul « Au revoir »).
	/// Effet de bord : lit un fichier disque ; logue les erreurs (fichier manquant,
	/// validation).
	DialogueTree LoadDialogueTree(const engine::core::Config& cfg,
	                              const std::string& base,
	                              const std::vector<std::string>& legacyLines);

	/// Convertit une chaîne d'action config en enum (défaut : Continue).
	DialogueAction ParseDialogueAction(const std::string& s);

} // namespace engine::client
