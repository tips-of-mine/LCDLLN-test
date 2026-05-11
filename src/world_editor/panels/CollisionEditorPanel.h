// src/world_editor/panels/CollisionEditorPanel.h
#pragma once

#include "src/world_editor/camera/CollisionPreviewCamera.h"
#include "src/world_editor/core/IPanel.h"
#include "src/client/world/collision/CollisionProxy.h"

#include <filesystem>
#include <string>

namespace engine::editor::world::panels
{
	/// Panel ImGui d'authoring de proxies de collision (M100.12).
	/// Workflow : Open .collision.bin OU New (Capsule/Hull/TriMesh) → switch type →
	/// edit fields (sliders capsule, read-only hull/trimesh) → Save .collision.bin.
	/// Mini-preview 3D avec wireframe vert overlay sur mesh test synthétique.
	class CollisionEditorPanel final : public engine::editor::world::IPanel
	{
	public:
		const char* GetName() const override { return "Collision Editor"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool v) override { m_visible = v; }

		/// Initialise le panel avec un contentRoot (typiquement "game/data").
		/// Ne charge pas un proxy automatique — l'utilisateur doit cliquer
		/// "Open .collision.bin..." ou "New ...".
		void Init(const std::filesystem::path& contentRoot);

	private:
		bool m_visible = false;
		std::filesystem::path m_contentRoot;
		std::filesystem::path m_currentPath;
		engine::world::collision::CollisionProxy m_proxy;
		engine::editor::world::CollisionPreviewCamera m_camera;
		int m_testMeshIndex = 0;  // 0=Cube, 1=Cylinder, 2=Sphere, 3=Slab
		std::string m_status;
		int m_statusFramesLeft = 0;
		char m_pathInputBuf[260] = {};
	};
}
