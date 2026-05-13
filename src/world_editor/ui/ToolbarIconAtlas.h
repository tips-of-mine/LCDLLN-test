#pragma once

#include "src/world_editor/core/WorldEditorShell.h"

#include <cstdint>

namespace engine::editor::world
{
	/// Style visuel d'une icône d'outil (M100.35). Comme la livraison du
	/// ticket ne fournit que des placeholders procéduraux (pas d'art final),
	/// chaque icône est définie par une couleur de fond et une lettre
	/// (initiale FR du nom de l'outil), dessinée à la frame via ImDrawList.
	///
	/// Un futur ticket d'art remplacera cet atlas par un chargement PNG
	/// via `TexturePreviewCache`.
	struct ToolIconStyle
	{
		/// Couleur de fond du bouton en mode placeholder (ARGB encodée
		/// `IM_COL32(r, g, b, a)`). Utilisée par ImDrawList::AddRectFilled
		/// avant le caractère central.
		uint32_t bgColorArgb = 0xFF606060u;
		/// Caractère unique (UTF-8 court) dessiné au centre du bouton.
		const char* letter = "?";
		/// Tooltip ImGui français affiché après ~600 ms de survol.
		const char* tooltipFr = "(outil)";
		/// Si false, le bouton est grisé et non-cliquable. Cas typique :
		/// outil futur déclaré dans l'enum mais pas encore implémenté.
		bool enabled = true;
	};

	/// Atlas (Win32 + Linux) qui retourne le style placeholder pour chaque
	/// `ActiveTool`. Pas de chargement de fichier, pas de ressource GPU,
	/// pas d'état mutable : le contrat est figé à la compilation.
	///
	/// Effet de bord : aucun.
	class ToolbarIconAtlas
	{
	public:
		/// Style à utiliser pour rendre l'icône `tool`. `ActiveTool::None`
		/// retourne le style "Désélectionner" (X rouge).
		static ToolIconStyle Get(ActiveTool tool);

		/// Style explicite du bouton de désélection (icône X), retourné en
		/// fin de toolbar quel que soit l'outil actif.
		static ToolIconStyle GetDeselect();
	};
}
