// M101.4 / M101.5 — Rendu du panneau nodal. Corps ImGui guardé Windows.

#include "src/world_editor/routine/RoutineGraphPanel.h"

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/routine/RoutineGraphCommands.h"
#include "src/world_editor/routine/RoutineNodeInspector.h"
#include "src/world_editor/routine/RoutineNodePalette.h"
#include "src/shared/routine/RoutineGraph.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <memory>
#endif

namespace engine::editor::world
{
	void RoutineGraphPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Routines", &m_visible))
		{
			// Barre : nom + type de graphe + bouton palette.
			ImGui::Text("Graphe: %s",
				m_doc.graph.name.empty() ? "(sans nom)" : m_doc.graph.name.c_str());
			ImGui::SameLine();
			ImGui::Text("| Type: %s", engine::routine::ToString(m_doc.graph.kind));
			ImGui::SameLine();
			if (ImGui::Button("+ Noeud")) ImGui::OpenPopup("routine_palette");
			if (ImGui::BeginPopup("routine_palette"))
			{
				if (m_stack) RenderRoutineNodePalette(m_doc, *m_stack);
				ImGui::EndPopup();
			}
			ImGui::Separator();

			ImGui::Columns(2, "routine_cols", true);

			// --- canvas ---
			ImGui::BeginChild("routine_canvas", ImVec2(0.0f, 0.0f), true);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float zoom = (m_doc.zoom <= 0.0f) ? 1.0f : m_doc.zoom;
			const ImVec2 nodeSize(140.0f * zoom, 48.0f * zoom);

			// Liens (courbes de Bézier sortie -> entrée).
			for (const auto& l : m_doc.graph.links)
			{
				const engine::routine::RoutineNode* a = m_doc.FindNode(l.fromNodeId);
				const engine::routine::RoutineNode* b = m_doc.FindNode(l.toNodeId);
				if (!a || !b) continue;
				const ImVec2 pa(origin.x + (a->canvasX + m_doc.panX) * zoom + nodeSize.x,
				                origin.y + (a->canvasY + m_doc.panY) * zoom + nodeSize.y * 0.5f);
				const ImVec2 pb(origin.x + (b->canvasX + m_doc.panX) * zoom,
				                origin.y + (b->canvasY + m_doc.panY) * zoom + nodeSize.y * 0.5f);
				dl->AddBezierCubic(pa, ImVec2(pa.x + 50.0f, pa.y), ImVec2(pb.x - 50.0f, pb.y), pb,
				                   IM_COL32(180, 180, 120, 255), 2.0f);
			}

			// Nœuds (boîtes + sélection + drag).
			for (auto& n : m_doc.graph.nodes)
			{
				const ImVec2 p(origin.x + (n.canvasX + m_doc.panX) * zoom,
				               origin.y + (n.canvasY + m_doc.panY) * zoom);
				ImGui::PushID(static_cast<int>(n.id));
				ImGui::SetCursorScreenPos(p);
				ImGui::InvisibleButton("node", nodeSize);

				if (ImGui::IsItemActivated())
				{
					m_doc.selectedNodeId = n.id;
					m_dragging = true;
					m_dragNodeId = n.id;
					m_dragStartX = n.canvasX;
					m_dragStartY = n.canvasY;
				}
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
				{
					const ImVec2 d = ImGui::GetIO().MouseDelta;
					n.canvasX += d.x / zoom;
					n.canvasY += d.y / zoom;
				}

				const bool sel = (m_doc.selectedNodeId == n.id);
				const ImU32 fill = sel ? IM_COL32(90, 90, 140, 230) : IM_COL32(60, 60, 70, 230);
				dl->AddRectFilled(p, ImVec2(p.x + nodeSize.x, p.y + nodeSize.y), fill, 5.0f);
				dl->AddRect(p, ImVec2(p.x + nodeSize.x, p.y + nodeSize.y),
				            IM_COL32(200, 200, 210, 255), 5.0f);
				dl->AddText(ImVec2(p.x + 6.0f, p.y + 6.0f), IM_COL32(235, 235, 240, 255),
				            engine::routine::ToString(n.type));
				ImGui::PopID();
			}

			// Au relâchement : enregistre un MoveNodeCommand (undo) si déplacé.
			if (m_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (m_stack && m_dragNodeId != 0)
				{
					if (engine::routine::RoutineNode* n = m_doc.FindNode(m_dragNodeId))
					{
						const float curX = n->canvasX;
						const float curY = n->canvasY;
						if (curX != m_dragStartX || curY != m_dragStartY)
						{
							// Remet à l'origine puis pousse la commande (Execute
							// réapplique la position finale ; Undo restaure).
							n->canvasX = m_dragStartX;
							n->canvasY = m_dragStartY;
							m_stack->Push(std::make_unique<MoveNodeCommand>(
								m_doc, m_dragNodeId, m_dragStartX, m_dragStartY, curX, curY));
						}
					}
				}
				m_dragging = false;
				m_dragNodeId = 0;
			}

			ImGui::EndChild();

			// --- inspecteur ---
			ImGui::NextColumn();
			if (m_stack) RenderRoutineNodeInspector(m_doc, *m_stack);

			ImGui::Columns(1);
		}
		ImGui::End();
#endif
	}
}
