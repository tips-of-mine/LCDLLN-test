#pragma once
// EditorAction — déclaration unique d'une action de l'éditeur monde
// (réorganisation UI 2026-07-17, spec docs/superpowers/specs/
// 2026-07-17-editor-menus-toolbar-reorg-design.md §3).
//
// Une action est déclarée UNE fois (id stable, libellé FR, catégorie,
// raccourci, prédicats, callback) puis consommée par toutes les surfaces :
// barre de menu française, barre d'actions, palette de commandes Ctrl+P et
// fenêtre « Raccourcis clavier ». Ce header ne dépend NI d'ImGui NI du
// WorldEditorShell : la logique registre est testable sous ctest Linux.

#include <cstdint>
#include <functional>
#include <string>

namespace engine::editor::world::actions
{
	/// Catégorie d'une action — pilote le groupement dans les menus, la
	/// palette de commandes et la fenêtre « Raccourcis clavier ». L'ordre
	/// des valeurs suit l'ordre des menus de la barre.
	enum class ActionCategory : uint8_t
	{
		Fichier = 0,
		Edition = 1,
		Vue     = 2,
		Fenetre = 3,
		Outils  = 4,
		Aide    = 5,
	};

	/// Déclaration unique d'une action de l'éditeur monde.
	///
	/// Champs fonctionnels (tous optionnels sauf `execute` hors tests) :
	///   - `enabled` : nul => l'action est toujours activable ; sinon évalué
	///     à chaque frame par les surfaces (item de menu grisé, bouton de la
	///     barre d'actions grisé, entrée palette non exécutable).
	///   - `checked` : nul => action « bouton » classique ; non nul => action
	///     « toggle » (coche de menu, ex. visibilité d'un panneau).
	///   - `execute` : effet de l'action. Toujours appelé depuis le main
	///     thread, pendant la frame ImGui (les callbacks capturent des
	///     pointeurs non possédés vers session/shell/config).
	struct EditorAction
	{
		/// Identifiant stable kebab-case, préfixé par domaine
		/// (ex. "file.save", "tool.terrain-sculpt", "window.panel.outliner").
		/// Sert de clé de lookup ; jamais affiché à l'utilisateur.
		std::string id;
		/// Libellé français affiché (menus, palette, tooltips).
		std::string label;
		/// Catégorie de groupement (menu porteur).
		ActionCategory category = ActionCategory::Fichier;
		/// Sous-groupe optionnel dans la catégorie (en-tête `SeparatorText`
		/// des menus, ex. "Import", "Export", "Terrain"). Vide = sans section.
		std::string section;
		/// Texte du raccourci affiché ("Ctrl+S"). L'EXÉCUTION du raccourci
		/// reste dans les dispatchers existants (WorldEditorShell::
		/// HandleShortcut, Engine) — le registre est la source du texte.
		std::string shortcutText;
		/// Prédicat d'activation (nul => toujours actif).
		std::function<bool()> enabled;
		/// Prédicat d'état coché pour les toggles (nul => pas un toggle).
		std::function<bool()> checked;
		/// Effet de l'action (main thread, frame ImGui en cours).
		std::function<void()> execute;
	};
}
