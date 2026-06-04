// M100.17 — Implémentation de l'outil de placement (BuildInstances pur + Render ImGui).

#include "src/world_editor/PlacementTool.h"

#include "src/world_editor/PlacementDocument.h"
#include "src/world_editor/PlacementGeometry.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstdio>
#endif

namespace engine::editor::world
{
	uint32_t HashAssetPath(const std::string& path)
	{
		uint32_t hash = 2166136261u;
		for (unsigned char c : path) { hash ^= c; hash *= 16777619u; }
		return hash;
	}

	std::vector<engine::world::instances::PropInstance> PlacementTool::BuildInstances(
		const engine::math::Vec3& start, const engine::math::Vec3& end,
		const engine::math::Vec3& terrainNormal, PlacementDocument& doc) const
	{
		using engine::world::instances::PropInstance;
		namespace pg = engine::editor::world::placement;

		std::vector<engine::math::Vec3> positions;
		switch (m_params.mode)
		{
			case PlacementMode::Single:   positions.push_back(start); break;
			case PlacementMode::DragLine: positions = pg::GenerateDragLine(start, end, m_params.dragLineSpacing); break;
			case PlacementMode::Scatter:  positions = pg::GenerateScatter(start, m_params.scatterRadius, m_params.scatterCount, m_params.rngSeed); break;
		}

		const uint32_t assetId = HashAssetPath(m_params.assetPath);
		const bool alignNormal = (m_params.align == PlacementAlign::TerrainNormal);

		std::vector<PropInstance> out;
		out.reserve(positions.size());
		for (size_t i = 0; i < positions.size(); ++i)
		{
			const uint64_t seedI = m_params.rngSeed + static_cast<uint64_t>(i);
			const pg::YawScale ys = pg::RandomYawScale(seedI, m_params.rotMinDeg, m_params.rotMaxDeg,
			                                           m_params.scaleMin, m_params.scaleMax);
			PropInstance inst;
			inst.assetId = assetId;
			inst.position = positions[i];
			pg::BuildOrientation(ys.yawDeg, terrainNormal, alignNormal, inst.rotationQuat);
			inst.scale = engine::math::Vec3(ys.scale, ys.scale, ys.scale);
			inst.layerTag = m_params.layerTag;
			inst.instanceId = doc.AllocInstanceId();
			out.push_back(inst);
		}
		return out;
	}

	void PlacementTool::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Placement");
		ImGui::Separator();

		char buf[260];
		std::snprintf(buf, sizeof(buf), "%s", m_params.assetPath.c_str());
		if (ImGui::InputText("Asset", buf, sizeof(buf))) m_params.assetPath = buf;

		int mode = static_cast<int>(m_params.mode);
		const char* kModes[] = { "Single", "Drag-line", "Scatter" };
		if (ImGui::Combo("Mode", &mode, kModes, IM_ARRAYSIZE(kModes)))
			m_params.mode = static_cast<PlacementMode>(mode);

		int snap = static_cast<int>(m_params.snap);
		const char* kSnaps[] = { "Ground", "Grid", "Face" };
		if (ImGui::Combo("Snap", &snap, kSnaps, IM_ARRAYSIZE(kSnaps)))
			m_params.snap = static_cast<PlacementSnap>(snap);

		int align = static_cast<int>(m_params.align);
		ImGui::RadioButton("Terrain normal", &align, 0); ImGui::SameLine();
		ImGui::RadioButton("World up", &align, 1);
		m_params.align = static_cast<PlacementAlign>(align);

		ImGui::DragFloatRange2("Rotation Y (deg)", &m_params.rotMinDeg, &m_params.rotMaxDeg, 1.0f, 0.0f, 360.0f);
		ImGui::DragFloatRange2("Scale", &m_params.scaleMin, &m_params.scaleMax, 0.01f, 0.1f, 5.0f);

		if (m_params.mode == PlacementMode::DragLine)
			ImGui::InputFloat("Spacing (m)", &m_params.dragLineSpacing);
		if (m_params.mode == PlacementMode::Scatter)
		{
			ImGui::InputFloat("Scatter radius (m)", &m_params.scatterRadius);
			int cnt = static_cast<int>(m_params.scatterCount);
			if (ImGui::InputInt("Scatter count", &cnt) && cnt >= 0)
				m_params.scatterCount = static_cast<uint32_t>(cnt);
		}
#endif
	}
}
