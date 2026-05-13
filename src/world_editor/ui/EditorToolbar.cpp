#include "src/world_editor/ui/EditorToolbar.h"

#include "src/world_editor/ui/ToolbarIconAtlas.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	EditorToolbar::EditorToolbar(WorldEditorShell& shell)
		: m_shell(shell)
	{
		m_orderedTools = {
			ActiveTool::TerrainSculpt,
			ActiveTool::TerrainStamp,
			ActiveTool::SplatPaint,
			ActiveTool::Lake,
			ActiveTool::River,
			ActiveTool::MountainRange,
			ActiveTool::ValleyChain,
			ActiveTool::RiverNetwork,     // M100.36
			ActiveTool::Coastline,        // M100.37
			ActiveTool::HydraulicErosion,   // M100.38
			ActiveTool::ThermalWindErosion, // M100.39 (clôt Phase 2.5)
			ActiveTool::Cave,               // M100.40 (démarre Phase 11)
			ActiveTool::Overhang,           // M100.41
			ActiveTool::Arch,               // M100.42
		};
	}

	ToolbarLayout EditorToolbar::BuildLayout(float viewportWidthPx,
		float menuBarHeightPx) const
	{
		ToolbarLayout layout;
		layout.toolbarY      = menuBarHeightPx;
		layout.toolbarHeight = kToolbarHeightPx;
		layout.toolbarWidth  = viewportWidthPx;

		// Le bouton X de désélection est ajouté en queue : (N outils) + 1.
		const size_t totalButtons = m_orderedTools.size() + 1u;
		layout.buttons.reserve(totalButtons);

		float cursorX = kToolbarPaddingPx;
		const float cursorY = layout.toolbarY + kToolbarPaddingPx;
		for (ActiveTool tool : m_orderedTools)
		{
			ToolbarButtonRect r;
			r.x = cursorX;
			r.y = cursorY;
			r.width = kButtonSizePx;
			r.height = kButtonSizePx;
			r.tool = tool;
			layout.buttons.push_back(r);
			cursorX += kButtonSizePx + kButtonSpacingPx;
		}

		// Bouton de désélection (X) — un petit espace supplémentaire avant
		// pour le séparer visuellement des autres outils.
		cursorX += kButtonSpacingPx;
		ToolbarButtonRect deselect;
		deselect.x = cursorX;
		deselect.y = cursorY;
		deselect.width = kButtonSizePx;
		deselect.height = kButtonSizePx;
		deselect.tool = ActiveTool::None;
		layout.buttons.push_back(deselect);

		return layout;
	}

	bool EditorToolbar::HitTest(const ToolbarLayout& layout,
		float mouseX, float mouseY, size_t& outButtonIndex)
	{
		for (size_t i = 0; i < layout.buttons.size(); ++i)
		{
			const ToolbarButtonRect& r = layout.buttons[i];
			if (mouseX >= r.x && mouseX < r.x + r.width &&
				mouseY >= r.y && mouseY < r.y + r.height)
			{
				outButtonIndex = i;
				return true;
			}
		}
		return false;
	}

	void EditorToolbar::HandleClick(const ToolbarLayout& layout,
		size_t buttonIndex)
	{
		if (buttonIndex >= layout.buttons.size()) return;
		m_shell.SetActiveTool(layout.buttons[buttonIndex].tool);
	}

