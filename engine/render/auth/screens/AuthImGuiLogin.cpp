// AUTH-UI.1 — rendu ImGui de l'écran de connexion (identifiant + mot de passe).
#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur thème en ImVec4 pour ImGui::PushStyleColor.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}
	}

	/// Affiche le panneau de connexion centré : titre du jeu, champs identifiant/mot de passe, boutons Créer un compte et Se connecter.
	void AuthImGuiRenderer::RenderLoginScreen(const VisualState& vs, const RenderModel& rm, float vpW, float vpH)
	{
		/// Résout une clé de traduction via le présentateur ; retombe sur le fallback si absent.
		const auto tr = [this](const char* key, const char* fallback) -> std::string {
			if (m_authPresenter != nullptr)
			{
				std::string s = m_authPresenter->UiTranslate(key);
				if (!s.empty())
				{
					return s;
				}
			}
			return std::string(fallback);
		};

		// Cadre central agrandi de ~30 px en largeur (540 → 570) suite au retour utilisateur :
		// la maquette précédente paraissait à l'étroit, surtout côté boutons.
		const float stageW = (std::min)(570.f, vpW * 0.96f);
		ImGui::SetCursorPosX((vpW - stageW) * 0.5f);
		ImGui::BeginChild("##ln_login_stage", ImVec2(stageW, 0.f), false, ImGuiWindowFlags_NoScrollbar);

		// h2 (sous-titre auth.title_line2) optionnel : on ne le dessine que s'il est non vide,
		// sinon le fallback en dur dupliquait visuellement le titre.
		const std::string& h1 = rm.titleLine1.empty() ? std::string("Les Chroniques de la Lune Noire") : rm.titleLine1;

		// Titre nettement plus haut + marge dans la zone vide entre le bord supérieur de l'écran
		// et le panneau (panneau positionné à vpH * 0.28 par BeginPanel). Le titre doit remplir
		// plus de la moitié de cet espace, avec un peu d'air en haut. SetWindowFontScale(5.0f)
		// donne ≈ 65 px de hauteur de glyphe (Windlass 13 px), centré entre la marge top et le
		// haut du cadre. Pour atteindre franchement « plus de la moitié », on ajoute une bande
		// d'air supérieure proportionnelle au viewport (≈ vpH * 0.05) avant le rendu du titre.
		const float topMargin = (std::max)(24.f, vpH * 0.05f);
		ImGui::SetCursorPosY(topMargin);
		ImGui::SetWindowFontScale(5.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPosX((stageW - w1) * 0.5f);
		ImGui::TextUnformatted(h1.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();

		if (!rm.titleLine2.empty())
		{
			ImGui::SetWindowFontScale(1.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			const float w2 = ImGui::CalcTextSize(rm.titleLine2.c_str()).x;
			ImGui::SetCursorPosX((stageW - w2) * 0.5f);
			ImGui::TextUnformatted(rm.titleLine2.c_str());
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.f);
		}

		ImGui::Spacing();

		std::string panelTitle = rm.sectionTitle.empty() ? std::string("CONNEXION") : rm.sectionTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string ver =
			rm.authLoginVersionBadge.empty() ? std::string("v0.0.0") : rm.authLoginVersionBadge;
		if (!BeginPanel(stageW, stageW, vpH, panelTitle, "", ver, true, false))
		{
			EndPanel();
			ImGui::EndChild();
			return;
		}

		if (!rm.errorText.empty())
		{
			DrawAuthBanner(tr("auth.banner.error_title", "Echec"), rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g,
				LnTheme::kErrorCol.b);
		}
		// Le badge « Langue : … » est rendu au-dessus du cadre par DrawLoginLanguageBadge() ;
		// on supprime la double affichage à l'intérieur du panneau dès que rm.infoBanner
		// correspond au texte capturé sur la transition LangSel → Login. Cette suppression
		// est *permanente* (et pas seulement pendant la fenêtre éphémère) pour éviter que
		// le bandeau ne « saute » dans le cadre principal après le fade-out — l'utilisateur
		// l'a vécu comme un bug visuel.
		const bool suppressLangInfoInsidePanel = !m_loginLangBadgeText.empty()
			&& rm.infoBanner == m_loginLangBadgeText;
		if (vs.submitting && !rm.infoBanner.empty())
		{
			DrawAuthBanner(tr("auth.banner.info_title", "Patience"), rm.infoBanner, LnTheme::kPrimary.r, LnTheme::kPrimary.g,
				LnTheme::kPrimary.b);
		}
		else if (!rm.infoBanner.empty() && !suppressLangInfoInsidePanel)
		{
			DrawAuthBanner(tr("auth.banner.info_title", "Information"), rm.infoBanner, LnTheme::kPrimary.r, LnTheme::kPrimary.g,
				LnTheme::kPrimary.b);
		}

		if (rm.fields.size() >= 2u)
		{
			DrawAuthGoldField(rm.fields[0], m_loginId, static_cast<int>(sizeof(m_loginId)), false);
			DrawAuthGoldField(rm.fields[1], m_loginPw, static_cast<int>(sizeof(m_loginPw)), true);
		}
		else
		{
			DrawField(tr("auth.label.login", "Login").c_str(), m_loginId, static_cast<int>(sizeof(m_loginId)), false);
			DrawField(tr("auth.label.password", "Password").c_str(), m_loginPw, static_cast<int>(sizeof(m_loginPw)), true);
		}

		DrawLoginRememberRow(rm);
		ImGui::Spacing();

		if (m_authPresenter != nullptr)
		{
			const std::string forgotLbl =
				(rm.bodyLines.size() >= 2u && !rm.bodyLines[1].text.empty()) ? rm.bodyLines[1].text :
																				tr("auth.button.forgot_password", "Forgot password");
			if (DrawAuthButtonText(forgotLbl, "##forgot_flow"))
			{
				m_authPresenter->ImGuiNavigateToForgotFromLogin();
			}
			ImGui::SameLine(0.f, 12.f);
			if (DrawAuthButtonText(tr("auth.login.forgot_portal", "Portail web"), "##forgot_portal") && m_authCfg != nullptr
				&& m_authWindow != nullptr)
			{
				m_authPresenter->ImGuiOpenForgotPasswordPortal(*m_authCfg, *m_authWindow);
			}
			ImGui::Spacing();
		}

		const engine::client::AuthUiPresenter::RenderAction* actCreate = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actSubmit = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actOpts = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actQuit = nullptr;
		if (rm.actions.size() >= 4u)
		{
			actCreate = &rm.actions[0];
			actOpts = &rm.actions[1];
			actSubmit = &rm.actions[2];
			actQuit = &rm.actions[3];
		}

		// Boutons « Créer un compte » et « Se connecter » alignés sur le visuel texte simple
		// (cf. « Retour au bureau ») : pas de fond coloré, pas de bordure, pas d'actionBadge.
		if (actCreate != nullptr && actSubmit != nullptr)
		{
			const std::string createLbl =
				actCreate->label.empty() ? tr("auth.login.maquette_create", "CREATE ACCOUNT") : actCreate->label;
			if (DrawAuthButtonText(createLbl, "##login_create_btn") && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiNavigateToRegisterFromLogin();
			}
			ImGui::SameLine(0.f, 24.f);
			const std::string submitLbl =
				actSubmit->label.empty() ? tr("auth.login.maquette_submit", "SIGN IN") : actSubmit->label;
			if (DrawAuthButtonText(submitLbl, "##login_submit_btn") && m_authPresenter != nullptr && m_authCfg != nullptr
				&& !vs.submitting)
			{
				m_authPresenter->ImGuiSubmitLogin(*m_authCfg, m_loginId, m_loginPw, m_rememberMe);
			}
		}
		else if (DrawAuthButtonText(tr("auth.login.maquette_submit", "SIGN IN"), "##login_submit_btn_only")
			&& m_authPresenter != nullptr && m_authCfg != nullptr && !vs.submitting)
		{
			m_authPresenter->ImGuiSubmitLogin(*m_authCfg, m_loginId, m_loginPw, m_rememberMe);
		}

		// Anciennement : DrawSeparator() + DrawLoginFooterChips(rm) qui affichaient les chips
		// [Tab] champ suivant | [Entree] se connecter | [Echap] quitter. L'utilisateur veut ces
		// rappels masqués (les touches restent actives via ImGui InputText nav et le handler
		// d'entrée du presenter), mais la zone visuelle disparaît pour épurer le panneau.

		EndPanel();

		ImGui::EndChild();

		DrawLoginLanguageBadge(vpW, vpH);
		DrawAuthTweaksPanel(vpW, vpH);

		if (actOpts != nullptr && actQuit != nullptr && m_authPresenter != nullptr)
		{
			// Avant : SetCursorPos dans la fenêtre overlay APRÈS DrawAuthTweaksPanel — les boutons
			// étaient bien dessinés mais ne réagissaient pas aux clics (hit-test brouillé par
			// l'autre fenêtre ImGui dessinée juste avant). Solution propre : ouvrir une petite
			// fenêtre dédiée au bas du viewport, comme DrawAuthTweaksPanel pour les races.
			const std::string lo = actOpts->label.empty() ? std::string("OPTIONS") : actOpts->label;
			const std::string lq = actQuit->label.empty() ? tr("auth.footer.chip.esc.desc", "QUIT") : actQuit->label;
			const float tw = ImGui::CalcTextSize(lo.c_str()).x + ImGui::CalcTextSize(lq.c_str()).x + 48.f;
			const float footerW = (std::max)(tw + 32.f, 220.f);
			const float footerH = 36.f;
			ImGui::SetNextWindowPos(ImVec2((vpW - footerW) * 0.5f, vpH - footerH - 12.f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(footerW, footerH), ImGuiCond_Always);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::Begin("##ln_auth_login_footer",
				nullptr,
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
			if (DrawAuthButtonText(lo, "##foot_opts"))
			{
				m_authPresenter->ImGuiOpenLanguageOptionsMenu();
			}
			ImGui::SameLine(0.f, 24.f);
			if (DrawAuthButtonText(lq, "##foot_quit") && m_authWindow != nullptr)
			{
				m_authPresenter->ImGuiRequestClose(*m_authWindow);
			}
			ImGui::End();
		}
	}
} // namespace engine::render

#endif
