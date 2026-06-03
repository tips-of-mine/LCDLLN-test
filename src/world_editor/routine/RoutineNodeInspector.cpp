// M101.5 — Implémentation de l'inspecteur de propriétés de nœud.

#include "src/world_editor/routine/RoutineNodeInspector.h"

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/routine/RoutineGraphCommands.h"
#include "src/world_editor/routine/RoutineGraphDocument.h"
#include "src/shared/routine/RoutineNodeSchema.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstring>
#	include <memory>
#endif

namespace engine::editor::world
{
	void RenderRoutineNodeInspector(RoutineGraphDocument& doc, CommandStack& undo)
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Inspecteur");
		ImGui::Separator();

		engine::routine::RoutineNode* n = doc.FindNode(doc.selectedNodeId);
		if (!n)
		{
			ImGui::TextUnformatted("(aucun noeud selectionne)");
			return;
		}

		if (const engine::routine::RoutineNodeSchema* s = engine::routine::FindSchema(n->type))
			ImGui::Text("Type : %s", s->displayName);

		for (size_t i = 0; i < n->properties.size(); ++i)
		{
			engine::routine::RoutineProperty& pr = n->properties[i];
			ImGui::PushID(static_cast<int>(i));

			switch (pr.type)
			{
				case engine::routine::RoutineDataType::Bool:
				{
					bool v = pr.bValue;
					if (ImGui::Checkbox(pr.key.c_str(), &v))
					{
						engine::routine::RoutineProperty np = pr; np.bValue = v;
						undo.Push(std::make_unique<SetNodePropertyCommand>(doc, n->id, np));
					}
					break;
				}
				case engine::routine::RoutineDataType::Int:
				{
					int v = static_cast<int>(pr.iValue);
					if (ImGui::InputInt(pr.key.c_str(), &v))
					{
						engine::routine::RoutineProperty np = pr; np.iValue = v;
						undo.Push(std::make_unique<SetNodePropertyCommand>(doc, n->id, np));
					}
					break;
				}
				case engine::routine::RoutineDataType::Float:
				{
					float v = pr.fValue;
					if (ImGui::InputFloat(pr.key.c_str(), &v))
					{
						engine::routine::RoutineProperty np = pr; np.fValue = v;
						undo.Push(std::make_unique<SetNodePropertyCommand>(doc, n->id, np));
					}
					break;
				}
				case engine::routine::RoutineDataType::Vec3:
				{
					float v[3] = { pr.vValue.x, pr.vValue.y, pr.vValue.z };
					if (ImGui::InputFloat3(pr.key.c_str(), v))
					{
						engine::routine::RoutineProperty np = pr;
						np.vValue.x = v[0]; np.vValue.y = v[1]; np.vValue.z = v[2];
						undo.Push(std::make_unique<SetNodePropertyCommand>(doc, n->id, np));
					}
					break;
				}
				case engine::routine::RoutineDataType::String:
				case engine::routine::RoutineDataType::EntityRef:
				{
					char buf[256];
					std::strncpy(buf, pr.sValue.c_str(), sizeof(buf) - 1);
					buf[sizeof(buf) - 1] = '\0';
					if (ImGui::InputText(pr.key.c_str(), buf, sizeof(buf)))
					{
						engine::routine::RoutineProperty np = pr; np.sValue = buf;
						undo.Push(std::make_unique<SetNodePropertyCommand>(doc, n->id, np));
					}
					break;
				}
				case engine::routine::RoutineDataType::None:
				default:
					ImGui::Text("%s : (aucune valeur)", pr.key.c_str());
					break;
			}

			ImGui::PopID();
		}
#else
		(void)doc;
		(void)undo;
#endif
	}
}
