// M101.5 — Implémentation de la palette de nœuds.

#include "src/world_editor/routine/RoutineNodePalette.h"

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/routine/RoutineGraphCommands.h"
#include "src/world_editor/routine/RoutineGraphDocument.h"
#include "src/world_editor/help/HelpContentStore.h"
#include "src/shared/routine/RoutineNodeSchema.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <memory>
#	include <string>

namespace
{
	using namespace engine::routine;

	// Instancie un nœud depuis un schéma : ids uniques (alloués sur le document),
	// pins/propriétés clonés du gabarit, position au centre logique du canvas.
	RoutineNode MakeNodeFromSchema(const RoutineNodeSchema& s,
	                               engine::editor::world::RoutineGraphDocument& doc)
	{
		RoutineNode n;
		n.id = doc.nextNodeId++;
		n.type = s.type;
		n.canvasX = 40.0f;
		n.canvasY = 40.0f;
		for (const RoutinePin& tmpl : s.pinTemplate)
		{
			RoutinePin p = tmpl;
			p.id = doc.nextPinId++;
			n.pins.push_back(p);
		}
		n.properties = s.propertyTemplate;
		return n;
	}
}
#endif

namespace engine::editor::world
{
	void RenderRoutineNodePalette(RoutineGraphDocument& doc, CommandStack& undo)
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Ajouter un noeud");
		ImGui::Separator();
		for (const engine::routine::RoutineNodeSchema& s : engine::routine::AllSchemas())
		{
			if (!engine::routine::SchemaValidForKind(s, doc.graph.kind)) continue;
			if (ImGui::Selectable(s.displayName))
			{
				undo.Push(std::make_unique<AddNodeCommand>(doc, MakeNodeFromSchema(s, doc)));
				ImGui::CloseCurrentPopup();
			}
			// M101.11 — tooltip de survol : réutilise HelpContentStore (M100.47)
			// si une entrée "routine.<NodeType>" existe, sinon le libellé.
			if (ImGui::IsItemHovered())
			{
				const std::string key = std::string("routine.") + engine::routine::ToString(s.type);
				const auto* help = engine::editor::world::help::HelpContentStore::Instance().FindTooltip(key);
				if (help && !help->descriptionSimple.empty())
					ImGui::SetTooltip("%s\n%s", help->label.c_str(), help->descriptionSimple.c_str());
				else
					ImGui::SetTooltip("%s", s.displayName);
			}
		}
#else
		(void)doc;
		(void)undo;
#endif
	}
}
