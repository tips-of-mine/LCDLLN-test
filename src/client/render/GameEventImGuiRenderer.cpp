// CMANGOS.31 (Phase 5.31 step 3+4) — GameEventImGuiRenderer implementation.

#include "src/client/render/GameEventImGuiRenderer.h"

#include "src/client/events/GameEventUi.h"
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

		/// Couleur ImGui par etat (Active=vert, Inactive=gris). Inputs
		/// invalides -> gris fonce.
		ImVec4 StateColor(uint8_t state)
		{
			switch (state)
			{
			case 0: return ImVec4(0.60f, 0.60f, 0.65f, 1.f); // Inactive -> gris.
			case 1: return ImVec4(0.40f, 0.95f, 0.40f, 1.f); // Active -> vert.
			default: return ImVec4(0.50f, 0.40f, 0.40f, 1.f);
			}
		}

		/// Petit "icone" textuel ASCII pour la liste (pas d'emoji, MSVC
		/// strict + Arial dans editor monde sans certains glyphs).
		const char* StateIcon(uint8_t state)
		{
			switch (state)
			{
			case 0: return "[ ]";  // Inactive.
			case 1: return "[*]";  // Active.
			default: return "[?]";
			}
		}

		/// Retourne le system_clock now en ms epoch (pour calculer les
		/// countdowns vis-a-vis des untilTsMs / startTsMs).
		uint64_t WallNowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}

		/// Retourne le steady_clock now en ms (pour les toasts 5s).
		uint64_t SteadyNowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	void GameEventImGuiRenderer::Render()
	{
		if (m_presenter == nullptr)
			return;
		if (!m_presenter->IsInitialized())
			return;

		// Le panel n'est rendu que si IsEnabled().
		if (m_enabled)
			RenderMainPanel();

		// Le toast est rendu independamment, si un StateChange recent existe.
		RenderToast();
	}

	void GameEventImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 480x520.
		const float panelW = 480.f;
		const float panelH = 520.f;
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
		if (ImGui::Begin("Game Events##ln_gameevents_panel", nullptr, flags))
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

			// Bouton Subscribe / Unsubscribe global au top.
			if (state.subscribed)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.f));
				if (ImGui::Button("Se desabonner des events", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->Unsubscribe();
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.30f, 1.f));
				if (ImGui::Button("S'abonner aux push d'events", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->Subscribe();
				ImGui::PopStyleColor();
			}
			ImGui::Separator();

			// Liste des events.
			if (!state.eventsLoaded || state.events.empty())
			{
				if (ImGui::Button("Charger les events", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->RequestList();
				if (state.eventsLoaded && state.events.empty())
				{
					ImGui::TextWrapped("Aucun event saisonnier.");
				}
			}
			else
			{
				ImGui::TextUnformatted("Events saisonniers :");
				ImGui::Separator();

				const uint64_t nowMs = WallNowMs();

				for (const auto& ev : state.events)
				{
					ImGui::PushID(static_cast<int>(ev.eventId));

					// Header : icone d'etat + nom + (id).
					ImGui::PushStyleColor(ImGuiCol_Text, StateColor(ev.state));
					ImGui::Text("%s %s", StateIcon(ev.state), ev.name.c_str());
					ImGui::PopStyleColor();

					// Sous-ligne : etat textuel + countdown (ends in / starts in).
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.f));
					ImGui::Text("Etat : %s", engine::client::GameEventStateName(ev.state));
					ImGui::PopStyleColor();

					// Countdown : si untilTsMs reçu via push, calcule
					// untilTsMs - nowMs ; sinon, fallback sur startTsMs/durationMs.
					uint64_t targetTsMs = ev.untilTsMs;
					if (targetTsMs == 0u)
					{
						// Fallback : derive depuis state + startTsMs +
						// durationMs + recurMs.
						if (ev.state == 1u)
						{
							// Active : derive la fin de l'occurrence courante.
							if (ev.recurMs == 0u)
							{
								targetTsMs = ev.startTsMs + ev.durationMs;
							}
							else if (nowMs >= ev.startTsMs)
							{
								const uint64_t offset       = (nowMs - ev.startTsMs) % ev.recurMs;
								const uint64_t cycleStartMs = nowMs - offset;
								targetTsMs = cycleStartMs + ev.durationMs;
							}
						}
						else
						{
							// Inactive : derive le prochain debut.
							if (nowMs < ev.startTsMs)
							{
								targetTsMs = ev.startTsMs;
							}
							else if (ev.recurMs != 0u)
							{
								const uint64_t offset       = (nowMs - ev.startTsMs) % ev.recurMs;
								const uint64_t cycleStartMs = nowMs - offset;
								targetTsMs = cycleStartMs + ev.recurMs;
							}
						}
					}

					if (targetTsMs > 0u && targetTsMs > nowMs)
					{
						const int64_t deltaMs = static_cast<int64_t>(targetTsMs - nowMs);
						const std::string rel = engine::client::FormatRelativeTime(deltaMs);
						const char* prefix = (ev.state == 1u) ? "Termine dans" : "Demarre dans";
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 1.0f, 1.f));
						ImGui::Text("%s : %s", prefix, rel.c_str());
						ImGui::PopStyleColor();
					}
					else if (targetTsMs == 0u && ev.state == 0u && ev.recurMs == 0u)
					{
						// One-shot termine.
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.f));
						ImGui::TextUnformatted("Termine.");
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

	void GameEventImGuiRenderer::RenderToast()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastChangeTimeMs.has_value())
			return;

		// Toast actif 5s apres reception.
		constexpr uint64_t kToastDurationMs = 5000ull;
		const uint64_t nowSteady = SteadyNowMs();
		if (nowSteady < *state.lastChangeTimeMs)
			return;
		const uint64_t age = nowSteady - *state.lastChangeTimeMs;
		if (age > kToastDurationMs)
			return;

		// Cherche le nom de l'event dans le cache. Fallback "Event N".
		std::string eventName;
		for (const auto& ev : state.events)
		{
			if (ev.eventId == state.lastChangeEventId)
			{
				eventName = ev.name;
				break;
			}
		}
		if (eventName.empty())
		{
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "Event %u",
				static_cast<unsigned>(state.lastChangeEventId));
			eventName = buf;
		}

		// Texte + couleur selon nouvel etat.
		const bool becameActive = (state.lastChangeNewState == 1u);
		std::string text = eventName + (becameActive ? " a commence !" : " est termine.");
		ImVec4 textColor = becameActive
			? ImVec4(0.40f, 0.95f, 0.40f, 1.f) // vert
			: ImVec4(0.95f, 0.65f, 0.30f, 1.f); // orange

		// Geometrie toast : bottom-right, 320x60, marge 16.
		const float toastW = 320.f;
		const float toastH = 60.f;
		const float margin = 16.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - toastW - margin);
		const float posY = std::max(0.f, vpH - toastH - margin);

		// Fade out sur les 1000 dernieres ms.
		float alpha = 0.95f;
		if (age > kToastDurationMs - 1000ull)
		{
			const float remain = static_cast<float>(kToastDurationMs - age) / 1000.0f;
			alpha = std::max(0.0f, std::min(0.95f, remain * 0.95f));
		}

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(toastW, toastH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(alpha);

		const ImGuiWindowFlags toastFlags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoInputs;

		if (ImGui::Begin("##ln_gameevents_toast", nullptr, toastFlags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}
}

#else // !_WIN32

namespace engine::render
{
	void GameEventImGuiRenderer::Render()           {}
	void GameEventImGuiRenderer::RenderMainPanel()  {}
	void GameEventImGuiRenderer::RenderToast()      {}
}

#endif
