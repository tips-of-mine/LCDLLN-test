// M100.34 — Implémentation MinimapPanel (helper pur + rendu ImGui guardé).

#include "src/world_editor/panels/MinimapPanel.h"

#include <algorithm>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	MinimapView BuildMinimapView(int32_t centerX, int32_t centerZ, int32_t radius,
		const std::vector<std::pair<int32_t, int32_t>>& loaded)
	{
		if (radius < 0) radius = 0;
		MinimapView view;
		view.centerX = centerX;
		view.centerZ = centerZ;
		view.radius  = radius;
		const int32_t side = 2 * radius + 1;
		view.cells.reserve(static_cast<size_t>(side) * static_cast<size_t>(side));
		for (int32_t dz = -radius; dz <= radius; ++dz)
		{
			for (int32_t dx = -radius; dx <= radius; ++dx)
			{
				const int32_t cx = centerX + dx;
				const int32_t cz = centerZ + dz;
				MinimapCell cell;
				cell.chunkX = cx;
				cell.chunkZ = cz;
				cell.isCurrent = (dx == 0 && dz == 0);
				cell.isLoaded = std::find(loaded.begin(), loaded.end(),
					std::make_pair(cx, cz)) != loaded.end();
				view.cells.push_back(cell);
			}
		}
		return view;
	}

	void MinimapPanel::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Minimap");
		ImGui::Separator();
		ImGui::Text("Center chunk (%d, %d)  radius %d", m_centerX, m_centerZ, m_radius);

		const MinimapView view = BuildMinimapView(m_centerX, m_centerZ, m_radius, m_loaded);
		const int32_t side = 2 * m_radius + 1;
		const float cellPx = 16.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		for (int32_t row = 0; row < side; ++row)
		{
			for (int32_t col = 0; col < side; ++col)
			{
				const MinimapCell& c = view.cells[static_cast<size_t>(row) * side + col];
				const ImVec2 a(origin.x + col * cellPx, origin.y + row * cellPx);
				const ImVec2 b(a.x + cellPx - 1.0f, a.y + cellPx - 1.0f);
				ImU32 col32;
				if (c.isCurrent)      col32 = IM_COL32(255, 230, 80, 255);  // chunk courant.
				else if (c.isLoaded)  col32 = IM_COL32(120, 160, 200, 255); // chargé.
				else                  col32 = IM_COL32(60, 60, 70, 255);    // vide.
				dl->AddRectFilled(a, b, col32);
			}
		}
		ImGui::Dummy(ImVec2(side * cellPx, side * cellPx));
#endif
	}
}