#if defined(_WIN32)
	void EditorToolbar::Render()
	{
		// Construction de la fenêtre toolbar : ancrée sous le menu bar,
		// largeur = viewport principal, hauteur fixe. Le menu bar ImGui
		// occupe `ImGui::GetFrameHeight()` pixels en haut du viewport
		// principal.
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const float menuBarH = ImGui::GetFrameHeight();
		const ImVec2 toolbarPos{ vp->WorkPos.x, vp->WorkPos.y };
		const ImVec2 toolbarSize{ vp->WorkSize.x, kToolbarHeightPx };

		ImGui::SetNextWindowPos(toolbarPos);
		ImGui::SetNextWindowSize(toolbarSize);

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoBringToFrontOnFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
			ImVec2(kToolbarPaddingPx, kToolbarPaddingPx));
		if (!ImGui::Begin("##EditorToolbar", nullptr, flags))
		{
			ImGui::End();
			ImGui::PopStyleVar(3);
			return;
		}

		const ActiveTool current = m_shell.GetActiveTool();
		const ImU32 activeBg = IM_COL32(255, 196, 64, 96);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 windowOrigin = ImGui::GetCursorScreenPos();

		float cursorX = 0.0f;
		for (size_t i = 0; i < m_orderedTools.size(); ++i)
		{
			const ActiveTool tool = m_orderedTools[i];
			const ToolIconStyle style = ToolbarIconAtlas::Get(tool);

			ImGui::PushID(static_cast<int>(i));
			ImGui::SetCursorPosX(cursorX);
			ImGui::SetCursorPosY(0.0f);

			const ImVec2 buttonScreenMin{
				windowOrigin.x + cursorX,
				windowOrigin.y };
			const ImVec2 buttonScreenMax{
				buttonScreenMin.x + kButtonSizePx,
				buttonScreenMin.y + kButtonSizePx };

			// Background du bouton : ambre si actif, sinon couleur de
			// l'icône (placeholder). Le rect est dessiné avant
			// l'InvisibleButton pour qu'ImGui ne mange pas le rect.
			const bool isActive = (current == tool);
			const ImU32 baseColor = isActive
				? activeBg
				: static_cast<ImU32>(style.bgColorArgb);
			dl->AddRectFilled(buttonScreenMin, buttonScreenMax, baseColor, 4.0f);

			// Lettre centrale (placeholder).
			const ImVec2 textSize = ImGui::CalcTextSize(style.letter);
			const ImVec2 textPos{
				buttonScreenMin.x + (kButtonSizePx - textSize.x) * 0.5f,
				buttonScreenMin.y + (kButtonSizePx - textSize.y) * 0.5f };
			dl->AddText(textPos, IM_COL32_WHITE, style.letter);

			// Bordure (active = jaune, sinon gris foncé).
			const ImU32 border = isActive
				? IM_COL32(255, 196, 64, 220)
				: IM_COL32(40, 40, 40, 200);
			dl->AddRect(buttonScreenMin, buttonScreenMax, border, 4.0f, 0, 1.5f);

			// InvisibleButton occupe exactement le rect du bouton.
			ImGui::InvisibleButton("##btn",
				ImVec2(kButtonSizePx, kButtonSizePx));
			const bool clicked = ImGui::IsItemActivated() ||
				(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left));
			if (clicked && style.enabled)
			{
				m_shell.SetActiveTool(tool);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", style.tooltipFr);
			}

			ImGui::PopID();
			cursorX += kButtonSizePx + kButtonSpacingPx;
			ImGui::SameLine(); // s'aligne pour InvisibleButton suivant
		}

		// Bouton X de désélection. Petit espace avant.
		cursorX += kButtonSpacingPx;
		{
			const ToolIconStyle style = ToolbarIconAtlas::GetDeselect();
			ImGui::PushID("deselect");

			const ImVec2 buttonScreenMin{
				windowOrigin.x + cursorX,
				windowOrigin.y };
			const ImVec2 buttonScreenMax{
				buttonScreenMin.x + kButtonSizePx,
				buttonScreenMin.y + kButtonSizePx };

			const bool isActive = (current == ActiveTool::None);
			const ImU32 baseColor = isActive
				? activeBg
				: static_cast<ImU32>(style.bgColorArgb);
			dl->AddRectFilled(buttonScreenMin, buttonScreenMax, baseColor, 4.0f);
			const ImVec2 textSize = ImGui::CalcTextSize(style.letter);
			const ImVec2 textPos{
				buttonScreenMin.x + (kButtonSizePx - textSize.x) * 0.5f,
				buttonScreenMin.y + (kButtonSizePx - textSize.y) * 0.5f };
			dl->AddText(textPos, IM_COL32_WHITE, style.letter);
			dl->AddRect(buttonScreenMin, buttonScreenMax,
				IM_COL32(80, 0, 0, 220), 4.0f, 0, 1.5f);

			ImGui::SetCursorPosX(cursorX);
			ImGui::SetCursorPosY(0.0f);
			ImGui::InvisibleButton("##xbtn",
				ImVec2(kButtonSizePx, kButtonSizePx));
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				m_shell.SetActiveTool(ActiveTool::None);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", style.tooltipFr);
			}

			ImGui::PopID();
		}

		ImGui::End();
		ImGui::PopStyleVar(3);

		(void)menuBarH; // toolbarPos est calculé via vp->WorkPos qui inclut le menu bar
	}
#endif
}
