// AUTH-UI.11 - rendu ImGui ecran de creation de personnage avec saisie du nom et confirmation (split depuis AuthImGuiRenderer.cpp).
// Contient RenderCharCreateScreen : panneau avec champ de nom, lignes d'information issues du modele, et boutons Annuler / Creer.
//
// Sous-projet C MVP (Task 12) — Refactor : la liste des races est
// desormais lue depuis CharacterCreationPresenter::GetRaces() (parsing
// races.json au boot via AuthUiPresenter::Init) au lieu d'etre hardcodee.
// Coherence garantie : si on ajoute / retire une race en data, l'UI suit
// sans changement de code.
//
// Un apercu 3D est affiche via ImGui::Image quand le RacePreviewViewport
// (branche par Engine::Init) est valide. Pour MVP le rendu mesh 3D du
// viewport n'est pas encore implemente (Task 11 = fallback clear color),
// donc on superpose le nom de la race pour que l'utilisateur sache
// laquelle est selectionnee.

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/LnTheme.h"

// Sous-projet C MVP (Task 12) — necessaire pour iterer sur
// CharacterCreationPresenter::GetRaces() et appeler
// RacePreviewViewport::SetMesh / GetImguiTextureId / IsValid.
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

	/// Affiche l'ecran de creation de personnage : champ de saisie du nom, lignes d'information issues du modele, puis boutons Annuler et Creer.
	void AuthImGuiRenderer::RenderCharCreateScreen(const RenderModel& rm, float vpW, float vpH)
	{
		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "charcreate");
		const float titleZoneW = vpW * 0.96f;

		const std::string panelTitle = rm.sectionTitle.empty() ? std::string("Creation de personnage") : rm.sectionTitle;
		if (!BeginPanel(680.f, titleZoneW, vpH, panelTitle.c_str(), "", ""))
		{
			EndPanel();
			ImGui::EndChild();
			return;
		}
		const std::string& nameLabel = rm.fields.empty() ? std::string("Nom du personnage") : rm.fields[0].label;
		DrawField(nameLabel.c_str(), m_charName, static_cast<int>(sizeof(m_charName)));
		for (const auto& line : rm.bodyLines)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", line.text.c_str());
			ImGui::PopStyleColor();
		}

		// Choix de la race : itere sur les races chargees par
		// CharacterCreationPresenter (M39.1, etendu Task 1 avec meshPath).
		// Plus de liste hardcodee : la coherence avec races.json est
		// garantie cote data.
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
		ImGui::TextUnformatted("RACE");
		ImGui::PopStyleColor();
		const auto* ccPresenter = (m_authPresenter ? m_authPresenter->GetCharacterCreationPresenter() : nullptr);
		const std::vector<engine::client::RaceDefinition>* races =
			ccPresenter ? &ccPresenter->GetRaces() : nullptr;
		if (races && !races->empty())
		{
			// Build le vector de libelles affichables (.c_str() refs sur
			// la duree de l'appel Combo ; safe car races est stable).
			std::vector<const char*> labels;
			labels.reserve(races->size());
			for (const auto& r : *races) labels.push_back(r.displayName.c_str());
			if (m_charRaceIdx < 0 || m_charRaceIdx >= static_cast<int>(races->size())) m_charRaceIdx = 0;
			const int prevRaceIdx = m_charRaceIdx;
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::Combo("##charcreate_race", &m_charRaceIdx, labels.data(), static_cast<int>(labels.size()));
			// Si la selection a change OU si c'est la 1ere frame (le
			// viewport n'a pas encore de mesh : detection via
			// m_racePreviewInitialMeshSent) : notifier le preview
			// viewport pour qu'il charge le mesh de la race (lookup
			// via AuthUiPresenter::GetRaceMeshForId qui delegue a
			// Engine::GetRaceMesh, avec fallback humains si la race
			// n'est pas chargee dans m_raceMeshes).
			const bool needFirstPush = !m_racePreviewInitialMeshSent;
			if ((m_charRaceIdx != prevRaceIdx || needFirstPush) && m_racePreview && m_authPresenter)
			{
				m_racePreview->SetMesh(m_authPresenter->GetRaceMeshForId((*races)[m_charRaceIdx].id));
				m_racePreviewInitialMeshSent = true;
			}
			// Preview 3D : ImGui::Image consomme la VkDescriptorSet
			// enregistree par RacePreviewViewport::Init. Task 11 =
			// fallback clear color noir pour MVP (rendu mesh 3D reel
			// laisse a un sous-projet C.1.5 / C.2 qui refactorera
			// SkinnedRenderer pour le rendre RT-agnostic).
			if (m_racePreview && m_racePreview->IsValid())
			{
				ImGui::Spacing();
				// `ImTextureID` est defini comme `ImU64` (numerique) dans
				// le fork ImGui utilise ici : `static_cast` direct, pas
				// `reinterpret_cast` (le cast pointeur echoue sous MSVC
				// C2440). Pattern miroir de ScenePanel.cpp / M100.34.
				ImGui::Image(static_cast<ImTextureID>(m_racePreview->GetImguiTextureId()),
				             ImVec2(256.0f, 384.0f));
				// Overlay : nom de la race par-dessus (compense le
				// fallback viewport qui ne dessine pas encore le mesh
				// 3D pour MVP -> l'utilisateur a quand meme un retour
				// visuel de la race selectionnee).
				ImGui::Text("Race : %s", (*races)[m_charRaceIdx].displayName.c_str());
			}
			else
			{
				// Pas de preview disponible : fallback texte simple
				// (cas typique : Engine n'a pas reussi a creer le
				// viewport, ou test unitaire sans Engine).
				ImGui::TextDisabled("(preview 3D indisponible)");
			}
		}
		else
		{
			// Fallback "race par defaut" si le presenter n'est pas
			// dispo (Init du presenter a rate, ou races.json absent).
			// Le bouton Creer enverra "humains" par defaut.
			ImGui::TextDisabled("(liste des races indisponible)");
			if (m_charRaceIdx < 0) m_charRaceIdx = 0;
		}

		// CHAR-MODEL.25 — Panneau "Apparence physique" : sliders de proportions
		// bornes aux limites de la race + presets rapides. Pilote par
		// CharacterCustomizationSystem (charge depuis game/data/configuration/).
		// Les valeurs editees vivent dans m_charBodyMetrics (etat renderer) ;
		// l'envoi serveur n'est pas encore branche (payload name+race en MVP).
		const engine::client::CharacterCustomizationSystem* custSys =
			ccPresenter ? &ccPresenter->GetCustomizationSystem() : nullptr;
		if (custSys && races && !races->empty() &&
		    m_charRaceIdx >= 0 && m_charRaceIdx < static_cast<int>(races->size()))
		{
			const std::string& curRaceId = (*races)[m_charRaceIdx].id;
			const engine::client::RaceConfiguration* raceCfg = custSys->GetRaceConfig(curRaceId);
			if (raceCfg)
			{
				// Reset des metriques sur changement de race (defauts de la race).
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
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::SliderFloat("Taille", &m_charBodyMetrics.heightScale,
				                   static_cast<float>(lim.height.scaleMin),
				                   static_cast<float>(lim.height.scaleMax), "%.2f");

				if (ImGui::CollapsingHeader("Proportions avancees"))
				{
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::SliderFloat("Longueur des jambes", &m_charBodyMetrics.legLengthRatio,
					                   static_cast<float>(lim.proportions.legLength.min),
					                   static_cast<float>(lim.proportions.legLength.max), "%.2f");
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::SliderFloat("Largeur des epaules", &m_charBodyMetrics.shoulderWidthRatio,
					                   static_cast<float>(lim.proportions.shoulderWidth.min),
					                   static_cast<float>(lim.proportions.shoulderWidth.max), "%.2f");
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::SliderFloat("Corpulence", &m_charBodyMetrics.bodyMassIndex,
					                   static_cast<float>(lim.bodyMass.min),
					                   static_cast<float>(lim.bodyMass.max), "%.2f");
				}

				// Presets rapides (data-driven depuis body_proportions.json).
				const auto& presets = custSys->GetProportionPresets();
				if (!presets.empty())
				{
					ImGui::Spacing();
					ImGui::TextUnformatted("Presets rapides :");
					for (size_t pi = 0; pi < presets.size(); ++pi)
					{
						if (pi > 0) ImGui::SameLine();
						// Label visible + id ImGui unique (##) pour eviter les collisions.
						const std::string btnLabel =
							presets[pi].displayName + "##preset_" + presets[pi].id;
						if (ImGui::Button(btnLabel.c_str()))
						{
							custSys->ApplyProportionPreset(curRaceId, presets[pi].id, m_charBodyMetrics);
						}
					}
				}
			}
		}

		ImGui::Spacing();
		// Largeurs finies pour eviter que Annuler (pleine largeur) ne capture les clics destines
		// a Creer (cf. correctif AuthImGuiTerms.cpp pour la meme classe de bug).
		const float ccGap = 8.f;
		const float ccBtnW = (ImGui::GetContentRegionAvail().x - ccGap) * 0.5f;
		if (DrawGhostButton("Annuler", false, ccBtnW) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCancelCharacterCreateReturnToLogin();
		}
		ImGui::SameLine(0.f, ccGap);
		std::string submitLabel = "Creer"; ///< Libelle du bouton de confirmation, surcharge par l'action primaire du modele si presente.
		for (const auto& a : rm.actions)
		{
			if (a.primary)
			{
				submitLabel = a.label;
				break;
			}
		}
		if (DrawPrimaryButton(submitLabel.c_str(), false, ccBtnW) && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			// Sous-projet C MVP (Task 12) — Resout le raceId depuis le
			// presenter (au lieu d'une liste hardcodee). Fallback
			// "humains" si la liste est vide (alignement avec le
			// fallback de Engine::GetRaceMesh).
			std::string raceId = "humains";
			const auto* presenterForSubmit = m_authPresenter->GetCharacterCreationPresenter();
			if (presenterForSubmit)
			{
				const auto& submitRaces = presenterForSubmit->GetRaces();
				if (m_charRaceIdx >= 0 && m_charRaceIdx < static_cast<int>(submitRaces.size()))
				{
					raceId = submitRaces[m_charRaceIdx].id;
				}
			}
			m_authPresenter->ImGuiSubmitCharacterCreate(*m_authCfg, m_charName, raceId.c_str());
		}
		EndPanel();
		ImGui::EndChild();
	}
} // namespace engine::render

#endif
