#pragma once

#include "engine/client/AuthUi.h"
#include "engine/core/Config.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <vector>

namespace engine::render
{
	/// Hauteur commune des boutons d’action (auth) : renderer + hit-test + texte (AuthGlyphPass).
	inline constexpr int32_t kAuthUiActionButtonHeightPx = 44;
	/// Case à cocher « se souvenir » : taille extérieure (glyphs + fond + hit-test alignés).
	inline constexpr int32_t kAuthUiCheckboxOuterPx = 20;
	/// Hauteur du rectangle de saisie (renderer + hit-test + placement du texte dans AuthGlyphPass).
	inline constexpr int32_t kAuthUiFieldBoxHeightPx = 32;
	/// Origine X du libellé à droite de la case (contentX + offset).
	inline constexpr int32_t kAuthUiCheckboxLabelOffsetX = 30;
	/// Marge coin du logo statut connexion (alignée sur \c Engine.cpp : \c 24.f + half).
	inline constexpr int32_t kAuthUiStatusLogoCornerMarginPx = 24;
	/// Espace entre la droite du logo et le texte de statut serveur.
	inline constexpr int32_t kAuthUiStatusBannerAfterLogoGapPx = 12;

	/// Nombre de colonnes de la grille d'inscription.
	inline constexpr int32_t kAuthUiGridColumns = 3;
	/// Marge horizontale entre colonnes (px).
	inline constexpr int32_t kAuthUiGridColGapPx = 12;

	/// Calcule la position X et la largeur d'un champ dans la grille.
	/// @param contentX     X de départ de la zone de contenu
	/// @param contentW     Largeur totale de la zone de contenu
	/// @param gridColumn   Index de colonne (0, 1 ou 2)
	/// @param gridSpan     Nombre de colonnes occupées (1, 2 ou 3)
	/// @param outX         [out] Position X du champ
	/// @param outW         [out] Largeur du champ
	inline void AuthUiGridFieldGeometry(int32_t contentX, int32_t contentW,
	    int32_t gridColumn, int32_t gridSpan,
	    int32_t& outX, int32_t& outW)
	{
	    const int32_t colW = (contentW - (kAuthUiGridColumns - 1) * kAuthUiGridColGapPx) / kAuthUiGridColumns;
	    outX = contentX + gridColumn * (colW + kAuthUiGridColGapPx);
	    outW = colW * gridSpan + kAuthUiGridColGapPx * (gridSpan - 1);
	}

	/// Référence dérivée de la largeur panneau (titres, espacements structurels dans les métriques).
	inline int32_t AuthUiLayoutBodyScaleFromPanelW(int32_t panelW)
	{
		return std::clamp(panelW / 260, 2, 4);
	}

	/// Texte courant (corps, champs, liens, libellés de boutons) : un cran plus petit que \ref AuthUiLayoutBodyScaleFromPanelW.
	inline int32_t AuthUiClassicTextScaleFromPanelW(int32_t panelW)
	{
		return std::max(2, AuthUiLayoutBodyScaleFromPanelW(panelW) - 1);
	}

	struct AuthUiLayer
	{
		VkClearColorValue color{};
		VkClearRect rect{};
	};

	struct AuthUiTheme
	{
		float primary[4]{ 0.23f, 0.43f, 0.65f, 1.0f };
		float secondary[4]{ 0.35f, 0.50f, 0.66f, 1.0f };
		float accent[4]{ 0.85f, 0.64f, 0.25f, 1.0f };
		float background[4]{ 0.06f, 0.09f, 0.11f, 1.0f };
		float surface[4]{ 0.09f, 0.13f, 0.17f, 1.0f };
		float panel[4]{ 0.10f, 0.16f, 0.21f, 1.0f };
		float text[4]{ 0.91f, 0.93f, 0.95f, 1.0f };
		float mutedText[4]{ 0.66f, 0.71f, 0.76f, 1.0f };
		float border[4]{ 0.19f, 0.27f, 0.34f, 1.0f };
	};

	struct AuthUiLayoutMetrics
	{
		int32_t panelW = 0;
		int32_t panelH = 0;
		int32_t panelX = 0;
		int32_t panelY = 0;
		int32_t innerX = 0;
		int32_t artW = 0;
		int32_t contentX = 0;
		int32_t contentW = 0;
		int32_t topOffset = 0;
		bool largeContent = false;
		bool compactSingleField = false;
		/// Même pas que AuthGlyphPass / hit-test souris (évite chevauchements et « traits » fantômes).
		int32_t fieldRowStepPx = 48;
		/// Titre principal / sous-titre / section : offsets depuis le haut du panneau (px), source unique avec AuthGlyphPass.
		bool authTitleUseViewportWidth = false;
		int32_t authTitleLine1OffsetFromPanelTopPx = 24;
		int32_t authTitleLine2OffsetFromPanelTopPx = 0;
		int32_t authSectionTitleOffsetFromPanelTopPx = 0;
		/// Message \p model.infoBanner dessiné près du logo statut (haut gauche), pas dans le panneau.
		bool authStatusBannerBesideLogo = false;
		/// Écran d'erreur (\c state.error + \p model.errorText) : zone message et barre d'action (depuis haut panneau).
		int32_t authErrorBoxTopFromPanelTopPx = 0;
		int32_t authErrorBoxHeightPx = 0;
		int32_t authErrorFooterTopFromPanelTopPx = 0;
	};

	AuthUiTheme LoadAuthUiTheme(const engine::core::Config& cfg);

	AuthUiLayoutMetrics BuildAuthUiLayoutMetrics(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model);

	/// Disposition deux rangées (Inscription / Options puis Valider / Quitter) pour l’écran connexion.
	struct AuthLoginTwoRowLayout
	{
		int32_t secondaryRowY = 0;
		int32_t primaryRowY = 0;
		int32_t buttonHalfWidth = 0;
		int32_t primarySubmitWidth = 0;
		int32_t primaryQuitWidth = 0;
	};

	/// Remplit \p out si \p state.login et exactement 4 actions (modèle connexion deux rangées).
	bool TryGetLoginTwoRowLayout(
		const AuthUiLayoutMetrics& lay,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model,
		AuthLoginTwoRowLayout& out);

	/// Si \p calibrationOverlay est true, ajoute des bandes de référence (rouge=haut, vert=bas,
	/// bleu=gauche, jaune=droite, magenta=centre) pour diagnostiquer l’orientation sur une capture.
	/// Si \p usePhotoBackdrop est true, n’applique pas les grands aplats plein écran (fond déjà dessiné par blit).
	std::vector<AuthUiLayer> BuildAuthUiLayers(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model,
		const AuthUiTheme& theme,
		bool calibrationOverlay = false,
		bool usePhotoBackdrop = false);

	/// Centre (pixels, origine haut-gauche) d’une icône info à côté du libellé de champ — aligné sur AuthGlyphPass + hit-test.
	struct AuthFieldInfoIconLayout
	{
		bool valid = false;
		float centerXPx = 0.f;
		float centerYPx = 0.f;
		float halfExtentPx = 9.f;
	};

	std::vector<AuthFieldInfoIconLayout> BuildAuthFieldInfoIconLayouts(
		VkExtent2D extent,
		const engine::client::AuthUiPresenter::VisualState& state,
		const engine::client::AuthUiPresenter::RenderModel& model);
}
