#pragma once

// Primitives de widgets ImGui partagées, lisant le thème actif via LnTheme.
// Extraites d'AuthImGuiRenderer (étape 0c) pour être consommées DIRECTEMENT par
// tous les écrans (auth aujourd'hui, in-game demain), sans passer par l'auth.
//
// Ces fonctions sont sans état : elles ne dépendent que de leurs paramètres, du
// thème actif (LnTheme) et de la pile ImGui courante. Les déclarations n'ont
// aucune dépendance ImGui (types simples) ; les corps (LnWidgets.cpp) sont sous
// #if defined(_WIN32), où ImGui est présent.
//
// Contrat de rendu : ces primitives reproduisent à l'identique le rendu des
// anciennes méthodes privées d'AuthImGuiRenderer (mêmes couleurs, mêmes
// constantes de style, mêmes identifiants ImGui). Ne pas modifier les valeurs
// sans validation visuelle.

#include <cfloat>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LnWidgets
{
	/// Ouvre la fenêtre overlay plein écran (fond figé sur le thème par défaut via
	/// LnTheme::AuthBackdrop, invariant au thème actif). \param windowBgAlpha alpha
	/// du fond. Le caller doit refermer avec ImGui::End().
	/// NOTE (IDs fixes) : utilise l'ID ImGui fixe "##ln_auth_overlay" — correct tant
	/// qu'un seul overlay est à l'écran. Paramétrer un id avant tout usage
	/// multi-fenêtres concurrent (écrans à onglets in-game).
	void BeginFullscreenOverlay(float vpW, float vpH, float windowBgAlpha = 1.f);

	/// Grand titre centré (h1 scale 5.0x + h2 scale 2.5x) dans un BeginChild de
	/// stage 96 % vpW. \param line1 \param line2 fournis par le caller (toute
	/// logique de repli métier reste côté appelant). \param stageId suffixe d'ID du
	/// child ("##ln_<stageId>_stage"). Le caller doit appeler ImGui::EndChild()
	/// après EndPanel.
	void BigTitle(std::string_view line1, std::string_view line2, float vpW, float vpH, const char* stageId);

	/// Ouvre le panneau standard (fond PanelBg, bordure, titre/sous-titre/version
	/// optionnels + séparateur). \return true si ouvert ; EndPanel doit être appelé
	/// dans TOUS les cas (le push de style est équilibré même si false).
	/// NOTE (IDs fixes) : utilise l'ID ImGui fixe "##ln_panel" — correct tant qu'un
	/// seul panneau est à l'écran. Paramétrer un id avant l'usage multi-panneaux
	/// in-game (on heurtera cette limite dès les écrans à onglets).
	bool BeginPanel(float width, float vpW, float vpH, std::string_view title,
		std::string_view subtitle, std::string_view versionLabel = {}, bool versionLeadingInfoGlyph = false,
		bool subtitleWelcomeAccent = false, float fixedHeight = 0.f);

	/// Referme le panneau ouvert par BeginPanel (pop du style + EndChild).
	void EndPanel();

	/// Champ de saisie standard : libellé muté + InputText pleine largeur.
	/// \param buf tampon éditable, \param bufSz sa taille, \param password masque la saisie.
	void Field(std::string_view label, char* buf, int bufSz, bool password = false);

	/// Bannière colorée (titre + message). \param r \param g \param b couleur brute
	/// (sémantique warning/error décidée par le caller).
	/// NOTE : signature en r,g,b bruts conservée pour l'identité bit-à-bit ; une
	/// variante Banner(std::string_view, std::string_view, LnTheme::Rgba) pourra
	/// venir plus tard.
	void Banner(std::string_view title, std::string_view msg, float r, float g, float b);

	/// Séparateur horizontal teinté (kBorder) suivi d'un espacement.
	void Separator();

	/// Rangée d'indices « [touche] libellé » alignés horizontalement.
	void KeycapHints(std::initializer_list<std::pair<const char*, const char*>> hints);

	/// Bouton d'action principal (fond kPrimary). \return true si cliqué.
	bool PrimaryButton(std::string_view label, bool disabled = false, float width = -FLT_MIN);

	/// Bouton secondaire « fantôme » (fond transparent + bordure). \return true si cliqué.
	bool GhostButton(std::string_view label, bool disabled = false, float width = -FLT_MIN);

	/// Fil d'Ariane numéroté ; \param current index de l'étape active (les étapes
	/// avant sont en kSuccess, l'active en kAccent, les suivantes en kMuted).
	void Breadcrumb(std::initializer_list<const char*> steps, int current);
	void Breadcrumb(const std::vector<std::string>& steps, int current);

	/// Rangée de « chips » de pied de page : chaque paire = (clé accentuée, description).
	void FooterChipRow(const std::vector<std::pair<std::string, std::string>>& chips);

	/// Indices de pied de panneau : \param left aligné à gauche, \param right aligné à droite.
	void FooterHints(std::string_view left, std::string_view right);
} // namespace LnWidgets
