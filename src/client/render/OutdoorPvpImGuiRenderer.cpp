// CMANGOS.36 (Phase 5.36 step 3+4) — OutdoorPvpImGuiRenderer implementation.

#include "src/client/render/OutdoorPvpImGuiRenderer.h"

#include "src/client/outdoorpvp/OutdoorPvpUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

		/// Libelle FR pour une faction.
		const char* FactionLabel(uint8_t fac)
		{
			switch (fac)
			{
			case 0:    return "Alliance";
			case 1:    return "Horde";
			case 0xFF: return "Neutre";
			default:   return "?";
			}
		}

		/// Couleur ImGui par faction (pour TextColored / ColorButton).
		ImVec4 FactionColor(uint8_t fac)
		{
			switch (fac)
			{
			case 0:    return ImVec4(0.4f, 0.7f, 1.f, 1.f);  // Alliance bleu
			case 1:    return ImVec4(1.f, 0.4f, 0.4f, 1.f);  // Horde rouge
			case 0xFF: return ImVec4(0.7f, 0.7f, 0.7f, 1.f); // Neutre gris
			default:   return ImVec4(0.85f, 0.85f, 0.85f, 1.f);
			}
		}

		/// Duree en ms depuis steady_clock::now().
		uint64_t NowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	void OutdoorPvpImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled)
			return;
		if (!m_presenter->IsInitialized())
			return;
		RenderMainPanel();
	}

	void OutdoorPvpImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 460x560.
		const float panelW = 460.f;
		const float panelH = 560.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - panelW - margin);
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Outdoor PvP##ln_outdoorpvp_panel", nullptr, flags))
		{
			// Erreur transitoire (rouge).
			if (!state.lastErrorText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastErrorText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}
			// Info transitoire (vert leger).
			if (!state.lastInfoText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastInfoText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Toast transitoire 5 sec apres une capture finie.
			if (state.lastCaptureCompletedTimeMs.has_value())
			{
				const uint64_t now = NowMs();
				const uint64_t completed = *state.lastCaptureCompletedTimeMs;
				if (now > completed && (now - completed) < 5000u)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, FactionColor(state.lastCaptureNewOwner));
					char buf[256]{};
					std::snprintf(buf, sizeof(buf),
						"Zone %u : objectif %u capture par %s ! (Score %u-%u)",
						static_cast<unsigned>(state.lastCaptureZoneId),
						static_cast<unsigned>(state.lastCaptureObjectiveId),
						FactionLabel(state.lastCaptureNewOwner),
						static_cast<unsigned>(state.lastCaptureAllianceScore),
						static_cast<unsigned>(state.lastCaptureHordeScore));
					ImGui::TextWrapped("%s", buf);
					ImGui::PopStyleColor();
					ImGui::Separator();
				}
			}

			// Capture en cours : bar de progression centrale.
			if (state.capturingZoneId.has_value() && state.capturingObjectiveId.has_value())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.20f, 1.f));
				char head[160]{};
				std::snprintf(head, sizeof(head),
					"Capture en cours (zone %u, objectif %u, %s)",
					static_cast<unsigned>(*state.capturingZoneId),
					static_cast<unsigned>(*state.capturingObjectiveId),
					FactionLabel(state.capturingByFaction));
				ImGui::TextUnformatted(head);
				ImGui::PopStyleColor();
				const float pct = std::min(1.f, static_cast<float>(state.capturingPct) / 100.f);
				ImGui::ProgressBar(pct, ImVec2(-FLT_MIN, 18.f));
				ImGui::Separator();
			}

			// Liste des zones.
			if (!state.zonesLoaded || state.zones.empty())
			{
				if (ImGui::Button("Charger les zones", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->RequestList();
				if (state.zonesLoaded && state.zones.empty())
				{
					ImGui::TextWrapped("Aucune zone contestee.");
				}
			}
			else
			{
				ImGui::TextUnformatted("Zones contestees :");
				ImGui::Separator();

				for (const auto& z : state.zones)
				{
					ImGui::PushID(static_cast<int>(z.zoneId));

					char header[200]{};
					std::snprintf(header, sizeof(header), "%s (zone %u)##zone_hdr",
						z.name.c_str(), static_cast<unsigned>(z.zoneId));
					if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
					{
						// Scores.
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.f, 1.f));
						ImGui::Text("Alliance : %u", static_cast<unsigned>(z.allianceScore));
						ImGui::PopStyleColor();
						ImGui::SameLine();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
						ImGui::Text("Horde : %u", static_cast<unsigned>(z.hordeScore));
						ImGui::PopStyleColor();

						// Bouton Subscribe/Unsubscribe.
						const bool isSubscribed = state.subscribedZones.count(z.zoneId) > 0u;
						if (isSubscribed)
						{
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.f));
							if (ImGui::Button("Se desabonner", ImVec2(-FLT_MIN, 24.f)))
								m_presenter->Unsubscribe(z.zoneId);
							ImGui::PopStyleColor();
						}
						else
						{
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.30f, 1.f));
							if (ImGui::Button("S'abonner", ImVec2(-FLT_MIN, 24.f)))
								m_presenter->Subscribe(z.zoneId);
							ImGui::PopStyleColor();
						}

						// Liste des objectifs.
						ImGui::Spacing();
						ImGui::TextUnformatted("Objectifs :");
						ImGui::Separator();

						const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
							| ImGuiTableFlags_RowBg;
						if (ImGui::BeginTable("##ln_outdoorpvp_objs", 4, tableFlags))
						{
							ImGui::TableSetupColumn("ID",      ImGuiTableColumnFlags_WidthFixed, 40.f);
							ImGui::TableSetupColumn("Owner",   ImGuiTableColumnFlags_WidthFixed, 70.f);
							ImGui::TableSetupColumn("Capture", ImGuiTableColumnFlags_WidthFixed, 80.f);
							ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthStretch);
							ImGui::TableHeadersRow();

							for (const auto& obj : z.objectives)
							{
								ImGui::TableNextRow();
								ImGui::PushID(static_cast<int>(obj.objectiveId));

								ImGui::TableSetColumnIndex(0);
								ImGui::Text("%u", static_cast<unsigned>(obj.objectiveId));

								ImGui::TableSetColumnIndex(1);
								ImGui::PushStyleColor(ImGuiCol_Text, FactionColor(obj.owner));
								ImGui::TextUnformatted(FactionLabel(obj.owner));
								ImGui::PopStyleColor();

								ImGui::TableSetColumnIndex(2);
								if (obj.capturingBy != 0xFFu && obj.capturePct > 0u)
								{
									ImGui::PushStyleColor(ImGuiCol_Text, FactionColor(obj.capturingBy));
									ImGui::Text("%u%%", static_cast<unsigned>(obj.capturePct));
									ImGui::PopStyleColor();
								}
								else
								{
									ImGui::TextUnformatted("-");
								}

								ImGui::TableSetColumnIndex(3);
								ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.70f, 1.f));
								if (ImGui::Button("Cap. Alliance", ImVec2(110.f, 22.f)))
									m_presenter->StartCapture(z.zoneId, obj.objectiveId, 0u);
								ImGui::PopStyleColor();
								ImGui::SameLine();
								ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 1.f));
								if (ImGui::Button("Cap. Horde", ImVec2(100.f, 22.f)))
									m_presenter->StartCapture(z.zoneId, obj.objectiveId, 1u);
								ImGui::PopStyleColor();

								ImGui::PopID();
							}
							ImGui::EndTable();
						}
					}

					ImGui::PopID();
				}

				ImGui::Spacing();
				if (ImGui::Button("Rafraichir la liste", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->RequestList();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void OutdoorPvpImGuiRenderer::Render()              {}
	void OutdoorPvpImGuiRenderer::RenderMainPanel()     {}
}

#endif
