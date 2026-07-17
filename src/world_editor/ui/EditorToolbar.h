#pragma once

#include "src/world_editor/core/WorldEditorShell.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor::world
{
	class ToolbarIconAtlas;

	/// Spec géométrique d'un bouton dans la barre d'actions (réorganisation
	/// UI 2026-07-17 ; géométrie héritée de la toolbar M100.35). Position en
	/// pixels relatifs au coin haut-gauche de la barre (laquelle est elle-
	/// même ancrée juste sous le menu bar ImGui).
	struct ToolbarButtonRect
	{
		float x      = 0.0f;
		float y      = 0.0f;
		float width  = 40.0f;
		float height = 40.0f;
		/// Id de l'action du registre (`EditorActionRegistry`) que ce bouton
		/// déclenche (ex. "file.save"). Toujours non vide : un id absent du
		/// registre ne produit PAS de bouton (omis par `BuildLayout`).
		std::string actionId;
	};

	/// Disposition complète de la barre : la bande horizontale (hauteur fixe
	/// `kToolbarHeightPx`, largeur `viewportWidth`) plus la liste des
	/// rectangles boutons. Pure data, indépendant d'ImGui — sert à la fois
	/// au rendu et aux tests d'invariant de couverture viewport.
	struct ToolbarLayout
	{
		float toolbarY      = 0.0f;
		float toolbarHeight = 0.0f;
		float toolbarWidth  = 0.0f;
		std::vector<ToolbarButtonRect> buttons;
	};

	/// Barre d'actions à boutons-icônes (réorganisation UI 2026-07-17 —
	/// remplace la rangée d'outils M100.35, les outils ayant migré dans la
	/// palette latérale `ToolPalettePanel`). Rendue chaque frame en haut du
	/// viewport 3D, hauteur fixe 48 px, largeur viewport. Ne couvre JAMAIS
	/// un pixel du viewport 3D (invariant M100.35 conservé).
	///
	/// Contenu : les actions générales du registre du shell —
	/// Sauvegarder | Annuler / Rétablir | Valider / Exporter — groupées par
	/// un double espacement. Un id absent du registre (ex. actions session
	/// non enregistrées en mode shell in-game) n'affiche pas de bouton ;
	/// une action dont le prédicat `enabled` est faux affiche un bouton
	/// grisé non cliquable.
	///
	/// Effet de bord à chaque clic sur un bouton actif : `execute()` de
	/// l'action correspondante. Aucune mutation directe du state caméra,
	/// frustum cull, render pass ou framebuffer (invariant terrain visible —
	/// cf. ticket M100.35 « Contexte critique » §4).
	///
	/// Contraintes thread/timing : `Render` doit être appelée depuis le
	/// main thread, dans une frame ImGui en cours (`NewFrame` déjà appelée,
	/// `Render` pas encore). `BuildLayout` est pure et thread-safe.
	class EditorToolbar
	{
	public:
		/// Hauteur fixe de la barre en pixels (spec : 48 px).
		static constexpr float kToolbarHeightPx = 48.0f;
		/// Côté d'une icône utile (32 px) ; le bouton total = 32 + 8 padding
		/// = 40 px de côté. Espacement horizontal entre boutons : 4 px.
		static constexpr float kIconSizePx     = 32.0f;
		static constexpr float kButtonSizePx   = 40.0f;
		static constexpr float kButtonSpacingPx = 4.0f;
		/// Espacement supplémentaire entre deux GROUPES d'actions
		/// (Sauvegarder | Annuler/Rétablir | Valider/Exporter).
		static constexpr float kGroupSpacingPx = 12.0f;
		/// Padding du contenu de la barre : (48 - 40) / 2 = 4 px vertical.
		static constexpr float kToolbarPaddingPx = 4.0f;

		EditorToolbar(WorldEditorShell& shell);

		/// Calcule la disposition de la barre pour une largeur viewport
		/// donnée. Pure : ne mute rien ; lit le registre d'actions du shell
		/// pour omettre les ids non enregistrés. Utilisée par `Render` ET
		/// par les tests d'invariant.
		///
		/// \param viewportWidthPx Largeur de la zone 3D en pixels (en
		///   pratique : `ImGui::GetIO().DisplaySize.x`).
		/// \param menuBarHeightPx Hauteur du menu bar ImGui.
		/// \return Layout complet (un bouton par action trouvée au registre).
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

		/// Exécute l'action du bouton `buttonIndex` du layout donné si elle
		/// existe au registre ET que son prédicat `enabled` est vrai.
		/// Wrapper testable autour de `EditorAction::execute`. No-op si
		/// l'index est hors plage, l'action absente ou désactivée.
		///
		/// Effet de bord : celui de l'action exécutée.
		void HandleClick(const ToolbarLayout& layout, size_t buttonIndex);

#if defined(_WIN32)
		/// Rend la barre dans la frame ImGui courante (Win32 uniquement).
		/// Crée une fenêtre ImGui auto-positionnée juste sous le menu bar,
		/// dessine chaque bouton via `ImDrawList` (style placeholder de
		/// `ToolbarIconAtlas::GetForAction`), grise les actions dont le
		/// prédicat `enabled` est faux, tooltip « libellé (raccourci) ».
		///
		/// Effet de bord : ImGui state + exécution d'action au clic.
		void Render();
#endif

	private:
		WorldEditorShell& m_shell;
		/// Ids d'actions affichés, dans l'ordre. Figé à la construction.
		std::vector<std::string> m_orderedActionIds;
		/// Indices (dans `m_orderedActionIds`) qui DÉMARRENT un nouveau
		/// groupe visuel (espacement `kGroupSpacingPx` supplémentaire).
		std::vector<size_t> m_groupStartIndices;

		/// True si `idx` démarre un nouveau groupe visuel.
		bool IsGroupStart(size_t idx) const;
	};
}
