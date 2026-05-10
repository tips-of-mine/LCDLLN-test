// CMANGOS.42 (Phase 4.42 step 3+4) — WeatherImGuiRenderer implementation.

#include "src/client/render/WeatherImGuiRenderer.h"

#include "src/client/weather/WeatherUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

		/// Couleur ImGui par WeatherKind pour le texte (intensifie le visuel
		/// de la liste / HUD). Inputs invalides -> gris.
		ImVec4 KindColor(uint8_t kind)
		{
			switch (kind)
			{
			case 0: return ImVec4(1.0f, 0.95f, 0.40f, 1.f); // Clear -> jaune.
			case 1: return ImVec4(0.40f, 0.65f, 1.00f, 1.f); // Rain -> bleu.
			case 2: return ImVec4(0.92f, 0.95f, 1.00f, 1.f); // Snow -> blanc.
			case 3: return ImVec4(0.75f, 0.40f, 0.95f, 1.f); // Storm -> violet.
			case 4: return ImVec4(0.95f, 0.65f, 0.30f, 1.f); // Sandstorm -> orange.
			case 5: return ImVec4(0.60f, 0.60f, 0.65f, 1.f); // Fog -> gris.
			default: return ImVec4(0.7f, 0.7f, 0.7f, 1.f);
			}
		}

		/// Petit "icone" textuel ASCII pour le HUD (pas d'emoji, MSVC
		/// strict + Arial dans editor monde sans certains glyphs).
		const char* KindIcon(uint8_t kind)
		{
			switch (kind)
			{
			case 0: return "[*]";  // Clear (etoile / soleil).
			case 1: return "[/]";  // Rain (gouttes obliques).
			case 2: return "[#]";  // Snow (flocon).
			case 3: return "[!]";  // Storm.
			case 4: return "[~]";  // Sandstorm.
			case 5: return "[=]";  // Fog (brume).
			default: return "[?]";
			}
		}
	}

	void WeatherImGuiRenderer::Render()
	{
		if (m_presenter == nullptr)
			return;
		if (!m_presenter->IsInitialized())
			return;

		// Le panel n'est rendu que si IsEnabled().
		if (m_enabled)
			RenderMainPanel();

		// Le HUD est rendu independamment, si activeZoneId set.
		RenderHud();
	}

	void WeatherImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 460x500.
		const float panelW = 460.f;
		const float panelH = 500.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - panelW - margin);
		// Decale un peu plus bas pour ne pas chevaucher avec le HUD top-right.
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Weather##ln_weather_panel", nullptr, flags))
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

			// Liste des zones.
			if (!state.zonesLoaded || state.zones.empty())
			{
				if (ImGui::Button("Charger les zones meteo", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->RequestList();
				if (state.zonesLoaded && state.zones.empty())
				{
					ImGui::TextWrapped("Aucune zone meteo.");
				}
			}
			else
			{
				ImGui::TextUnformatted("Zones meteo :");
				ImGui::Separator();

				for (const auto& z : state.zones)
				{
					ImGui::PushID(static_cast<int>(z.zoneId));

					// Header de zone : icone + nom + (zone N).
					ImGui::PushStyleColor(ImGuiCol_Text, KindColor(z.kind));
					ImGui::Text("%s %s", KindIcon(z.kind), z.name.c_str());
					ImGui::PopStyleColor();

					// Sous-ligne : kind name + intensity %.
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.f));
					ImGui::Text("Etat : %s  -  Intensite : %.0f%%",
						engine::client::WeatherKindName(z.kind),
						static_cast<double>(z.intensity * 100.0f));
					ImGui::PopStyleColor();

					// Intensity bar.
					const float pct = std::min(1.f, std::max(0.f, z.intensity));
					ImGui::ProgressBar(pct, ImVec2(-FLT_MIN, 14.f));

					// Boutons : Set Active / Subscribe / Unsubscribe.
					const bool isActive = state.activeZoneId.has_value()
						&& *state.activeZoneId == z.zoneId;
					if (isActive)
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.30f, 1.f));
						if (ImGui::Button("Active (HUD)##active", ImVec2(140.f, 24.f)))
							m_presenter->ClearActiveZone();
						ImGui::PopStyleColor();
					}
					else
					{
						if (ImGui::Button("Set Active##active", ImVec2(140.f, 24.f)))
							m_presenter->SetActiveZone(z.zoneId);
					}

					ImGui::SameLine();
					const bool isSubscribed = state.subscribedZones.count(z.zoneId) > 0u;
					if (isSubscribed)
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.f));
						if (ImGui::Button("Se desabonner##sub", ImVec2(160.f, 24.f)))
							m_presenter->Unsubscribe(z.zoneId);
						ImGui::PopStyleColor();
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.30f, 1.f));
						if (ImGui::Button("S'abonner##sub", ImVec2(160.f, 24.f)))
							m_presenter->Subscribe(z.zoneId);
						ImGui::PopStyleColor();
					}

					ImGui::Separator();
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

	void WeatherImGuiRenderer::RenderHud()
	{
		if (m_presenter == nullptr)
			return;
		if (!m_presenter->IsInitialized())
			return;

		const auto& state = m_presenter->GetState();
		if (!state.activeZoneId.has_value())
			return;

		const uint32_t zid = *state.activeZoneId;

		// Cherche le nom de la zone dans le cache. Si absent, "Zone <id>".
		std::string zoneName;
		for (const auto& z : state.zones)
		{
			if (z.zoneId == zid)
			{
				zoneName = z.name;
				break;
			}
		}
		if (zoneName.empty())
		{
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "Zone %u", static_cast<unsigned>(zid));
			zoneName = buf;
		}

		// Geometrie HUD : top-right, 240x70, hors viewport border de 16px.
		const float hudW = 240.f;
		const float hudH = 70.f;
		const float margin = 16.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float posX = std::max(0.f, vpW - hudW - margin);
		const float posY = margin;

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(hudW, hudH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.45f);

		const ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing;

		if (ImGui::Begin("##ln_weather_hud", nullptr, hudFlags))
		{
			// Ligne 1 : icone (colore) + nom de zone.
			ImGui::PushStyleColor(ImGuiCol_Text, KindColor(state.activeKind));
			ImGui::Text("%s", KindIcon(state.activeKind));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
			ImGui::TextUnformatted(zoneName.c_str());
			ImGui::PopStyleColor();

			// Ligne 2 : kind name + intensity %.
			ImGui::PushStyleColor(ImGuiCol_Text, KindColor(state.activeKind));
			ImGui::Text("%s  %.0f%%",
				engine::client::WeatherKindName(state.activeKind),
				static_cast<double>(state.activeIntensity * 100.0f));
			ImGui::PopStyleColor();

			// Ligne 3 : intensity bar.
			const float pct = std::min(1.f, std::max(0.f, state.activeIntensity));
			ImGui::ProgressBar(pct, ImVec2(-FLT_MIN, 8.f));
		}
		ImGui::End();
	}
}

#else // !_WIN32

namespace engine::render
{
	void WeatherImGuiRenderer::Render()           {}
	void WeatherImGuiRenderer::RenderMainPanel()  {}
	void WeatherImGuiRenderer::RenderHud()        {}
}

#endif
