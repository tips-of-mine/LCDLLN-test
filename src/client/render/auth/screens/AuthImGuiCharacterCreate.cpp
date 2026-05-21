// AUTH-UI.11 - rendu ImGui ecran de creation de personnage avec saisie du nom et confirmation (split depuis AuthImGuiRenderer.cpp).
// Contient RenderCharCreateScreen : panneau 2 colonnes (gauche = apercu 3D,
// droite = nom + race + apparence physique), puis boutons Annuler / Creer.
//
// Sous-projet C MVP (Task 12) — Refactor : la liste des races est
// desormais lue depuis CharacterCreationPresenter::GetRaces() (parsing
// races.json au boot via AuthUiPresenter::Init) au lieu d'etre hardcodee.
//
// CHAR-MODEL.25 — Panneau "Apparence physique" : sliders de proportions
// bornes aux limites de la race + presets rapides (data-driven depuis
// body_proportions.json via CharacterCustomizationSystem). Disposition 2
// colonnes pour garder le panneau de hauteur moyenne et lisible.

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/LnTheme.h"

#include "src/client/auth/AuthUi.h"
#include "src/client/character_creation/CharacterCreationUi.h"
#include "src/client/render/race/RacePreviewViewport.h"

#include <string>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur LnTheme::Rgba en ImVec4 pour les appels de style ImGui.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}
	} // namespace

	/// Affiche l'ecran de creation de personnage en 2 colonnes : apercu 3D a
	/// gauche, options (nom, race, apparence physique) a droite, boutons dessous.
	void AuthImGuiRenderer::RenderCharCreateScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "charcreate");
		const float titleZoneW = vpW * 0.96f;

		// Panneau elargi pour une disposition 2 colonnes. Hauteur en auto-resize :
		// le contenu reparti sur 2 colonnes garde une hauteur moyenne.
		float panelW = 900.f;
		if (panelW > titleZoneW)
			panelW = titleZoneW;

		const std::string panelTitle =
			rm.sectionTitle.empty() ? std::string("Creation de personnage") : rm.sectionTitle;
		if (!BeginPanel(panelW, titleZoneW, vpH, panelTitle.c_str(), "", ""))
		{
			EndPanel();
			ImGui::EndChild();
			return;
		}

		// Presenter + liste des races (data-driven, cf. races.json) + systeme de
		// customisation (limites par race + presets de proportions).
		const auto* ccPresenter =
			(m_authPresenter ? m_authPresenter->GetCharacterCreationPresenter() : nullptr);
		const std::vector<engine::client::RaceDefinition>* races =
			ccPresenter ? &ccPresenter->GetRaces() : nullptr;
		const engine::client::CharacterCustomizationSystem* custSys =
			ccPresenter ? &ccPresenter->GetCustomizationSystem() : nullptr;
		const bool hasRaces = (races && !races->empty());
		if (hasRaces && (m_charRaceIdx < 0 || m_charRaceIdx >= static_cast<int>(races->size())))
			m_charRaceIdx = 0;

		// Disposition 2 colonnes : [apercu 3D (largeur fixe) | options (etire)].
		if (ImGui::BeginTable("##cc_layout", 2, ImGuiTableFlags_BordersInnerV, ImVec2(0.f, 0.f)))
		{
			ImGui::TableSetupColumn("##cc_preview", ImGuiTableColumnFlags_WidthFixed, 280.f);
			ImGui::TableSetupColumn("##cc_options", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			// ---------------- Colonne gauche : apercu visuel ----------------
			ImGui::TableSetColumnIndex(0);
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::TextUnformatted("APERCU");
			ImGui::PopStyleColor();
			if (m_racePreview && m_racePreview->IsValid())
			{
				// `ImTextureID` est defini comme `ImU64` (numerique) dans le fork
				// ImGui utilise ici : static_cast direct (cf. ScenePanel.cpp / M100.34).
				ImGui::Image(static_cast<ImTextureID>(m_racePreview->GetImguiTextureId()),
				             ImVec2(256.0f, 384.0f));
				if (hasRaces)
					ImGui::Text("Race : %s", (*races)[m_charRaceIdx].displayName.c_str());
			}
			else
			{
				ImGui::TextDisabled("(apercu 3D indisponible)");
			}

			// ---------------- Colonne droite : options ----------------
			ImGui::TableSetColumnIndex(1);

			const std::string& nameLabel =
				rm.fields.empty() ? std::string("Nom du personnage") : rm.fields[0].label;
			DrawField(nameLabel.c_str(), m_charName, static_cast<int>(sizeof(m_charName)));
			for (const auto& line : rm.bodyLines)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextWrapped("%s", line.text.c_str());
				ImGui::PopStyleColor();
			}

			// Race (combo data-driven). Le push du mesh d'apercu se fait ici
			// (colonne droite) ; la colonne gauche affiche le rendu correspondant
			// (1 frame de latence au changement, invisible en pratique).
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			ImGui::TextUnformatted("RACE");
			ImGui::PopStyleColor();
			if (hasRaces)
			{
				std::vector<const char*> labels;
				labels.reserve(races->size());
				for (const auto& r : *races)
					labels.push_back(r.displayName.c_str());
				const int prevRaceIdx = m_charRaceIdx;
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::Combo("##charcreate_race", &m_charRaceIdx, labels.data(),
				             static_cast<int>(labels.size()));
				const bool needFirstPush = !m_racePreviewInitialMeshSent;
				if ((m_charRaceIdx != prevRaceIdx || needFirstPush) && m_racePreview && m_authPresenter)
				{
					m_racePreview->SetMesh(m_authPresenter->GetRaceMeshForId((*races)[m_charRaceIdx].id));
					m_racePreviewInitialMeshSent = true;
				}
			}
			else
			{
				ImGui::TextDisabled("(liste des races indisponible)");
				if (m_charRaceIdx < 0)
					m_charRaceIdx = 0;
			}

			// Apparence physique (CHAR-MODEL.25) : sliders bornes a la race +
			// presets. Labels au-dessus des sliders (sinon clippes par -FLT_MIN).
			if (custSys && hasRaces)
			{
				const std::string& curRaceId = (*races)[m_charRaceIdx].id;
				const engine::client::RaceConfiguration* raceCfg = custSys->GetRaceConfig(curRaceId);
				if (raceCfg)
				{
					if (m_charMetricsRaceIdx != m_charRaceIdx)
					{
						m_charBodyMetrics    = custSys->DefaultMetricsForRace(curRaceId);
						m_charMetricsRaceIdx = m_charRaceIdx;
					}

					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
					ImGui::TextUnformatted("APPARENCE PHYSIQUE");
					ImGui::PopStyleColor();

					const auto& lim = raceCfg->physicalLimits;
					ImGui::TextUnformatted("Taille");
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::SliderFloat("##cc_taille", &m_charBodyMetrics.heightScale,
					                   static_cast<float>(lim.height.scaleMin),
					                   static_cast<float>(lim.height.scaleMax), "%.2f");

					if (ImGui::CollapsingHeader("Proportions avancees"))
					{
						ImGui::TextUnformatted("Longueur des jambes");
						ImGui::SetNextItemWidth(-FLT_MIN);
						ImGui::SliderFloat("##cc_legs", &m_charBodyMetrics.legLengthRatio,
						                   static_cast<float>(lim.proportions.legLength.min),
						                   static_cast<float>(lim.proportions.legLength.max), "%.2f");
						ImGui::TextUnformatted("Largeur des epaules");
						ImGui::SetNextItemWidth(-FLT_MIN);
						ImGui::SliderFloat("##cc_shoulders", &m_charBodyMetrics.shoulderWidthRatio,
						                   static_cast<float>(lim.proportions.shoulderWidth.min),
						                   static_cast<float>(lim.proportions.shoulderWidth.max), "%.2f");
						ImGui::TextUnformatted("Corpulence");
						ImGui::SetNextItemWidth(-FLT_MIN);
						ImGui::SliderFloat("##cc_mass", &m_charBodyMetrics.bodyMassIndex,
						                   static_cast<float>(lim.bodyMass.min),
						                   static_cast<float>(lim.bodyMass.max), "%.2f");
					}

					// Presets rapides (data-driven). Wrap manuel sur la largeur
					// de colonne (ImGui ne wrappe pas les boutons en SameLine).
					const auto& presets = custSys->GetProportionPresets();
					if (!presets.empty())
					{
						ImGui::Spacing();
						ImGui::TextUnformatted("Presets rapides :");
						const float avail     = ImGui::GetContentRegionAvail().x;
						const float spacing   = ImGui::GetStyle().ItemSpacing.x;
						const float framePadX = ImGui::GetStyle().FramePadding.x * 2.f;
						float lineUsed = 0.f;
						for (size_t pi = 0; pi < presets.size(); ++pi)
						{
							const float btnW =
								ImGui::CalcTextSize(presets[pi].displayName.c_str()).x + framePadX;
							if (pi > 0)
							{
								if (lineUsed + spacing + btnW <= avail)
								{
									ImGui::SameLine();
									lineUsed += spacing;
								}
								else
								{
									lineUsed = 0.f; // passe a la ligne suivante
								}
							}
							const std::string btnLabel =
								presets[pi].displayName + "##preset_" + presets[pi].id;
							if (ImGui::Button(btnLabel.c_str()))
								custSys->ApplyProportionPreset(curRaceId, presets[pi].id, m_charBodyMetrics);
							lineUsed += btnW;
						}
					}
				}
			}

			ImGui::EndTable();
		}

		// Boutons (pleine largeur, sous les 2 colonnes). Largeurs finies pour
		// eviter que Annuler ne capture les clics destines a Creer.
		ImGui::Spacing();
		const float ccGap  = 8.f;
		const float ccBtnW = (ImGui::GetContentRegionAvail().x - ccGap) * 0.5f;
		if (DrawGhostButton("Annuler", false, ccBtnW) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCancelCharacterCreateReturnToLogin();
		}
		ImGui::SameLine(0.f, ccGap);
		std::string submitLabel = "Creer"; ///< Surcharge par l'action primaire du modele si presente.
		for (const auto& a : rm.actions)
		{
			if (a.primary)
			{
				submitLabel = a.label;
				break;
			}
		}
		if (DrawPrimaryButton(submitLabel.c_str(), false, ccBtnW) && m_authPresenter != nullptr &&
		    m_authCfg != nullptr)
		{
			// Resout le raceId depuis le presenter (fallback "humains").
			std::string raceId = "humains";
			const auto* presenterForSubmit = m_authPresenter->GetCharacterCreationPresenter();
			if (presenterForSubmit)
			{
				const auto& submitRaces = presenterForSubmit->GetRaces();
				if (m_charRaceIdx >= 0 && m_charRaceIdx < static_cast<int>(submitRaces.size()))
					raceId = submitRaces[m_charRaceIdx].id;
			}
			m_authPresenter->ImGuiSubmitCharacterCreate(*m_authCfg, m_charName, raceId.c_str());
		}
		EndPanel();
		ImGui::EndChild();
	}
} // namespace engine::render

#endif
