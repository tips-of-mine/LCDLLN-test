// engine/editor/world/panels/CollisionEditorPanel.cpp
#include "engine/editor/world/panels/CollisionEditorPanel.h"
#include "engine/world/collision/ProxyWireframe.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <cmath>
#include <cstring>
#include <vector>

namespace engine::editor::world::panels
{
	namespace
	{
		using engine::math::Vec3;
		using engine::world::collision::CollisionProxy;
		using engine::world::collision::ProxyType;
		using engine::world::collision::Edge3D;

		/// Mesh test : cube unitaire centré sur origine (8 verts, 12 arêtes).
		std::vector<Edge3D> MakeCubeEdges()
		{
			Vec3 v[8] = {
				{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f},
				{-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
				{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f},
				{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f},
			};
			static constexpr int e[12][2] = {
				{0,1},{1,3},{3,2},{2,0},
				{4,5},{5,7},{7,6},{6,4},
				{0,4},{1,5},{2,6},{3,7},
			};
			std::vector<Edge3D> out;
			out.reserve(12);
			for (int i = 0; i < 12; ++i)
				out.emplace_back(v[e[i][0]], v[e[i][1]]);
			return out;
		}

		/// Mesh test : cylindre vertical centré, 16 segments × 2 caps.
		std::vector<Edge3D> MakeCylinderEdges()
		{
			std::vector<Edge3D> out;
			constexpr int segs = 16;
			const float r = 0.4f;
			const float h = 1.0f;
			for (int cap = 0; cap < 2; ++cap)
			{
				const float y = (cap == 0) ? -h * 0.5f : h * 0.5f;
				for (int i = 0; i < segs; ++i)
				{
					const float t0 = static_cast<float>(i)     / segs * 6.2831853f;
					const float t1 = static_cast<float>(i + 1) / segs * 6.2831853f;
					out.emplace_back(
						Vec3{ std::cos(t0) * r, y, std::sin(t0) * r },
						Vec3{ std::cos(t1) * r, y, std::sin(t1) * r });
				}
			}
			for (int i = 0; i < 4; ++i)
			{
				const float t = static_cast<float>(i) * 1.5707963f;
				const float x = std::cos(t) * r;
				const float z = std::sin(t) * r;
				out.emplace_back(Vec3{ x, -h * 0.5f, z }, Vec3{ x, h * 0.5f, z });
			}
			return out;
		}

		/// Mesh test : icosphère grossière 20 tris (12 verts).
		std::vector<Edge3D> MakeSphereEdges()
		{
			const float t = (1.0f + std::sqrt(5.0f)) * 0.5f * 0.5f;
			const float s = 0.5f;
			Vec3 v[12] = {
				{-s,  t*s, 0}, { s,  t*s, 0}, {-s, -t*s, 0}, { s, -t*s, 0},
				{ 0, -s,  t*s}, { 0,  s,  t*s}, { 0, -s, -t*s}, { 0,  s, -t*s},
				{ t*s, 0, -s}, { t*s, 0,  s}, {-t*s, 0, -s}, {-t*s, 0,  s},
			};
			static constexpr int idx[20][3] = {
				{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
				{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
				{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
				{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1},
			};
			std::vector<Edge3D> out;
			out.reserve(60);
			for (int i = 0; i < 20; ++i)
			{
				out.emplace_back(v[idx[i][0]], v[idx[i][1]]);
				out.emplace_back(v[idx[i][1]], v[idx[i][2]]);
				out.emplace_back(v[idx[i][2]], v[idx[i][0]]);
			}
			return out;
		}

		/// Mesh test : slab plat 1×0.05×1.
		std::vector<Edge3D> MakeSlabEdges()
		{
			Vec3 v[8] = {
				{-0.5f,-0.025f,-0.5f}, { 0.5f,-0.025f,-0.5f},
				{-0.5f, 0.025f,-0.5f}, { 0.5f, 0.025f,-0.5f},
				{-0.5f,-0.025f, 0.5f}, { 0.5f,-0.025f, 0.5f},
				{-0.5f, 0.025f, 0.5f}, { 0.5f, 0.025f, 0.5f},
			};
			static constexpr int e[12][2] = {
				{0,1},{1,3},{3,2},{2,0},
				{4,5},{5,7},{7,6},{6,4},
				{0,4},{1,5},{2,6},{3,7},
			};
			std::vector<Edge3D> out;
			out.reserve(12);
			for (int i = 0; i < 12; ++i)
				out.emplace_back(v[e[i][0]], v[e[i][1]]);
			return out;
		}

		std::vector<Edge3D> GetTestMeshEdges(int index)
		{
			switch (index)
			{
				case 1:  return MakeCylinderEdges();
				case 2:  return MakeSphereEdges();
				case 3:  return MakeSlabEdges();
				default: return MakeCubeEdges();
			}
		}
	}

	void CollisionEditorPanel::Init(const std::filesystem::path& contentRoot)
	{
		m_contentRoot = contentRoot;
	}

	void CollisionEditorPanel::Render()
	{
#if defined(_WIN32)
		if (!ImGui::Begin(GetName(), &m_visible))
		{
			ImGui::End();
			return;
		}

		// ── Toolbar ────────────────────────────────────────────────────
		ImGui::InputText("##path", m_pathInputBuf, sizeof(m_pathInputBuf));
		ImGui::SameLine();
		if (ImGui::Button("Open"))
		{
			std::filesystem::path p = m_contentRoot / m_pathInputBuf;
			std::string err;
			if (m_proxy.LoadFromFile(p, err))
			{
				m_currentPath = p;
				m_status = "Loaded \xE2\x9C\x93";
				m_statusFramesLeft = 60;
			}
			else
			{
				m_status = "Load error: " + err;
				m_statusFramesLeft = 180;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("New Capsule"))
		{
			m_proxy = engine::world::collision::CollisionProxy{};
			m_proxy.type = ProxyType::Capsule;
		}
		ImGui::SameLine();
		if (ImGui::Button("New ConvexHull"))
		{
			m_proxy = engine::world::collision::CollisionProxy{};
			m_proxy.type = ProxyType::ConvexHull;
		}
		ImGui::SameLine();
		if (ImGui::Button("New TriMesh"))
		{
			m_proxy = engine::world::collision::CollisionProxy{};
			m_proxy.type = ProxyType::TriMesh;
		}

		ImGui::Text("Source: %s", m_currentPath.empty() ? "<empty>" : m_currentPath.string().c_str());
		ImGui::Separator();

		// ── Type radios ─────────────────────────────────────────────────
		int t = static_cast<int>(m_proxy.type);
		ImGui::RadioButton("Capsule",    &t, 0); ImGui::SameLine();
		ImGui::RadioButton("ConvexHull", &t, 1); ImGui::SameLine();
		ImGui::RadioButton("TriMesh",    &t, 2);
		m_proxy.type = static_cast<ProxyType>(t);
		ImGui::Separator();

		// ── Type-specific fields ────────────────────────────────────────
		if (m_proxy.type == ProxyType::Capsule)
		{
			ImGui::SliderFloat3("A",      &m_proxy.capsuleA.x,    -5.0f, 5.0f, "%.3f");
			ImGui::SliderFloat3("B",      &m_proxy.capsuleB.x,    -5.0f, 5.0f, "%.3f");
			ImGui::SliderFloat ("Radius", &m_proxy.capsuleRadius,  0.05f, 2.0f, "%.3f");
		}
		else if (m_proxy.type == ProxyType::ConvexHull)
		{
			ImGui::Text("Vertex count: %zu", m_proxy.vertices.size());
			ImGui::BeginDisabled(true);
			ImGui::Button("Re-run AutoFit");
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Requires mesh CPU data — wired with mesh import (out-of-scope M100.12)");
		}
		else // TriMesh
		{
			ImGui::Text("Vertex count: %zu", m_proxy.vertices.size());
			ImGui::Text("Tri count: %zu",     m_proxy.indices.size() / 3);
		}
		ImGui::Separator();

		// ── Preview 3D ──────────────────────────────────────────────────
		ImGui::Text("Preview");
		const char* meshNames[4] = { "Cube", "Cylinder", "Sphere", "Slab" };
		ImGui::Combo("Test mesh", &m_testMeshIndex, meshNames, 4);
		ImGui::SameLine();
		if (ImGui::Button("Reset Camera")) m_camera.Reset();

		const ImVec2 previewSize{ 300.0f, 200.0f };
		ImGui::BeginChild("##preview", previewSize, true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		const ImVec2 previewMin = ImGui::GetCursorScreenPos();
		const ImVec2 contentSize = ImGui::GetContentRegionAvail();
		const float w = contentSize.x;
		const float h = contentSize.y;

		// Drag / wheel
		ImGui::InvisibleButton("##previewCanvas", contentSize);
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 d = ImGui::GetIO().MouseDelta;
			m_camera.HandleDrag(d.x, d.y);
		}
		if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
			m_camera.HandleZoom(ImGui::GetIO().MouseWheel);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		auto drawEdge = [&](const Vec3& a, const Vec3& b, ImU32 color)
		{
			float ax, ay, bx, by;
			const bool aOk = m_camera.Project(a, w, h, ax, ay);
			const bool bOk = m_camera.Project(b, w, h, bx, by);
			if (!aOk || !bOk) return;
			dl->AddLine(ImVec2(previewMin.x + ax, previewMin.y + ay),
			            ImVec2(previewMin.x + bx, previewMin.y + by),
			            color, 1.5f);
		};

		// Test mesh en gris
		const auto testEdges = GetTestMeshEdges(m_testMeshIndex);
		for (const auto& e : testEdges)
			drawEdge(e.first, e.second, IM_COL32(140, 140, 140, 200));

		// Proxy wireframe en vert
		const auto proxyEdges = engine::world::collision::GenerateWireframeEdges(m_proxy);
		for (const auto& e : proxyEdges)
			drawEdge(e.first, e.second, IM_COL32(80, 255, 80, 255));

		ImGui::EndChild();

		ImGui::Text("Yaw: %.0f deg   Pitch: %.0f deg   Distance: %.1f m",
			m_camera.GetYawDegrees(), m_camera.GetPitchDegrees(), m_camera.GetDistance());
		ImGui::Separator();

		// ── Save ────────────────────────────────────────────────────────
		if (ImGui::Button("Save .collision.bin"))
		{
			std::filesystem::path p = m_currentPath.empty()
				? (m_contentRoot / m_pathInputBuf)
				: m_currentPath;
			std::string err;
			if (m_proxy.SaveToFile(p, err))
			{
				m_currentPath = p;
				m_status = "Saved \xE2\x9C\x93";
				m_statusFramesLeft = 60;
			}
			else
			{
				m_status = "Save error: " + err;
				m_statusFramesLeft = 180;
			}
		}

		// ── Status ──────────────────────────────────────────────────────
		if (m_statusFramesLeft > 0)
		{
			--m_statusFramesLeft;
			if (m_status.find("error") != std::string::npos)
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: %s", m_status.c_str());
			else
				ImGui::Text("Status: %s", m_status.c_str());
		}

		ImGui::End();
#endif
	}
}
