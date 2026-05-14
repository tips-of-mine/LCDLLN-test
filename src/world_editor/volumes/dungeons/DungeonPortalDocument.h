#pragma once

#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes::dungeons
{
	/// Document éditeur portant les portails de donjon (M100.43). Distinct
	/// du `MeshInsertDocument` (M100.40) car porte des metadata gameplay
	/// (template id, difficulty range, level gating). Persiste dans
	/// `instances/dungeon_portals.bin` (LCDP v1).
	class DungeonPortalDocument
	{
	public:
		using ChangeCallback = std::function<void(const DungeonPortalInstance&)>;
		using RemoveCallback = std::function<void(uint64_t)>;

		uint64_t NextGuid() { return ++m_nextGuid; }

		uint64_t Add(DungeonPortalInstance instance);
		bool     Remove(uint64_t guid);
		bool     Update(uint64_t guid, const DungeonPortalInstance& newData);

		const DungeonPortalInstance* GetByGuid(uint64_t guid) const;
		const std::vector<DungeonPortalInstance>& All() const { return m_instances; }
		size_t Size() const { return m_instances.size(); }

		bool IsDirty() const noexcept { return m_dirty; }
		void ClearDirty() noexcept    { m_dirty = false; }

		/// Sauve dans `<paths.content>/instances/dungeon_portals.bin`.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);
		/// Charge depuis `<paths.content>/instances/dungeon_portals.bin`.
		/// Si absent : doc vide, pas d'erreur.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

		void SetOnAdded(ChangeCallback cb)   { m_onAdded   = std::move(cb); }
		void SetOnUpdated(ChangeCallback cb) { m_onUpdated = std::move(cb); }
		void SetOnRemoved(RemoveCallback cb) { m_onRemoved = std::move(cb); }

	private:
		std::vector<DungeonPortalInstance> m_instances;
		uint64_t m_nextGuid = 0u;
		bool     m_dirty    = false;

		ChangeCallback m_onAdded;
		ChangeCallback m_onUpdated;
		RemoveCallback m_onRemoved;
	};
}
