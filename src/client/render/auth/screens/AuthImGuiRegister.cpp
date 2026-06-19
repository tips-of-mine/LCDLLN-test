// AUTH-UI.2 - rendu ImGui de l'ecran de creation de compte (formulaire d'inscription).
#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/auth/AuthImGuiCommon.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur theme en ImVec4 pour ImGui::PushStyleColor.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}
	}

	/// Affiche le formulaire d'inscription : identifiant, e-mail, mots de passe, date de naissance, pays, nom/prenom, et boutons Retour/Creer.
	void AuthImGuiRenderer::RenderRegisterScreen(const RenderModel& rm, float vpW, float vpH)
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

		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "register");
		const float titleZoneW = vpW * 0.96f;

		DrawRegisterFlowHeader(rm, vpW);

		std::string panelTitle = rm.sectionTitle.empty() ? tr("auth.panel.register", "CREATE ACCOUNT") : rm.sectionTitle;
		for (char& ch : panelTitle)
		{
			if (ch >= 'a' && ch <= 'z')
			{
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		const std::string& sub =
			rm.authRegisterPanelSubtitle.empty() ? std::string("FORGER VOTRE IDENTITE") : rm.authRegisterPanelSubtitle;
		const std::string ver =
			rm.authRegisterPanelBadge.empty() ? std::string("2 / 4") : rm.authRegisterPanelBadge;
		// Panel elargi 760 > 880 px : avec 760, le bouton " CREER LE COMPTE " a droite etait
		// coupe en bord de cadre. Cf retour utilisateur sur la maquette.
		if (!BeginPanel(880.f, titleZoneW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
			ImGui::EndChild();
			return;
		}

		if (!rm.errorText.empty())
		{
			DrawBanner("Echec", rm.errorText, LnTheme::kErrorCol.r, LnTheme::kErrorCol.g, LnTheme::kErrorCol.b);
		}

		const bool haveModel = (rm.fields.size() >= 10u && rm.dropdowns.size() >= 3u);
		if (haveModel)
		{
			DrawAuthGoldField(rm.fields[0], m_regId, static_cast<int>(sizeof(m_regId)), false);
			DrawAuthGoldField(rm.fields[4], m_regEmail, static_cast<int>(sizeof(m_regEmail)), false);
			if (!rm.authRegisterEmailHint.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::TextWrapped("%s", rm.authRegisterEmailHint.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			const float halfW = (ImGui::GetContentRegionAvail().x - 14.f) * 0.5f;
			ImGui::Columns(2, "##reg_pw_col", false);
			ImGui::SetColumnWidth(0, halfW);
			ImGui::SetColumnWidth(1, halfW);
			DrawAuthGoldField(rm.fields[8], m_regPw, static_cast<int>(sizeof(m_regPw)), true);
			ImGui::NextColumn();
			DrawAuthGoldField(rm.fields[9], m_regPw2, static_cast<int>(sizeof(m_regPw2)), true);
			ImGui::Columns(1);

			/// Peuple un vecteur de pointeurs C a partir des labels d'une RenderDropdown, necessaire pour ImGui::Combo qui attend un tableau de const char*.
			/// IMPORTANT : on remplit \p store en entier AVANT de prendre les c_str() - chaque push_back peut
			/// reallouer le vector, ce qui invalide les c_str() des elements precedents. Le bug provoquait
			/// l'affichage du libelle du dernier dropdown dans toutes les cases (" Jour de naissance " partout).
			auto buildDdPtrs = [](const engine::client::AuthUiPresenter::RenderDropdown& dd, std::vector<std::string>& store,
								   std::vector<const char*>& ptrs) {
				store.clear();
				store.reserve(dd.options.size());
				for (const auto& o : dd.options)
				{
					store.push_back(o.label);
				}
				ptrs.clear();
				ptrs.reserve(store.size());
				for (const auto& s : store)
				{
					ptrs.push_back(s.c_str());
				}
			};
			std::vector<std::string> dayStore;
			std::vector<const char*> dayPtrs;
			std::vector<std::string> monStore;
			std::vector<const char*> monPtrs;
			std::vector<std::string> yrStore;
			std::vector<const char*> yrPtrs;
			buildDdPtrs(rm.dropdowns[0], dayStore, dayPtrs);
			buildDdPtrs(rm.dropdowns[1], monStore, monPtrs);
			buildDdPtrs(rm.dropdowns[2], yrStore, yrPtrs);

			// Date de naissance : trois colonnes egales (jour / mois / annee) avec libelles au-dessus.
			// Avant : deux combos colles (jour+mois) puis l'annee toute seule sur une nouvelle ligne - la
			// combo annee apparaissait coupee et donnait l'impression que les champs n'etaient pas fonctionnels.
			const float ddGap = 10.f;
			const float ddW = (ImGui::GetContentRegionAvail().x - ddGap * 2.f) / 3.f;
			const float ddStartX = ImGui::GetCursorPosX();
			const std::string& dayLab = rm.dropdowns[0].label.empty() ? std::string("JOUR") : rm.dropdowns[0].label;
			const std::string& monLab = rm.dropdowns[1].label.empty() ? std::string("MOIS") : rm.dropdowns[1].label;
			const std::string& yrLab = rm.dropdowns[2].label.empty() ? std::string("ANNEE") : rm.dropdowns[2].label;
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::TextUnformatted(dayLab.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPosX(ddStartX + ddW + ddGap);
			ImGui::TextUnformatted(monLab.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPosX(ddStartX + (ddW + ddGap) * 2.f);
			ImGui::TextUnformatted(yrLab.c_str());
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::SetNextItemWidth(ddW);
			if (!dayPtrs.empty())
			{
				ImGui::Combo("##reg_day", &m_regBirthDayIdx, dayPtrs.data(), static_cast<int>(dayPtrs.size()));
			}
			ImGui::SameLine(0.f, ddGap);
			ImGui::SetNextItemWidth(ddW);
			if (!monPtrs.empty())
			{
				ImGui::Combo("##reg_month", &m_regBirthMonthIdx, monPtrs.data(), static_cast<int>(monPtrs.size()));
			}
			ImGui::SameLine(0.f, ddGap);
			ImGui::SetNextItemWidth(ddW);
			if (!yrPtrs.empty())
			{
				ImGui::Combo("##reg_year", &m_regBirthYearIdx, yrPtrs.data(), static_cast<int>(yrPtrs.size()));
			}
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(2);
			ImGui::Spacing();

			{
				std::string lab = rm.fields[1].label;
				for (char& ch : lab)
				{
					if (ch >= 'a' && ch <= 'z')
					{
						ch = static_cast<char>(ch - 'a' + 'A');
					}
				}
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
				ImGui::TextUnformatted(lab.c_str());
				ImGui::PopStyleColor();
				if (!rm.authRegisterCountryPick.empty())
				{
					m_regCountryComboIdx =
						std::clamp(m_regCountryComboIdx, 0, static_cast<int>(rm.authRegisterCountryPick.size()) - 1);
					const std::string& preview = rm.authRegisterCountryPick[static_cast<size_t>(m_regCountryComboIdx)].second;
					ImGui::PushStyleColor(ImGuiCol_FrameBg, IV(LnTheme::kSurface));
					ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
					if (ImGui::BeginCombo("##reg_country", preview.c_str()))
					{
						for (int i = 0; i < static_cast<int>(rm.authRegisterCountryPick.size()); ++i)
						{
							const bool sel = (i == m_regCountryComboIdx);
							ImGui::PushID(i);
							if (ImGui::Selectable(rm.authRegisterCountryPick[static_cast<size_t>(i)].second.c_str(), sel))
							{
								m_regCountryComboIdx = i;
							}
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
					ImGui::PopStyleVar(1);
					ImGui::PopStyleColor(2);
					const std::string& iso = rm.authRegisterCountryPick[static_cast<size_t>(m_regCountryComboIdx)].first;
					if (iso.size() >= 2u)
					{
						m_regCountry[0] = iso[0];
						m_regCountry[1] = iso[1];
						m_regCountry[2] = '\0';
					}
				}
				else
				{
					DrawField(tr("auth.label.country", "Country").c_str(), m_regCountry, static_cast<int>(sizeof(m_regCountry)), false);
				}
				ImGui::Spacing();
			}

			const float nameW = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;
			ImGui::Columns(2, "##reg_name_col", false);
			ImGui::SetColumnWidth(0, nameW);
			ImGui::SetColumnWidth(1, nameW);
			DrawAuthGoldField(rm.fields[2], m_regLastName, static_cast<int>(sizeof(m_regLastName)), false);
			ImGui::NextColumn();
			DrawAuthGoldField(rm.fields[3], m_regFirstName, static_cast<int>(sizeof(m_regFirstName)), false);
			ImGui::Columns(1);
		}
		else
		{
			DrawField(tr("auth.label.login", "Login").c_str(), m_regId, static_cast<int>(sizeof(m_regId)));
			DrawField(tr("common.email", "Email").c_str(), m_regEmail, static_cast<int>(sizeof(m_regEmail)));
			DrawField(tr("auth.label.first_name", "First name").c_str(), m_regFirstName, static_cast<int>(sizeof(m_regFirstName)));
			DrawField(tr("auth.label.last_name", "Last name").c_str(), m_regLastName, static_cast<int>(sizeof(m_regLastName)));
			DrawField(tr("auth.label.country", "Country").c_str(), m_regCountry, static_cast<int>(sizeof(m_regCountry)));
		}

		// Checklist mot de passe LIVE, alignée sur la politique serveur (ValidatePassword) :
		// >= 8 caractères, au moins une lettre, au moins un chiffre. États lus depuis le
		// modèle (rm.fields[8].pwdRule*), peuplés par BuildModel_Register via les valideurs
		// PARTAGÉS — donc strictement cohérents avec l'acceptation serveur.
		const engine::client::AuthUiPresenter::RenderField& pwSpec = rm.fields[8];
		auto drawRule = [this, &tr](int32_t state, const char* key, const char* fallback) {
			if (state < 0) return;                       // non évalué (champ vide)
			const bool ok = (state == 1);
			ImGui::PushStyleColor(ImGuiCol_Text, ok ? IV(LnTheme::kAccent) : IV(LnTheme::kErrorCol));
			ImGui::Text("%s %s", ok ? "[v]" : "[x]", tr(key, fallback).c_str());
			ImGui::PopStyleColor();
		};
		drawRule(pwSpec.pwdRuleLength, "auth.register.pwd_rule_length", "Au moins 8 caracteres");
		drawRule(pwSpec.pwdRuleLetter, "auth.register.pwd_rule_letter", "Au moins une lettre");
		drawRule(pwSpec.pwdRuleDigit,  "auth.register.pwd_rule_digit",  "Au moins un chiffre");
		ImGui::Spacing();
		const bool pwdRulesOk = (pwSpec.pwdRuleLength == 1)
			&& (pwSpec.pwdRuleLetter == 1) && (pwSpec.pwdRuleDigit == 1);

		std::string dayStr = "01";
		std::string monStr = "01";
		std::string yrStr = "2000";
		if (rm.dropdowns.size() > 2u)
		{
			const int di = std::clamp(m_regBirthDayIdx, 0, static_cast<int>(rm.dropdowns[0].options.size()) - 1);
			const int mi = std::clamp(m_regBirthMonthIdx, 0, static_cast<int>(rm.dropdowns[1].options.size()) - 1);
			const int yi = std::clamp(m_regBirthYearIdx, 0, static_cast<int>(rm.dropdowns[2].options.size()) - 1);
			/// Garantit un affichage sur deux chiffres pour les valeurs de jour et de mois (ex. "1" > "01").
			auto pad2 = [](std::string s) -> std::string {
				if (s.size() == 1u)
				{
					return std::string("0") + s;
				}
				return s;
			};
			dayStr = pad2(rm.dropdowns[0].options[static_cast<size_t>(di)].value);
			monStr = pad2(rm.dropdowns[1].options[static_cast<size_t>(mi)].value);
			yrStr = rm.dropdowns[2].options[static_cast<size_t>(yi)].value;
		}

		const bool fieldsOk = std::strlen(m_regId) > 0 && std::strlen(m_regEmail) > 0 && std::strlen(m_regFirstName) > 0
			&& std::strlen(m_regLastName) > 0 && std::strlen(m_regCountry) >= 2u;
		const bool canSubmit = fieldsOk && pwdRulesOk && (std::strcmp(m_regPw, m_regPw2) == 0);

		const engine::client::AuthUiPresenter::RenderAction* actSubmit = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actBack = nullptr;
		if (rm.actions.size() >= 2u)
		{
			actSubmit = &rm.actions[0];
			actBack = &rm.actions[1];
		}

		if (actBack != nullptr && actSubmit != nullptr)
		{
			// Boutons d'action alignes sur le visuel texte simple (cf. ecran de connexion) :
			// pas de fond plein, pas de bordure, pas d'actionBadge - un simple SmallButton avec
			// couleur texte muted. Avant, le calcul de rowRight pouvait pousser le bouton submit
			// hors du panneau quand le contenu de la ligne etait large.
			std::string backLab = actBack->label.empty() ? tr("auth.hint.return_login", "Back") : actBack->label;
			if (DrawAuthButtonText(backLab, "##reg_back") && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiBackFromRegisterToLogin();
			}
			if (!rm.authRegisterShowErrorsLabel.empty() && !canSubmit)
			{
				ImGui::SameLine(0.f, 18.f);
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				if (ImGui::SmallButton(rm.authRegisterShowErrorsLabel.c_str()) && m_authPresenter != nullptr && m_authCfg != nullptr)
				{
					m_authPresenter->ImGuiRegisterPreviewValidationErrors(*m_authCfg);
				}
				ImGui::PopStyleColor();
			}

			ImGui::SameLine(0.f, 24.f);
			std::string subLab = actSubmit->label.empty() ? tr("auth.register.submit_create", "CREATE ACCOUNT") : actSubmit->label;
			if (DrawAuthButtonText(subLab, "##reg_submit") && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				engine::client::AuthUiPresenter::RegisterImGuiSubmit form{};
				form.login = m_regId;
				form.email = m_regEmail;
				form.password = m_regPw;
				form.passwordConfirm = m_regPw2;
				form.firstName = m_regFirstName;
				form.lastName = m_regLastName;
				form.birthDay = dayStr.c_str();
				form.birthMonth = monStr.c_str();
				form.birthYear = yrStr.c_str();
				form.countryIso2 = m_regCountry;
				m_authPresenter->ImGuiSubmitRegister(*m_authCfg, form);
			}
			ImGui::Spacing();
		}
		else
		{
			if (DrawGhostButton(tr("auth.register.footer.back", "Back").c_str()) && m_authPresenter != nullptr)
			{
				m_authPresenter->ImGuiBackFromRegisterToLogin();
			}
			ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
			if (DrawPrimaryButton(tr("auth.register.submit_create", "CREATE ACCOUNT").c_str(), !canSubmit) && m_authPresenter != nullptr && m_authCfg != nullptr)
			{
				engine::client::AuthUiPresenter::RegisterImGuiSubmit form{};
				form.login = m_regId;
				form.email = m_regEmail;
				form.password = m_regPw;
				form.passwordConfirm = m_regPw2;
				form.firstName = m_regFirstName;
				form.lastName = m_regLastName;
				form.birthDay = dayStr.c_str();
				form.birthMonth = monStr.c_str();
				form.birthYear = yrStr.c_str();
				form.countryIso2 = m_regCountry;
				m_authPresenter->ImGuiSubmitRegister(*m_authCfg, form);
			}
		}

		// Touche Entree (clavier principal ou pave numerique) : declenche le meme chemin que
		// le bouton « Creer le compte ». Pattern identique a RenderLoginScreen (l.162) et
		// RenderVerifyEmailScreen. Avant ce correctif, l'ecran Register n'avait AUCUN handler
		// Entree (le renderer n'en posait pas, Update_Register sort tot en mode ImGui, et le
		// dispatch clavier global exclut le mode ImGui) : la touche « Valider » annoncee dans
		// le pied de page restait donc sans effet. dayStr/monStr/yrStr et les buffers m_reg*
		// sont encore en portee ici (declares plus haut dans la fonction).
		if (m_authPresenter != nullptr && m_authCfg != nullptr
			&& (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
		{
			engine::client::AuthUiPresenter::RegisterImGuiSubmit form{};
			form.login = m_regId;
			form.email = m_regEmail;
			form.password = m_regPw;
			form.passwordConfirm = m_regPw2;
			form.firstName = m_regFirstName;
			form.lastName = m_regLastName;
			form.birthDay = dayStr.c_str();
			form.birthMonth = monStr.c_str();
			form.birthYear = yrStr.c_str();
			form.countryIso2 = m_regCountry;
			m_authPresenter->ImGuiSubmitRegister(*m_authCfg, form);
		}

		DrawSeparator();
		DrawRegisterFooterChips(rm);
		EndPanel();
		ImGui::EndChild();

		DrawAuthTweaksPanel(vpW, vpH);
	}
} // namespace engine::render

#endif
