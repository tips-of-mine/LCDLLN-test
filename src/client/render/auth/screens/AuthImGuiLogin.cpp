// AUTH-UI.1 - rendu ImGui de l'ecran de connexion (identifiant + mot de passe).
#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	/// Affiche le panneau de connexion centre : titre du jeu, champs identifiant/mot de passe, boutons Creer un compte et Se connecter.
	void AuthImGuiRenderer::RenderLoginScreen(const VisualState& vs, const RenderModel& rm, float vpW, float vpH)
	{
		/// Resout une cle de traduction via le presentateur ; retombe sur le fallback si absent.
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

		// Titre/sous-titre via helper unifie (reference visuelle pour tous les ecrans auth).
		DrawAuthBigTitle(rm, vpW, vpH, "login");
		const float titleZoneW = vpW * 0.96f;

		ImGui::Spacing();

		// Cadre central + 10 px en hauteur ET en largeur (570 > 580 ; +10 px de hauteur ajoute
		// avant EndPanel). La maquette demande ce coup de pouce pour aerer les boutons.
		const float stageW = (std::min)(580.f, vpW * 0.96f);

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
		// BeginPanel recoit la largeur du conteneur englobant comme 2e argument (" vpW " dans la
		// signature, mais utilise pour centrer le panneau via `(vpW - width) / 2`). Comme nous
		// sommes desormais dans un BeginChild de largeur titleZoneW ( vpW), il faut passer
		// titleZoneW - sinon le calcul `(stageW - stageW) / 2 = 0` poussait le panneau contre
		// le bord gauche de la stage (bug observe en revue UX).
		if (!BeginPanel(stageW, titleZoneW, vpH, panelTitle, "", ver, true, false))
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
		// Le badge " Langue : ... " est rendu au-dessus du cadre par DrawLoginLanguageBadge() ;
		// on supprime la double affichage a l'interieur du panneau des que rm.infoBanner
		// correspond au texte capture sur la transition LangSel > Login. Cette suppression
		// est *permanente* (et pas seulement pendant la fenetre ephemere) pour eviter que
		// le bandeau ne " saute " dans le cadre principal apres le fade-out - l'utilisateur
		// l'a vecu comme un bug visuel.
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

		// Suite au retour utilisateur : plus d'espace entre les 4 elements " Identifiant /
		// champ id / Mot de passe / champ pw ". extraSpacingPx=6 ajoute un Dummy entre
		// chaque libelle et son input ; un Dummy(0, 12) supplementaire separe les deux champs.
		if (rm.fields.size() >= 2u)
		{
			DrawAuthGoldField(rm.fields[0], m_loginId, static_cast<int>(sizeof(m_loginId)), false, 6.f);
			ImGui::Dummy(ImVec2(0.f, 12.f));
			DrawAuthGoldField(rm.fields[1], m_loginPw, static_cast<int>(sizeof(m_loginPw)), true, 6.f);
		}
		else
		{
			DrawField(tr("auth.label.login", "Login").c_str(), m_loginId, static_cast<int>(sizeof(m_loginId)), false);
			DrawField(tr("auth.label.password", "Password").c_str(), m_loginPw, static_cast<int>(sizeof(m_loginPw)), true);
		}

		DrawLoginRememberRow(rm);
		// Espace genereux avant les liens secondaires (Recuperation / Portail web) - l'utilisateur
		// veut tous les boutons descendus encore plus bas pour bien aerer le panneau.
		ImGui::Dummy(ImVec2(0.f, 32.f));

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
			// Espace avant les actions principales (Creer un compte / Se connecter).
			ImGui::Dummy(ImVec2(0.f, 28.f));
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

		// Boutons " Creer un compte " et " Se connecter " alignes sur le visuel texte simple
		// (cf. " Retour au bureau ") : pas de fond colore, pas de bordure, pas d'actionBadge.
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
		// rappels masques (les touches restent actives via ImGui InputText nav et le handler
		// d'entree du presenter), mais la zone visuelle disparait pour epurer le panneau.

		// Marge basse a l'interieur du cadre - BeginPanel utilise AutoResizeY, donc ce Dummy
		// translate directement la bordure inferieure du panneau vers le bas. Bumpe a 30 px
		// (suite au retour utilisateur : " il faudrait l'agrandir en hauteur ").
		ImGui::Dummy(ImVec2(0.f, 30.f));

		EndPanel();

		ImGui::EndChild();

		DrawLoginLanguageBadge(vpW, vpH);
		DrawAuthTweaksPanel(vpW, vpH);

		if (actOpts != nullptr && actQuit != nullptr && m_authPresenter != nullptr)
		{
			// Avant : SetCursorPos dans la fenetre overlay APRES DrawAuthTweaksPanel - les boutons
			// etaient bien dessines mais ne reagissaient pas aux clics (hit-test brouille par
			// l'autre fenetre ImGui dessinee juste avant). Solution propre : ouvrir une petite
			// fenetre dediee au bas du viewport, comme DrawAuthTweaksPanel pour les races.
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
