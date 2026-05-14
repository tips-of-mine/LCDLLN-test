#include "src/world_editor/ui/PresetDropdownWidget.h"

#include "src/world_editor/presets/ToolPresetRegistry.h"
#include "src/world_editor/prefs/UserPrefsStore.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <string>

namespace engine::editor::world::ui
{
	void RenderPresetDropdown(const char* toolId,
		const std::function<void(const presets::ToolPreset&)>& onApply)
	{
#if defined(_WIN32)
		if (toolId == nullptr) return;
		const std::string toolKey(toolId);

		auto& registry = presets::ToolPresetRegistry::Instance();
		const std::vector<presets::ToolPreset>& list =
			registry.GetPresetsForTool(toolKey);
		if (list.empty())
		{
			// Outil sans fichier de presets : rien à afficher (cas normal
			// pendant la migration Phase B des autres outils).
			return;
		}

		auto& prefs = prefs::UserPrefsStore::Instance();

		// Sélection courante : prefs > defaultPreset catalogue > première.
		std::string selectedId = prefs.GetLastPresetForTool(toolKey);
		if (selectedId.empty())
		{
			selectedId = registry.GetDefaultPresetId(toolKey);
		}
		if (selectedId.empty())
		{
			selectedId = list.front().id;
		}

		// Libellé affiché dans le combo.
		const presets::ToolPreset* selected = registry.FindPreset(toolKey, selectedId);
		const char* previewLabel = (selected != nullptr)
			? selected->displayName.c_str()
			: "(preset)";

		ImGui::TextUnformatted("Preset");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::BeginCombo("##tool_preset", previewLabel))
		{
			for (const presets::ToolPreset& preset : list)
			{
				const bool isSelected = (preset.id == selectedId);
				if (ImGui::Selectable(preset.displayName.c_str(), isSelected))
				{
					prefs.SetLastPresetForTool(toolKey, preset.id);
					if (onApply) onApply(preset);
				}
				if (!preset.description.empty() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", preset.description.c_str());
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// Icône info : description du preset courant en tooltip.
		if (selected != nullptr && !selected->description.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", selected->description.c_str());
			}
		}
		ImGui::Separator();
#else
		(void)toolId;
		(void)onApply;
#endif
	}
}
