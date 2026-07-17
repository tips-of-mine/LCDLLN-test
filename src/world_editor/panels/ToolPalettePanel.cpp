// ToolPalettePanel — implémentation. Voir ToolPalettePanel.h.

#include "src/world_editor/panels/ToolPalettePanel.h"

#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/ui/ToolPaletteModel.h"
#include "src/world_editor/ui/ToolbarIconAtlas.h"

#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	void ToolPalettePanel::Render()
	{
#if defined(_WIN32)
		if (m_shell == nullptr) return;
		if (!ImGui::Begin(GetName(), nullptr))
		{
			ImGui::End();
			return;
		}

		WorldEditorShell& shell = *m_shell;
		const ActiveTool current = shell.GetActiveTool();

		// Bouton « Aucun outil » en tête (équivalent de l'ex-bouton X).
		{
			const bool active = (current == ActiveTool::None);
			if (ImGui::Selectable(ToolLabelFr(ActiveTool::None), active))
			{
				shell.SetActiveTool(ActiveTool::None);
			}
		}
		ImGui::Separator();

		// Rend un bouton d'outil : pastille couleur (atlas) + libellé FR,
		// surligné si actif, tooltip « libellé (raccourci) » via le registre.
		// Un clic sur l'outil actif le désélectionne (convention menu Outils).
		auto renderToolButton = [&shell, current](ActiveTool tool)
		{
			const ToolIconStyle style = ToolbarIconAtlas::Get(tool);
			const bool active = (current == tool);

			ImGui::PushID(static_cast<int>(tool));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float sq = ImGui::GetTextLineHeight();

			// Le Selectable réserve la place de la pastille par des espaces
			// de tête ; la pastille est dessinée par-dessus juste après.
			const std::string label = std::string("     ") + ToolLabelFr(tool);
			if (ImGui::Selectable(label.c_str(), active))
			{
				shell.SetActiveTool(active ? ActiveTool::None : tool);
			}
			dl->AddRectFilled(
				ImVec2(pos.x + 2.0f, pos.y + 1.0f),
				ImVec2(pos.x + 2.0f + sq, pos.y + 1.0f + sq),
				static_cast<ImU32>(style.bgColorArgb), 3.0f);

			if (ImGui::IsItemHovered())
			{
				// Raccourci depuis le registre d'actions (source unique).
				const actions::EditorAction* a =
					shell.GetActionRegistry().Find(ToolActionId(tool));
				if (a != nullptr && !a->shortcutText.empty())
				{
					ImGui::SetTooltip("%s (%s)", style.tooltipFr, a->shortcutText.c_str());
				}
				else
				{
					ImGui::SetTooltip("%s", style.tooltipFr);
				}
			}
			ImGui::PopID();
		};

		for (const ToolPaletteGroup& group : GetToolPaletteGroups())
		{
			if (!ImGui::CollapsingHeader(group.titleFr, ImGuiTreeNodeFlags_DefaultOpen))
			{
				continue;
			}
			for (ActiveTool tool : group.tools)
			{
				renderToolButton(tool);
			}
		}

		ImGui::End();
#endif
	}
}
