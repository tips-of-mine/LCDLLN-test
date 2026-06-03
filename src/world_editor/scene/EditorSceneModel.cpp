#include "src/world_editor/scene/EditorSceneModel.h"

#include "src/world_editor/ui/WorldMapEditDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

namespace engine::editor::scene
{
	namespace
	{
		/// Première chaîne non vide parmi les candidats, ou "?" sinon.
		std::string FirstNonEmpty(const std::string& a, const std::string& b, const char* fallback)
		{
			if (!a.empty()) return a;
			if (!b.empty()) return b;
			return fallback;
		}
	}

	void EditorSceneModel::Bind(
		const engine::editor::WorldMapEditDocument* layoutDoc,
		const engine::editor::world::volumes::MeshInsertDocument* meshDoc,
		const engine::editor::world::volumes::dungeons::DungeonPortalDocument* dungeonDoc)
	{
		m_layoutDoc  = layoutDoc;
		m_meshDoc    = meshDoc;
		m_dungeonDoc = dungeonDoc;
	}

	void EditorSceneModel::Rebuild()
	{
		m_entities.clear();

		// Entité implicite : le terrain de la zone (pas de transform éditable).
		{
			SceneEntity terrain;
			terrain.id = EntityId{EntityKind::Terrain, 0u};
			terrain.label = "Terrain";
			terrain.hasTransform = false;
			m_entities.push_back(std::move(terrain));
		}

		if (m_layoutDoc)
		{
			const std::vector<engine::editor::WorldMapEditLayoutInstance>& insts =
				m_layoutDoc->layoutInstances;
			for (uint32_t i = 0; i < static_cast<uint32_t>(insts.size()); ++i)
			{
				const engine::editor::WorldMapEditLayoutInstance& it = insts[i];
				SceneEntity e;
				e.id = EntityId{EntityKind::LayoutInstance, i};
				e.label = FirstNonEmpty(it.speciesId, it.gltfContentRelativePath, "instance")
					+ " #" + std::to_string(i);
				e.hasTransform = true;
				e.transform.position.x = static_cast<float>(it.worldX);
				e.transform.position.y = static_cast<float>(it.worldY);
				e.transform.position.z = static_cast<float>(it.worldZ);
				e.transform.eulerDeg.y = static_cast<float>(it.yawDegrees);
				e.transform.uniformScale = static_cast<float>(it.uniformScale);
				m_entities.push_back(std::move(e));
			}
		}

		if (m_meshDoc)
		{
			const std::vector<engine::editor::world::volumes::MeshInsertInstance>& all =
				m_meshDoc->All();
			for (uint32_t i = 0; i < static_cast<uint32_t>(all.size()); ++i)
			{
				const engine::editor::world::volumes::MeshInsertInstance& m = all[i];
				SceneEntity e;
				e.id = EntityId{EntityKind::MeshInsert, i};
				e.label = FirstNonEmpty(m.displayName, m.insertCategory, "mesh")
					+ " #" + std::to_string(i);
				e.hasTransform = true;
				e.transform.position = m.worldPosition;
				e.transform.eulerDeg = m.eulerRotationDeg;
				e.transform.uniformScale = m.uniformScale;
				m_entities.push_back(std::move(e));
			}
		}

		if (m_dungeonDoc)
		{
			const std::vector<engine::editor::world::volumes::dungeons::DungeonPortalInstance>& all =
				m_dungeonDoc->All();
			for (uint32_t i = 0; i < static_cast<uint32_t>(all.size()); ++i)
			{
				const engine::editor::world::volumes::dungeons::DungeonPortalInstance& d = all[i];
				SceneEntity e;
				e.id = EntityId{EntityKind::DungeonPortal, i};
				e.label = FirstNonEmpty(d.displayName, d.dungeonTemplateId, "dungeon")
					+ " #" + std::to_string(i);
				e.hasTransform = true;
				e.transform.position = d.worldPosition;
				e.transform.eulerDeg = d.eulerRotationDeg;
				e.transform.uniformScale = 1.0f;
				m_entities.push_back(std::move(e));
			}
		}
	}

	const SceneEntity* EditorSceneModel::Find(EntityId id) const
	{
		for (const SceneEntity& e : m_entities)
		{
			if (e.id == id) return &e;
		}
		return nullptr;
	}
}
