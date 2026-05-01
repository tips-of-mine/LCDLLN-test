#include "engine/render/EditorHubImGuiRenderer.h"

#include "engine/editor/EditorMode.h"
#include "engine/render/LnTheme.h"

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }
	}

	void EditorHubImGuiRenderer::Render(float viewportW, float viewportH)
	{
		(void)viewportW;
		(void)viewportH;
		if (m_editor == nullptr)
			return;

		// Ancrage haut-gauche, marge 12 px ; auto-resize, non-modal, pas de move.
		const float margin = 12.f;
		ImGui::SetNextWindowPos(ImVec2(margin, margin));
		ImGui::SetNextWindowBgAlpha(0.78f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_AlwaysAutoResize;

		if (ImGui::Begin("##ln_editor_hub", nullptr, flags))
		{
			// Header LCDLLN Editor + indicator dirty.
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
			if (m_editor->IsDirty())
				ImGui::TextUnformatted("[LCDLLN Editor] *dirty*");
			else
				ImGui::TextUnformatted("[LCDLLN Editor]");
			ImGui::PopStyleColor();

			ImGui::Separator();

			// Le titre composé contient déjà Scene/Inspector/AssetBrowser.
			// On l'affiche tel quel — cohérence garantie avec l'ancien window-title.
			const std::string& title = m_editor->GetHubTitle();
			if (!title.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
				ImGui::TextWrapped("%s", title.c_str());
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::TextUnformatted("(en attente du premier RefreshShell...)");
				ImGui::PopStyleColor();
			}
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void EditorHubImGuiRenderer::Render(float, float) {}
}

#endif
