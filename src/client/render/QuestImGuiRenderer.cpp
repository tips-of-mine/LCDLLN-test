#include "src/client/render/QuestImGuiRenderer.h"

#include "src/client/quest/QuestUi.h"
#include "src/client/quest/QuestTextCatalog.h"
#include "src/client/ui_common/UIModel.h"
#include "src/shared/core/Config.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;

		/// Statuts quête (miroir de `engine::client::QuestStatus` / `quests::QuestStatus`,
		/// wire uint8) — dupliqués ici en constantes locales pour ne pas tirer une
		/// dépendance supplémentaire depuis un simple renderer d'affichage.
		constexpr uint8_t kQuestStatusOffered = 1;
		constexpr uint8_t kQuestStatusActive = 2;
		constexpr uint8_t kQuestStatusReadyToTurnIn = 3;

		/// SP3 Task 3 — miroir de `engine::server::QuestStepType` (QuestRuntime.h),
		/// duplique ici pour la meme raison que les constantes de statut
		/// ci-dessus (pas de dependance shardd depuis un renderer client).
		constexpr uint8_t kQuestStepTypeKill = 1;
		constexpr uint8_t kQuestStepTypeCollect = 2;
		constexpr uint8_t kQuestStepTypeTalk = 3;
		constexpr uint8_t kQuestStepTypeEnter = 4;
	}

	void QuestImGuiRenderer::BindQuestUi(engine::client::QuestUiPresenter* presenter,
		const engine::client::QuestTextCatalog* textCatalog,
		const engine::client::UIModelBinding* uiModelBinding,
		const engine::core::Config* cfg)
	{
		m_presenter = presenter;
		m_textCatalog = textCatalog;
		m_uiModelBinding = uiModelBinding;
		m_cfg = cfg;
	}

	void QuestImGuiRenderer::Render(float viewportW, float viewportH, bool inWorldShard, bool showMapCluster)
	{
		(void)viewportW;
		(void)viewportH;

		if (m_presenter == nullptr || m_textCatalog == nullptr || m_uiModelBinding == nullptr)
			return;

		// Pas de quêtes hors monde (pas de shard courant, donc pas d'état quête valide).
		if (!inWorldShard)
			return;

		RenderJournal();
		// Le tracker et le radar forment le « cluster carte » masquable par le
		// joueur (avec la boussole, gérée côté Engine). Le journal et le panneau
		// donneur restent toujours visibles (interactifs).
		if (showMapCluster)
		{
			RenderTracker();
			RenderMinimap();
		}
		RenderGiverPanel();
	}

	void QuestImGuiRenderer::RenderJournal()
	{
		const engine::client::QuestUiState& state = m_presenter->GetState();
		if (!state.layoutValid)
			return;

		const engine::client::QuestUiRect& bounds = state.journalPanelBounds;
		ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(bounds.width, bounds.height), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.90f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.90f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("Journal de quetes##ln_quest_journal", nullptr, flags))
		{
			// Liste : uniquement Active / ReadyToTurnIn (Offered exclu -- pas
			// encore acceptée, elle vit dans le panneau donneur tant qu'elle
			// n'est pas acceptée).
			ImGui::BeginChild("##ln_quest_journal_list", ImVec2(0.f, ImGui::GetContentRegionAvail().y * 0.45f), true);
			for (const engine::client::QuestJournalEntryView& entry : state.journalEntries)
			{
				if (entry.status != kQuestStatusActive && entry.status != kQuestStatusReadyToTurnIn)
					continue;

				const std::string title = m_textCatalog->Title(entry.questId);
				char label[256];
				std::snprintf(label, sizeof(label), "%s (%u/%u)##%s",
					title.c_str(), entry.completedSteps, entry.totalSteps, entry.questId.c_str());

				const bool readyToTurnIn = (entry.status == kQuestStatusReadyToTurnIn);
				if (readyToTurnIn)
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kSuccess));

				if (ImGui::Selectable(label, entry.selected))
				{
					m_presenter->SelectQuest(entry.questId);
				}

				if (readyToTurnIn)
					ImGui::PopStyleColor();
			}
			ImGui::EndChild();

			ImGui::Separator();

			// Détail : titre / description / étapes (StepLabel) / récompenses.
			// Textes issus du QuestTextCatalog (Task 1) -- jamais synthétisés ici.
			ImGui::BeginChild("##ln_quest_journal_detail", ImVec2(0.f, 0.f), false);
			if (!state.selectedQuestId.empty())
			{
				const std::string title = m_textCatalog->Title(state.selectedQuestId);
				const std::string description = m_textCatalog->Description(state.selectedQuestId);

				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::TextWrapped("%s", title.c_str());
				ImGui::PopStyleColor();

				if (!description.empty())
				{
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
					ImGui::TextUnformatted(description.c_str());
					ImGui::PopTextWrapPos();
				}

				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::TextUnformatted("Etapes :");
				ImGui::PopStyleColor();
				for (size_t i = 0; i < state.selectedQuestSteps.size(); ++i)
				{
					const engine::client::QuestStepView& step = state.selectedQuestSteps[i];
					// StepLabel inclut DÉJÀ le compteur (template quest_texts.fr.json
					// « … {current}/{required} », ou fallback « current/required »).
					// Ne pas ré-ajouter « (x/y) » ici sinon double affichage « 9/10 (9/10) ».
					const std::string stepLabel = m_textCatalog->StepLabel(
						state.selectedQuestId, i, step.currentCount, step.requiredCount);
					if (step.completed)
						ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kSuccess));
					ImGui::BulletText("%s", stepLabel.c_str());
					if (step.completed)
						ImGui::PopStyleColor();
				}

				// Récompenses : lues directement sur le modèle (le QuestUiState du
				// presenter n'expose pas les récompenses, seulement les étapes).
				const engine::client::UIModel& model = m_uiModelBinding->GetModel();
				for (const engine::client::UIQuestEntry& quest : model.quests)
				{
					if (quest.questId != state.selectedQuestId)
						continue;

					if (quest.rewardExperience > 0 || quest.rewardGold > 0 || !quest.rewardItems.empty())
					{
						ImGui::Spacing();
						ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
						ImGui::TextUnformatted("Recompenses :");
						ImGui::PopStyleColor();
						if (quest.rewardExperience > 0)
							ImGui::BulletText("%u XP", quest.rewardExperience);
						if (quest.rewardGold > 0)
							ImGui::BulletText("%u or", quest.rewardGold);
						for (const auto& item : quest.rewardItems)
						{
							ImGui::BulletText("Objet #%u x%u", item.itemId, item.quantity);
						}
					}
					break;
				}
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::TextUnformatted("Aucune quete selectionnee.");
				ImGui::PopStyleColor();
			}
			ImGui::EndChild();
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}

	void QuestImGuiRenderer::RenderTracker()
	{
		const engine::client::QuestUiState& state = m_presenter->GetState();
		if (!state.layoutValid || state.trackerSteps.empty())
			return;

		// Position : empilé SOUS le radar minimap, coin haut-droit, aligné à droite.
		// Corrige le chevauchement (retour joueur 2026-07-08 : « collision entre le
		// suivi de quêtes et le mini radar »). CAUSE : l'ancien calcul recomposait à
		// la main le bas du radar (16 + 70 + 8 + 200 = 294) alors que le radar réel
		// descend jusqu'à y0(140) + taille(200) = 340 → le tracker montait ~34 px
		// trop haut. FIX : on lit le VRAI rectangle du radar via ComputeRadarScreenRect
		// (même source que RenderMinimap et le hit-test de zoom) → zéro dérive. Si le
		// radar est désactivé, on retombe sous le dégagement HUD haut (marge + météo).
		// Cond_Always : le HUD doit suivre résolution/config sans se figer sur une
		// position imgui.ini périmée (fenêtre NoMove + NoSavedSettings de toute façon).
		const ImGuiIO& io = ImGui::GetIO();
		const float trackerMargin = 16.0f;
		const float weatherHudHeightPx = 70.0f;
		const engine::client::RadarScreenRect radarRect =
			engine::client::ComputeRadarScreenRect(*m_cfg, io.DisplaySize.x, io.DisplaySize.y);
		const float radarBottom = radarRect.enabled
			? (radarRect.y0 + radarRect.size)
			: (trackerMargin + weatherHudHeightPx);
		const float trackerW = state.trackerBounds.width;
		const float trackerH = state.trackerBounds.height;
		ImGui::SetNextWindowPos(
			ImVec2(io.DisplaySize.x - trackerMargin - trackerW, radarBottom + 12.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(trackerW, trackerH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.78f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoInputs;

		if (ImGui::Begin("##ln_quest_tracker", nullptr, flags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::TextUnformatted("Suivi de quetes");
			ImGui::PopStyleColor();
			ImGui::Separator();

			for (const engine::client::QuestStepView& step : state.trackerSteps)
			{
				if (step.completed)
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kSuccess));
				ImGui::TextWrapped("%s (%u/%u)", step.label.c_str(), step.currentCount, step.requiredCount);
				if (step.completed)
					ImGui::PopStyleColor();
			}
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}

	void QuestImGuiRenderer::RenderMinimap()
	{
		// Config : desactivable (client.quest.minimap.enabled, defaut true) et
		// taille pixels du cadre carre (client.quest.minimap.size_px, defaut 200).
		// m_cfg est non-null : invariant maintenu par l'unique appelant BindQuestUi
		// (les 4 pointeurs sont liés ensemble ; Render() vérifie les 3 autres).
		const engine::client::QuestUiState& state = m_presenter->GetState();
		if (!state.layoutValid)
			return;

		// Géométrie du radar partagée avec le hit-test Engine (molette/clic de zoom)
		// via ComputeRadarScreenRect -> zéro dérive rendu/interaction. `enabled` et
		// `size_px` (config) sont résolus dans le helper. Ancrage : coin haut-droit,
		// sous le HUD météo + la boussole (cf. helper).
		const ImGuiIO& io = ImGui::GetIO();
		const engine::client::RadarScreenRect rect =
			engine::client::ComputeRadarScreenRect(*m_cfg, io.DisplaySize.x, io.DisplaySize.y);
		if (!rect.enabled)
			return;

		const float sizePx = rect.size;
		const float x0 = rect.x0;
		const float y0 = rect.y0;
		const float x1 = x0 + sizePx;
		const float y1 = y0 + sizePx;
		const ImVec2 center((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);

		ImDrawList* dl = ImGui::GetForegroundDrawList();

		// Fond + bordure CIRCULAIRE + croix centrale (repere joueur). Les données
		// sont déjà radiales (WorldToRadarUv teste dist > rayon) : seul le dessin
		// devient circulaire (retour joueur 2026-07-04 : radar carré -> rond).
		const float radiusPx = sizePx * 0.5f;
		dl->AddCircleFilled(center, radiusPx, IM_COL32(10, 12, 16, 170), 48);
		dl->AddCircle(center, radiusPx, LnTheme::ToU32(LnTheme::kBorder), 48, 2.0f);
		dl->AddLine(ImVec2(center.x - 6.0f, center.y), ImVec2(center.x + 6.0f, center.y), LnTheme::ToU32(LnTheme::kMuted), 1.0f);
		dl->AddLine(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x, center.y + 6.0f), LnTheme::ToU32(LnTheme::kMuted), 1.0f);

		// Marqueurs de POI de quete, teintes par type d'etape.
		for (const engine::client::MinimapPoiView& poi : state.questPois)
		{
			if (!poi.visible)
				continue;

			// Clip circulaire : un POI hors du disque (u,v clampés au coin carré par
			// WorldToRadarUv quand off-radar) ne doit pas s'afficher dans les coins.
			const float pdu = poi.u - 0.5f;
			const float pdv = poi.v - 0.5f;
			if (pdu * pdu + pdv * pdv > 0.25f)
				continue;

			ImU32 color = IM_COL32(180, 180, 180, 255); // repli : collect/inconnu = gris
			switch (poi.stepType)
			{
			case kQuestStepTypeKill:
				color = IM_COL32(220, 60, 60, 255); // rouge
				break;
			case kQuestStepTypeTalk:
				color = IM_COL32(230, 200, 60, 255); // jaune
				break;
			case kQuestStepTypeEnter:
				color = IM_COL32(70, 140, 230, 255); // bleu
				break;
			case kQuestStepTypeCollect:
			default:
				break; // gris (repli ci-dessus)
			}

			// Retour joueur 2026-07-08 : « je ne veux que des points colorés dans le
			// radar, pas de texte ». On ne dessine QUE la pastille teintée par type
			// d'étape ; le libellé reste disponible dans le tracker/journal.
			const ImVec2 p(x0 + poi.u * sizePx, y0 + poi.v * sizePx);
			dl->AddCircleFilled(p, 4.5f, color);
		}

		// Marqueur joueur : petit triangle plein au centre du radar (toujours
		// (0.5, 0.5), cf. QuestUiPresenter::RebuildMinimap).
		if (state.playerMarker.visible)
		{
			const ImVec2 p(x0 + state.playerMarker.u * sizePx, y0 + state.playerMarker.v * sizePx);
			const ImU32 playerColor = LnTheme::ToU32(LnTheme::kAccent);
			dl->AddTriangleFilled(
				ImVec2(p.x, p.y - 6.0f),
				ImVec2(p.x - 5.0f, p.y + 5.0f),
				ImVec2(p.x + 5.0f, p.y + 5.0f),
				playerColor);
		}

		// --- Contrôle de zoom : glissière en arc sur la moitié haute du radar.
		// Rail lisse + portion remplie jusqu'au cran courant + 5 encoches de détente
		// + poignée (au lieu des points « constellation » précédents). Piloté par Engine
		// (molette + clic, cf. SetMinimapZoomIndex) ; ici on ne fait que dessiner.
		// Géométrie (centre, rayon d'arc, angles) IDENTIQUE à
		// engine::client::RadarZoomTickPos pour que poignée/encoches coïncident avec le
		// hit-test du clic côté Engine.
		{
			const int zoomIdx = engine::client::ClampZoomIndex(m_minimapZoomIndex);
			const float cx = center.x;
			const float cy = center.y;
			const float rArc = sizePx * 0.5f + 6.0f; // == RadarZoomTickPos
			constexpr float kDegToRad = 3.14159265f / 180.0f;
			constexpr float aStartDeg = 150.0f; // cran 0 (200 m), haut-gauche
			constexpr float aEndDeg = 30.0f;    // cran 4 (1000 m), haut-droite
			const float aCurDeg = aStartDeg - 30.0f * static_cast<float>(zoomIdx);
			// Repère écran (y vers le bas) : point = (cx + r cos θ, cy - r sin θ).
			auto arcPt = [&](float deg, float radius) -> ImVec2 {
				const float t = deg * kDegToRad;
				return ImVec2(cx + radius * std::cos(t), cy - radius * std::sin(t));
			};

			const ImU32 trackCol = IM_COL32(64, 72, 86, 205);
			const ImU32 fillCol = LnTheme::ToU32(LnTheme::kAccent);
			const ImU32 tickCol = IM_COL32(150, 160, 180, 170);
			const ImU32 knobRing = IM_COL32(255, 255, 255, 235);
			constexpr int kSeg = 40;

			// 1) Rail de fond (arc lisse, segments AddLine).
			ImVec2 prev = arcPt(aStartDeg, rArc);
			for (int i = 1; i <= kSeg; ++i)
			{
				const float a = aStartDeg + (aEndDeg - aStartDeg) * (static_cast<float>(i) / static_cast<float>(kSeg));
				const ImVec2 cur = arcPt(a, rArc);
				dl->AddLine(prev, cur, trackCol, 4.0f);
				prev = cur;
			}
			// 2) Portion remplie (de aStart jusqu'au cran courant), teinte accent.
			if (zoomIdx > 0)
			{
				ImVec2 fprev = arcPt(aStartDeg, rArc);
				for (int i = 1; i <= kSeg; ++i)
				{
					const float a = aStartDeg + (aCurDeg - aStartDeg) * (static_cast<float>(i) / static_cast<float>(kSeg));
					const ImVec2 cur = arcPt(a, rArc);
					dl->AddLine(fprev, cur, fillCol, 4.0f);
					fprev = cur;
				}
			}
			// 3) Encoches de détente (petits traits radiaux aux 5 crans).
			for (int t = 0; t < engine::client::kMinimapZoomLevelCount; ++t)
			{
				const float a = aStartDeg - 30.0f * static_cast<float>(t);
				dl->AddLine(arcPt(a, rArc - 3.5f), arcPt(a, rArc + 3.5f), tickCol, 1.5f);
			}
			// 4) Poignée (knob) au cran courant.
			const ImVec2 knob = arcPt(aCurDeg, rArc);
			dl->AddCircleFilled(knob, 5.5f, fillCol);
			dl->AddCircle(knob, 5.5f, knobRing, 20, 1.5f);

			// 5) Valeur numérique du cran courant (« 600 m »), centrée en haut du radar.
			const std::string zoomText =
				std::to_string(static_cast<int>(engine::client::RadiusForZoomIndex(zoomIdx))) + " m";
			const ImVec2 zts = ImGui::CalcTextSize(zoomText.c_str());
			dl->AddText(ImVec2(cx - zts.x * 0.5f, y0 + 3.0f), IM_COL32(220, 225, 235, 235), zoomText.c_str());
		}
	}

	void QuestImGuiRenderer::RenderGiverPanel()
	{
		// PR-B — Masqué tant qu'un dialogue PNJ est actif : dans ce cas
		// l'acceptation/rendu se fait DANS la conversation (DialogueImGuiRenderer).
		// Ce panneau ne subsiste que comme fallback (donneur sans arbre de dialogue).
		if (m_giverPanelSuppressed)
			return;

		const engine::client::UIModel& model = m_uiModelBinding->GetModel();
		const engine::client::UIQuestGiverList& giverList = model.giverList;
		if (giverList.entries.empty())
			return;

		ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.f));
		ImGui::SetNextWindowBgAlpha(0.92f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.92f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("Quetes du PNJ##ln_quest_giver_panel", nullptr, flags))
		{
			for (const engine::client::UIQuestGiverEntry& entry : giverList.entries)
			{
				const bool turnIn = (entry.role == 1);
				const std::string title = m_textCatalog->Title(entry.questId);

				// Titre coloré : doré = à proposer, vert = à rendre.
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(turnIn ? LnTheme::kSuccess : LnTheme::kAccent));
				ImGui::TextUnformatted(title.c_str());
				ImGui::PopStyleColor();

				// Texte spécifique : description à l'offre, texte de clôture au turn-in.
				const std::string body = turnIn
					? m_textCatalog->Completion(entry.questId)
					: m_textCatalog->Description(entry.questId);
				if (!body.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320.0f);
					ImGui::TextUnformatted(body.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}

				// Récompense (surtout parlante au turn-in) : lue sur le modèle.
				for (const engine::client::UIQuestEntry& q : model.quests)
				{
					if (q.questId != entry.questId)
						continue;
					if (q.rewardExperience > 0 || q.rewardGold > 0)
					{
						std::string reward = "Recompense :";
						if (q.rewardExperience > 0)
							reward += "  " + std::to_string(q.rewardExperience) + " XP";
						if (q.rewardGold > 0)
							reward += "  " + std::to_string(q.rewardGold) + " or";
						ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
						ImGui::TextUnformatted(reward.c_str());
						ImGui::PopStyleColor();
					}
					break;
				}

				// role 0 = offer (pas encore acceptée) -> bouton Accepter.
				// role 1 = turnin (étapes remplies, à rendre) -> bouton Terminer.
				if (entry.role == 0)
				{
					char buttonId[160];
					std::snprintf(buttonId, sizeof(buttonId), "Accepter##accept_%s", entry.questId.c_str());
					if (ImGui::SmallButton(buttonId) && m_giverAction)
					{
						m_giverAction(entry.questId, entry.role);
					}
				}
				else
				{
					char buttonId[160];
					std::snprintf(buttonId, sizeof(buttonId), "Terminer##turnin_%s", entry.questId.c_str());
					if (ImGui::SmallButton(buttonId) && m_giverAction)
					{
						m_giverAction(entry.questId, entry.role);
					}
				}
			}
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void QuestImGuiRenderer::BindQuestUi(engine::client::QuestUiPresenter*,
		const engine::client::QuestTextCatalog*,
		const engine::client::UIModelBinding*,
		const engine::core::Config*) {}
	void QuestImGuiRenderer::Render(float, float, bool, bool) {}
}

#endif
