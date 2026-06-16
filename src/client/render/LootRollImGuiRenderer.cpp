// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - LootRollImGuiRenderer implementation.

#include "src/client/render/LootRollImGuiRenderer.h"

#include "src/client/loot/LootRollUi.h"
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

		/// Couleur pour un choice (0=Pass=gris, 1=Greed=vert, 2=Need=violet).
		ImVec4 ChoiceColor(uint8_t choice)
		{
			switch (choice)
			{
			case 0u: return ImVec4(0.55f, 0.55f, 0.60f, 1.f); // Pass : gris
			case 1u: return ImVec4(0.40f, 0.95f, 0.40f, 1.f); // Greed : vert
			case 2u: return ImVec4(0.80f, 0.45f, 0.95f, 1.f); // Need : violet
			default: return ImVec4(0.95f, 0.85f, 0.45f, 1.f);
			}
		}

		/// Retourne le steady_clock now en ms (pour les toasts 5s + countdown).
		uint64_t SteadyNowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	void LootRollImGuiRenderer::Render()
	{
		if (m_presenter == nullptr)
			return;
		if (!m_presenter->IsInitialized())
			return;

		// Le panel n'est rendu que si IsEnabled().
		if (m_enabled)
			RenderMainPanel();

		// Le toast est rendu independamment, si un RollResult recent existe.
		RenderToast();
	}

	void LootRollImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre a gauche-centre, 460x520.
		const float panelW = 460.f;
		const float panelH = 520.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, margin);
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Tirage de butin (F7)##ln_loot_panel", nullptr, flags))
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

			// Top : bouton Simulate (V1 debug).
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1.f));
			ImGui::TextUnformatted("DEBOGAGE (V1) :");
			ImGui::PopStyleColor();
			if (ImGui::Button("Simuler un tirage", ImVec2(-FLT_MIN, 28.f)))
			{
				m_presenter->SimulateRoll();
			}

			ImGui::Separator();

			// Liste des pending rolls.
			if (state.pendingRolls.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.f));
				ImGui::TextWrapped("Aucun tirage en attente.");
				ImGui::PopStyleColor();
			}
			else
			{
				const uint64_t now = SteadyNowMs();
				for (const auto& p : state.pendingRolls)
				{
					ImGui::PushID(static_cast<int>(p.rollId & 0x7FFFFFFFull));

					ImGui::Spacing();
					if (ImGui::BeginChild("##roll_card", ImVec2(0.f, 110.f), true,
						ImGuiWindowFlags_NoScrollbar))
					{
						// Item name + count.
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.7f, 1.f));
						ImGui::Text("%s x%u", p.itemName.c_str(),
							static_cast<unsigned>(p.count));
						ImGui::PopStyleColor();

						// Countdown : "Time left: Xs".
						uint64_t remainMs = 0;
						if (p.expiresAtMs > now)
							remainMs = p.expiresAtMs - now;
						const uint64_t remainSec = remainMs / 1000ull;
						ImGui::Text("Temps restant : %us", static_cast<unsigned>(remainSec));

						// Boutons Need/Greed/Pass (grises si myChoice set).
						const bool clicked = p.myChoice.has_value();
						if (clicked)
						{
							ImGui::PushStyleColor(ImGuiCol_Text, ChoiceColor(*p.myChoice));
							ImGui::Text("Vous avez choisi : %s",
								engine::client::LootChoiceName(*p.myChoice));
							ImGui::PopStyleColor();
						}
						else
						{
							// Need (violet).
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.20f, 0.55f, 1.f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.30f, 0.65f, 1.f));
							if (ImGui::Button("Besoin", ImVec2(120.f, 26.f)))
								m_presenter->Choose(p.rollId, 2u);
							ImGui::PopStyleColor(2);
							ImGui::SameLine();
							// Greed (vert).
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.20f, 1.f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.65f, 0.30f, 1.f));
							if (ImGui::Button("Cupidite", ImVec2(120.f, 26.f)))
								m_presenter->Choose(p.rollId, 1u);
							ImGui::PopStyleColor(2);
							ImGui::SameLine();
							// Pass (gris).
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.40f, 0.45f, 1.f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.50f, 0.55f, 1.f));
							if (ImGui::Button("Passer", ImVec2(120.f, 26.f)))
								m_presenter->Choose(p.rollId, 0u);
							ImGui::PopStyleColor(2);
						}
					}
					ImGui::EndChild();

					ImGui::PopID();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void LootRollImGuiRenderer::RenderToast()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastResultTimeMs.has_value())
			return;

		// Toast actif 5s apres reception.
		constexpr uint64_t kToastDurationMs = 5000ull;
		const uint64_t nowSteady = SteadyNowMs();
		if (nowSteady < *state.lastResultTimeMs)
			return;
		const uint64_t age = nowSteady - *state.lastResultTimeMs;
		if (age > kToastDurationMs)
			return;

		// Compose le texte du toast. Si winnerName vide => "Personne (tous Pass)".
		char buf[256]{};
		if (state.lastResultWinnerName.empty())
		{
			std::snprintf(buf, sizeof(buf),
				"Personne n'a remporte %s x%u (tous Pass)",
				state.lastResultItemName.c_str(),
				static_cast<unsigned>(state.lastResultCount));
		}
		else
		{
			std::snprintf(buf, sizeof(buf),
				"%s a gagne %s x%u (%s avec roll %u)",
				state.lastResultWinnerName.c_str(),
				state.lastResultItemName.c_str(),
				static_cast<unsigned>(state.lastResultCount),
				engine::client::LootChoiceName(state.lastResultWinnerChoice),
				static_cast<unsigned>(state.lastResultWinnerRoll));
		}

		// Geometrie toast : bottom-right, 380x60, marge 16.
		const float toastW = 380.f;
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

		if (ImGui::Begin("##ln_loot_toast", nullptr, toastFlags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ChoiceColor(state.lastResultWinnerChoice));
			ImGui::TextWrapped("%s", buf);
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}
}

#else // !_WIN32

namespace engine::render
{
	void LootRollImGuiRenderer::Render()           {}
	void LootRollImGuiRenderer::RenderMainPanel()  {}
	void LootRollImGuiRenderer::RenderToast()      {}
}

#endif
