#include "src/world_editor/ui/RichTooltipWidget.h"

#include "src/world_editor/help/HelpContentStore.h"
#include "src/world_editor/help/TooltipDefinition.h"
#include "src/world_editor/modes/EditorMode.h"
#include "src/world_editor/modes/EditorModeRegistry.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::ui
{
#if defined(_WIN32)
	namespace
	{
		/// Rend le contenu d'un tooltip déjà résolu (`def` non-nul). Encapsulé
		/// pour partager la mise en page entre le chemin "found" et un
		/// éventuel chemin debug.
		void RenderTooltipContent(const help::TooltipDefinition& def)
		{
			const bool isAdvanced =
				modes::EditorModeRegistry::Instance().GetCurrentMode()
				== modes::EditorMode::Advanced;

			ImGui::PushTextWrapPos(320.0f);
			ImGui::TextUnformatted(def.label.empty() ? def.id.c_str() : def.label.c_str());
			ImGui::Separator();

			if (isAdvanced && !def.descriptionAdvanced.empty())
			{
				ImGui::TextWrapped("%s", def.descriptionAdvanced.c_str());
			}
			else if (!def.descriptionSimple.empty())
			{
				ImGui::TextWrapped("%s", def.descriptionSimple.c_str());
			}
			else if (!def.descriptionAdvanced.empty())
			{
				// Pas de Simple, fallback Advanced quel que soit le mode.
				ImGui::TextWrapped("%s", def.descriptionAdvanced.c_str());
			}

			const bool hasMeta = !def.defaultValue.empty() || !def.range.empty();
			if (hasMeta)
			{
				ImGui::Separator();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
				if (!def.defaultValue.empty())
					ImGui::Text("Defaut : %s", def.defaultValue.c_str());
				if (!def.range.empty())
					ImGui::Text("Plage  : %s", def.range.c_str());
				ImGui::PopStyleColor();
			}

			if (!def.docSectionId.empty())
			{
				ImGui::Separator();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
				ImGui::TextUnformatted("(F1 pour la doc complete)");
				ImGui::PopStyleColor();
			}
			ImGui::PopTextWrapPos();
		}
	}

	void RichTooltip(const std::string& tooltipId)
	{
		if (tooltipId.empty()) return;
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal
			| ImGuiHoveredFlags_NoSharedDelay))
			return;

		const auto* def = help::HelpContentStore::Instance().FindTooltip(tooltipId);
		ImGui::BeginTooltip();
		if (def != nullptr)
		{
			RenderTooltipContent(*def);
		}
		else
		{
			// Discret : un dev qui hover voit l'id manquant, l'utilisateur
			// final voit juste une bulle quasi-vide.
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::Text("(tooltip manquant : %s)", tooltipId.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::EndTooltip();
	}
#else
	void RichTooltip(const std::string&) {}
#endif
}
