// AUTH-UI.2 — rendu ImGui de l'écran de création de compte (formulaire d'inscription).
#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/auth/AuthImGuiCommon.h"
#include "engine/render/LnTheme.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

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

	/// Affiche le formulaire d'inscription : identifiant, e-mail, mots de passe, date de naissance, pays, nom/prénom, et boutons Retour/Créer.
	void AuthImGuiRenderer::RenderRegisterScreen(const RenderModel& rm, float vpW, float vpH)
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

		const std::string& h1 = rm.titleLine1.empty() ? std::string("Les Chroniques de la Lune Noire") : rm.titleLine1;

		ImGui::SetWindowFontScale(2.4f);
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		const float w1 = ImGui::CalcTextSize(h1.c_str()).x;
		ImGui::SetCursorPos(ImVec2((vpW - w1) * 0.5f, vpH * 0.05f));
		ImGui::TextUnformatted(h1.c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();

		if (!rm.titleLine2.empty())
		{
			ImGui::SetWindowFontScale(1.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			const float w2 = ImGui::CalcTextSize(rm.titleLine2.c_str()).x;
			ImGui::SetCursorPos(ImVec2((vpW - w2) * 0.5f, ImGui::GetCursorPosY() + 2.f));
			ImGui::TextUnformatted(rm.titleLine2.c_str());
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.f);
		}

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
			rm.authRegisterPanelSubtitle.empty() ? std::string("FORGER VOTRE IDENTITÉ") : rm.authRegisterPanelSubtitle;
		const std::string ver =
			rm.authRegisterPanelBadge.empty() ? std::string("2 / 4") : rm.authRegisterPanelBadge;
		if (!BeginPanel(760.f, vpW, vpH, panelTitle, sub, ver, true, false))
		{
			EndPanel();
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

			/// Peuple un vecteur de pointeurs C à partir des labels d'une RenderDropdown, nécessaire pour ImGui::Combo qui attend un tableau de const char*.
			/// IMPORTANT : on remplit \p store en entier AVANT de prendre les c_str() — chaque push_back peut
			/// réallouer le vector, ce qui invalide les c_str() des éléments précédents. Le bug provoquait
			/// l'affichage du libellé du dernier dropdown dans toutes les cases (« Jour de naissance » partout).
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

			// Date de naissance : trois colonnes égales (jour / mois / année) avec libellés au-dessus.
			// Avant : deux combos collés (jour+mois) puis l'année toute seule sur une nouvelle ligne — la
			// combo année apparaissait coupée et donnait l'impression que les champs n'étaient pas fonctionnels.
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

		int strength = 0;
		const size_t pwLen = std::strlen(m_regPw);
		if (pwLen >= 8)
		{
			++strength;
		}
		bool hasUpper = false;
		bool hasDigit = false;
		bool hasSym = false;
		for (size_t i = 0; i < pwLen; ++i)
		{
			const unsigned char c = static_cast<unsigned char>(m_regPw[i]);
			if (c >= 'A' && c <= 'Z')
			{
				hasUpper = true;
			}
			if (c >= '0' && c <= '9')
			{
				hasDigit = true;
			}
			if (!std::isalnum(static_cast<unsigned char>(c)))
			{
				hasSym = true;
			}
		}
		if (hasUpper)
		{
			++strength;
		}
		if (hasDigit)
		{
			++strength;
		}
		if (hasSym)
		{
			++strength;
		}

		std::string dayStr = "01";
		std::string monStr = "01";
		std::string yrStr = "2000";
		if (rm.dropdowns.size() > 2u)
		{
			const int di = std::clamp(m_regBirthDayIdx, 0, static_cast<int>(rm.dropdowns[0].options.size()) - 1);
			const int mi = std::clamp(m_regBirthMonthIdx, 0, static_cast<int>(rm.dropdowns[1].options.size()) - 1);
			const int yi = std::clamp(m_regBirthYearIdx, 0, static_cast<int>(rm.dropdowns[2].options.size()) - 1);
			/// Garantit un affichage sur deux chiffres pour les valeurs de jour et de mois (ex. "1" → "01").
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
		const bool canSubmit = fieldsOk && (strength >= 3) && (std::strlen(m_regPw) > 0) && (std::strcmp(m_regPw, m_regPw2) == 0);

		const engine::client::AuthUiPresenter::RenderAction* actSubmit = nullptr;
		const engine::client::AuthUiPresenter::RenderAction* actBack = nullptr;
		if (rm.actions.size() >= 2u)
		{
			actSubmit = &rm.actions[0];
			actBack = &rm.actions[1];
		}

		if (actBack != nullptr && actSubmit != nullptr)
		{
			// Boutons d'action alignés sur le visuel texte simple (cf. écran de connexion) :
			// pas de fond plein, pas de bordure, pas d'actionBadge — un simple SmallButton avec
			// couleur texte muted. Avant, le calcul de rowRight pouvait pousser le bouton submit
			// hors du panneau quand le contenu de la ligne était large.
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

		DrawSeparator();
		DrawRegisterFooterChips(rm);
		EndPanel();

		DrawAuthTweaksPanel(vpW, vpH);
	}
} // namespace engine::render

#endif
