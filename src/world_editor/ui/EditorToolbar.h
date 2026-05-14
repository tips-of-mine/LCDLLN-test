#pragma once

#include "src/world_editor/core/WorldEditorShell.h"

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	class ToolbarIconAtlas;

	/// Spec géométrique d'un bouton dans la toolbar (M100.35). Position en
	/// pixels relatifs au coin haut-gauche de la toolbar (laquelle est elle-
	/// même ancrée juste sous le menu bar ImGui).
	struct ToolbarButtonRect
	{
		float x      = 0.0f;
		float y      = 0.0f;
		float width  = 40.0f;
		float height = 40.0f;
		/// Outil que ce bouton active. Pour le bouton final "Désélection",
		/// vaut `ActiveTool::None`.
		ActiveTool tool = ActiveTool::None;
	};

	/// Disposition complète de la toolbar : la barre horizontale (hauteur
	/// fixe `kToolbarHeightPx`, largeur `viewportWidth`) plus la liste des
	/// rectangles boutons. Pure data, indépendant d'ImGui — sert à la fois
	/// au rendu et aux tests d'invariant de couverture viewport.
	struct ToolbarLayout
	{
		float toolbarY      = 0.0f;
		float toolbarHeight = 0.0f;
		float toolbarWidth  = 0.0f;
		std::vector<ToolbarButtonRect> buttons;
	};

	/// Barre d'outils à boutons-icônes (M100.35). Rendue chaque frame en
	/// haut du viewport 3D, hauteur fixe 48 px, largeur viewport. Ne couvre
	/// JAMAIS un pixel du viewport 3D (le 3D est dessiné dans le backbuffer
	/// avant ImGui ; la toolbar est une fenêtre ImGui dans le dockspace, son
	/// fond est opaque mais elle vit dans la zone réservée au menu bar +
	/// toolbar et n'empiète pas sur la région 3D).
	///
	/// Effet de bord à chaque clic sur un bouton : appelle
	/// `WorldEditorShell::SetActiveTool` avec la valeur enum correspondante.
	/// Aucune mutation directe du state caméra, frustum cull, render pass
	/// ou framebuffer (invariant terrain visible — cf. ticket M100.35
	/// "Contexte critique" §4).
	///
	/// Contraintes thread/timing : `Render` doit être appelée depuis le
	/// main thread, dans une frame ImGui en cours (`NewFrame` déjà appelée,
	/// `Render` pas encore). `BuildLayout` est pure et thread-safe.
	class EditorToolbar
	{
	public:
		/// Hauteur fixe de la toolbar en pixels (spec : 48 px).
		static constexpr float kToolbarHeightPx = 48.0f;
		/// Côté d'une icône utile (32 px) ; le bouton total = 32 + 8 padding
		/// = 40 px de côté. Espacement horizontal entre boutons : 4 px.
		static constexpr float kIconSizePx     = 32.0f;
		static constexpr float kButtonSizePx   = 40.0f;
		static constexpr float kButtonSpacingPx = 4.0f;
		/// Padding du contenu de la toolbar : (48 - 40) / 2 = 4 px vertical.
		static constexpr float kToolbarPaddingPx = 4.0f;

		EditorToolbar(WorldEditorShell& shell);

		/// Calcule la disposition de la toolbar pour une largeur viewport
		/// donnée. Pure : ne touche pas à `m_shell`, ne dépend que de la
		/// largeur. Utilisée par `Render` ET par les tests d'invariant.
		///
		/// \param viewportWidthPx Largeur de la zone 3D en pixels (en
		///   pratique : `ImGui::GetIO().DisplaySize.x`).
		/// \param menuBarHeightPx Hauteur du menu bar ImGui (en pratique :
		///   `ImGui::GetFrameHeightWithSpacing()` ou la valeur retournée
		///   par `ImGui::GetCurrentContext()->NextWindowData`).
		/// \return Layout complet (bouton par outil + bouton X final).
		///
		/// Effet de bord : aucun.
		ToolbarLayout BuildLayout(float viewportWidthPx,
			float menuBarHeightPx) const;

		/// Hit-test pur (pas d'ImGui). Retourne `true` si le pixel
		/// `(mouseX, mouseY)` (en coords écran) est à l'intérieur de l'un
		/// des rectangles boutons ; remplit `outButtonIndex` avec son index
		/// dans `layout.buttons`. Sinon retourne `false` et ne touche pas
		/// à `outButtonIndex`.
		///
		/// Effet de bord : aucun.
		static bool HitTest(const ToolbarLayout& layout,
			float mouseX, float mouseY, size_t& outButtonIndex);

		/// Active l'outil correspondant au bouton `buttonIndex` du layout
		/// donné. Wrapper testable autour de `WorldEditorShell::SetActiveTool`.
		/// No-op si l'index est hors plage.
		///
		/// Effet de bord : appelle `m_shell.SetActiveTool`.
		void HandleClick(const ToolbarLayout& layout, size_t buttonIndex);

#if defined(_WIN32)
		/// Rend la toolbar dans la frame ImGui courante (Win32 uniquement).
		/// Crée une fenêtre ImGui auto-positionnée juste sous le menu bar,
		/// dessine chaque bouton via `ImDrawList::AddRectFilled` + une
		/// lettre centrale (style placeholder). L'outil actif a un fond
		/// ambre `IM_COL32(255, 196, 64, 96)`. Tooltip FR après ~600 ms.
		///
		/// Effet de bord : ImGui state + appelle `m_shell.SetActiveTool`
		/// au clic.
		void Render();
#endif

	private:
		WorldEditorShell& m_shell;
		/// Liste figée à la construction des outils exposés dans la
		/// toolbar, dans l'ordre d'affichage. Le bouton final "X" (None)
		/// est ajouté à la fin par `BuildLayout` automatiquement.
		std::vector<ActiveTool> m_orderedTools;
	};
}
