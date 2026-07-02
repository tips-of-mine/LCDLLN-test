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
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;
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

		// Localisation : meme mecanisme que les autres ecrans auth (UiTranslate du
		// presenter). Repli sur la cle brute si le presenter est absent ou si la
		// traduction est vide (evite d'afficher du vide a l'ecran).
		auto tr = [this](const std::string& key) -> std::string {
			if (m_authPresenter == nullptr)
				return std::string();
			std::string s = m_authPresenter->UiTranslate(key);
			return s; // peut etre vide : le caller decide d'afficher ou non
		};

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

		// Systeme de personnages PR2 — factions selectionnables (combo FACTION ->
		// CLASSE). La race est deduite de la faction. Si aucune faction n'est
		// chargee, on retombe sur l'ancien combo RACE (cf. plus bas, guards).
		const std::vector<engine::client::FactionDefinition>* factions =
			ccPresenter ? &ccPresenter->GetFactions() : nullptr;
		std::vector<uint32_t> selectableFactionIdx =
			ccPresenter ? ccPresenter->GetSelectableFactionIndices() : std::vector<uint32_t>{};
		const bool hasFactions = (factions && !selectableFactionIdx.empty());
		// Borne l'index combo (sur la liste des factions selectionnables).
		if (hasFactions &&
		    (m_charFactionIdx < 0 || m_charFactionIdx >= static_cast<int>(selectableFactionIdx.size())))
			m_charFactionIdx = 0;

		// Disposition 2 colonnes : [apercu 3D (largeur fixe) | options (etire)].
		if (ImGui::BeginTable("##cc_layout", 2, ImGuiTableFlags_BordersInnerV, ImVec2(0.f, 0.f)))
		{
			ImGui::TableSetupColumn("##cc_preview", ImGuiTableColumnFlags_WidthFixed, 280.f);
			ImGui::TableSetupColumn("##cc_options", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			// ---------------- Colonne gauche : apercu visuel ----------------
			ImGui::TableSetColumnIndex(0);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
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
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::TextWrapped("%s", line.text.c_str());
				ImGui::PopStyleColor();
			}

			// Systeme de personnages PR2 — FACTION (combo) -> CLASSE (combo). La
			// race est deduite de la faction. Si aucune faction n'est chargee, on
			// retombe sur l'ancien combo RACE pur (compatibilite / robustesse).
			//
			// Le push du mesh d'apercu se fait apres les controles : la colonne
			// gauche affiche le rendu race+genre correspondant (1 frame de latence).

			// Id reels (faction/classe) resolus ce frame, utilises pour la
			// description localisee et la soumission.
			std::string selectedFactionId;
			std::string selectedClassId;

			if (hasFactions)
			{
				// ---- Combo FACTION (seulement les factions selectionnables) ----
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::TextUnformatted("FACTION");
				ImGui::PopStyleColor();

				std::vector<const char*> factionLabels;
				factionLabels.reserve(selectableFactionIdx.size());
				for (uint32_t realIdx : selectableFactionIdx)
					factionLabels.push_back((*factions)[realIdx].displayName.c_str());
				ImGui::SetNextItemWidth(-FLT_MIN);
				const int prevFactionIdx = m_charFactionIdx;
				ImGui::Combo("##charcreate_faction", &m_charFactionIdx, factionLabels.data(),
				             static_cast<int>(factionLabels.size()));
				if (m_charFactionIdx != prevFactionIdx)
					m_charClassIdx = 0; // changement de faction -> reset de la classe

				// Faction reelle selectionnee (index dans GetFactions()).
				const uint32_t realFactionIdx = selectableFactionIdx[static_cast<size_t>(m_charFactionIdx)];
				const engine::client::FactionDefinition& fac = (*factions)[realFactionIdx];
				selectedFactionId = fac.id;

				// ---- Race deduite de la faction : resync m_charRaceIdx ----
				// On cherche l'index de la race de la faction dans la liste des races
				// du presenter pour que l'apercu 3D et les sliders restent fonctionnels.
				const std::string facRaceId = ccPresenter->GetRaceIdForFaction(realFactionIdx);
				if (hasRaces && !facRaceId.empty())
				{
					for (size_t ri = 0; ri < races->size(); ++ri)
					{
						if ((*races)[ri].id == facRaceId)
						{
							m_charRaceIdx = static_cast<int>(ri);
							break;
						}
					}
				}
				// Affiche la race deduite en lecture seule (pas de combo race).
				if (hasRaces && m_charRaceIdx >= 0 && m_charRaceIdx < static_cast<int>(races->size()))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
					ImGui::Text("Race : %s", (*races)[m_charRaceIdx].displayName.c_str());
					ImGui::PopStyleColor();
				}

				// ---- Combo CLASSE (classes de la faction) ----
				const std::vector<engine::client::FactionClass>* facClasses =
					ccPresenter->GetFactionClasses(realFactionIdx);
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::TextUnformatted("CLASSE");
				ImGui::PopStyleColor();
				if (facClasses && !facClasses->empty())
				{
					if (m_charClassIdx < 0 || m_charClassIdx >= static_cast<int>(facClasses->size()))
						m_charClassIdx = 0;
					// Libelles stables (storage local vivant pendant l'appel Combo) :
					// "Nom" ou "Nom - Sous-classe" si la sous-classe est renseignee.
					std::vector<std::string> classLabelStorage;
					classLabelStorage.reserve(facClasses->size());
					for (const auto& c : *facClasses)
					{
						std::string lbl = c.displayName;
						if (!c.subclass.empty())
							lbl += " - " + c.subclass;
						classLabelStorage.push_back(std::move(lbl));
					}
					std::vector<const char*> classLabels;
					classLabels.reserve(classLabelStorage.size());
					for (const auto& s : classLabelStorage)
						classLabels.push_back(s.c_str());
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::Combo("##charcreate_class", &m_charClassIdx, classLabels.data(),
					             static_cast<int>(classLabels.size()));
					selectedClassId = (*facClasses)[static_cast<size_t>(m_charClassIdx)].id;
				}
				else
				{
					ImGui::TextDisabled("(aucune classe pour cette faction)");
				}

				// ---- Descriptions localisees (faction puis classe) ----
				// Cles : "faction.<factionId>.desc" et
				// "class.<factionId>.<classId>.desc". Affichees seulement si la
				// traduction est non vide (pas de texte en dur).
				const std::string facDesc =
					selectedFactionId.empty() ? std::string() : tr("faction." + selectedFactionId + ".desc");
				const std::string clsDesc =
					(selectedFactionId.empty() || selectedClassId.empty())
						? std::string()
						: tr("class." + selectedFactionId + "." + selectedClassId + ".desc");
				if (!facDesc.empty() || !clsDesc.empty())
				{
					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
					if (!facDesc.empty())
						ImGui::TextWrapped("%s", facDesc.c_str());
					if (!facDesc.empty() && !clsDesc.empty())
						ImGui::Spacing();
					if (!clsDesc.empty())
						ImGui::TextWrapped("%s", clsDesc.c_str());
					ImGui::PopStyleColor();
				}
			}
			else if (hasRaces)
			{
				// Repli : ancien combo RACE pur (aucune faction chargee).
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::TextUnformatted("RACE");
				ImGui::PopStyleColor();
				std::vector<const char*> labels;
				labels.reserve(races->size());
				for (const auto& r : *races)
					labels.push_back(r.displayName.c_str());
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::Combo("##charcreate_race", &m_charRaceIdx, labels.data(),
				             static_cast<int>(labels.size()));
			}
			else
			{
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::TextUnformatted("RACE");
				ImGui::PopStyleColor();
				ImGui::TextDisabled("(liste des races indisponible)");
				if (m_charRaceIdx < 0)
					m_charRaceIdx = 0;
			}

			// Genre : 0 = Masculin (male), 1 = Feminin (female). Deux RadioButtons cote
			// a cote. La bascule met a jour l'apercu 3D (mesh genre) plus bas.
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
			ImGui::TextUnformatted("GENRE");
			ImGui::PopStyleColor();
			ImGui::RadioButton("Masculin", &m_charGender, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Féminin", &m_charGender, 1);

			// Teinte de peau : 0 = Claire, 1 = Foncée. Visible sur les parties de
			// peau exposées (mains avec le Ranger ; corps entier avec un futur mesh).
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
			ImGui::TextUnformatted("TEINTE DE PEAU");
			ImGui::PopStyleColor();
			ImGui::RadioButton("Claire", &m_charSkinTone, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Foncée", &m_charSkinTone, 1);

			// Push de l'apercu si la race OU le genre OU la teinte a change (ou 1er rendu).
			if (hasRaces && m_racePreview && m_authPresenter &&
			    (m_charRaceIdx != m_racePreviewSentRaceIdx || m_charGender != m_racePreviewSentGender
			     || m_charSkinTone != m_racePreviewSentSkinTone))
			{
				const std::string genderStr = (m_charGender == 1) ? "female" : "male";
				m_racePreview->SetMesh(
					m_authPresenter->GetRaceMeshForId((*races)[m_charRaceIdx].id, genderStr));
				// Phase 2 — route la peau genrée + teinte dans l'aperçu 3D (live).
				m_racePreview->SetGender(genderStr);
				m_racePreview->SetSkinTone(m_charSkinTone);
				m_racePreviewSentRaceIdx  = m_charRaceIdx;
				m_racePreviewSentGender   = m_charGender;
				m_racePreviewSentSkinTone = m_charSkinTone;
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
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
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
			// Systeme de personnages PR2 — faction + classe choisies, re-resolues
			// depuis le presenter (le scope des combos est interne au tableau).
			std::string factionId;
			std::string classId;
			const auto* presenterForSubmit = m_authPresenter->GetCharacterCreationPresenter();
			if (presenterForSubmit)
			{
				// Faction + race deduite (prioritaire si des factions sont chargees).
				const auto& submitFactions = presenterForSubmit->GetFactions();
				const std::vector<uint32_t> submitSelectable =
					presenterForSubmit->GetSelectableFactionIndices();
				if (!submitSelectable.empty() && m_charFactionIdx >= 0 &&
				    m_charFactionIdx < static_cast<int>(submitSelectable.size()))
				{
					const uint32_t realFactionIdx = submitSelectable[static_cast<size_t>(m_charFactionIdx)];
					if (realFactionIdx < submitFactions.size())
					{
						factionId = submitFactions[realFactionIdx].id;
						// Race deduite de la faction (prioritaire sur m_charRaceIdx).
						const std::string facRaceId = presenterForSubmit->GetRaceIdForFaction(realFactionIdx);
						if (!facRaceId.empty())
							raceId = facRaceId;
						const auto* submitClasses = presenterForSubmit->GetFactionClasses(realFactionIdx);
						if (submitClasses && m_charClassIdx >= 0 &&
						    m_charClassIdx < static_cast<int>(submitClasses->size()))
							classId = (*submitClasses)[static_cast<size_t>(m_charClassIdx)].id;
					}
				}
				else
				{
					// Repli : aucune faction chargee -> raceId via le combo race pur.
					const auto& submitRaces = presenterForSubmit->GetRaces();
					if (m_charRaceIdx >= 0 && m_charRaceIdx < static_cast<int>(submitRaces.size()))
						raceId = submitRaces[m_charRaceIdx].id;
				}
			}
			const char* genderId = (m_charGender == 1) ? "female" : "male";
			const uint8_t skinTone = static_cast<uint8_t>((m_charSkinTone == 1) ? 1 : 0);
				m_authPresenter->ImGuiSubmitCharacterCreate(*m_authCfg, m_charName, raceId.c_str(), genderId, skinTone, factionId.c_str(), classId.c_str());
		}
		EndPanel();
		ImGui::EndChild();
	}
} // namespace engine::render

#endif
