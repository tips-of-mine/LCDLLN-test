// CMANGOS.30 (Phase 5.30 step 3+4) — CinematicImGuiRenderer implementation.

#include "src/client/render/CinematicImGuiRenderer.h"

#include "src/client/cinematics/CinematicUi.h"

#include <algorithm>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Hauteur (en pixels) de chaque barre noire (top + bottom). Choix
		/// esthetique : 12% de la viewport (cinematic letterbox-style).
		constexpr float kBarRatio = 0.12f;
	}

	float CinematicImGuiRenderer::ComputeFadeAlpha(uint64_t currentTimeMs) const
	{
		if (currentTimeMs >= kFadeInMs) return 1.0f;
		return static_cast<float>(currentTimeMs) / static_cast<float>(kFadeInMs);
	}

	void CinematicImGuiRenderer::Render()
	{
		if (m_presenter == nullptr) return;
		if (!m_presenter->IsInitialized()) return;
		const auto& state = m_presenter->GetState();
		if (!state.isPlaying) return;

		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float barH = vpH * kBarRatio;
		const float alpha = std::clamp(ComputeFadeAlpha(state.currentTimeMs), 0.0f, 1.0f);

		ImDrawList* fg = ImGui::GetForegroundDrawList();
		if (fg == nullptr) return;

		// Black bars top + bottom avec fade-in alpha.
		const ImU32 colBlack = IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.0f));
		fg->AddRectFilled(ImVec2(0.f, 0.f),       ImVec2(vpW, barH),         colBlack);
		fg->AddRectFilled(ImVec2(0.f, vpH - barH), ImVec2(vpW, vpH),          colBlack);

		// Texte "Press [Esc] to skip" centre dans la barre du bas.
		// On n'affiche le texte qu'une fois le fade-in termine pour eviter le
		// flash a t=0.
		if (alpha >= 0.95f)
		{
			const char* hint = "Press [Esc] to skip";
			const ImVec2 textSize = ImGui::CalcTextSize(hint);
			const float textX = (vpW - textSize.x) * 0.5f;
			const float textY = vpH - barH + (barH - textSize.y) * 0.5f;
			const ImU32 colText = IM_COL32(220, 220, 220, 220);
			fg->AddText(ImVec2(textX, textY), colText, hint);
		}
	}
}

#else // !_WIN32

namespace engine::render
{
	float CinematicImGuiRenderer::ComputeFadeAlpha(uint64_t /*currentTimeMs*/) const { return 1.0f; }
	void  CinematicImGuiRenderer::Render() {}
}

#endif
