#pragma once

#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes::dungeons
{
	/// Document éditeur portant les portails de donjon (M100.43). Distinct
	/// du `MeshInsertDocument` (M100.40) car porte des metadata gameplay
	/// (template id, difficulty range, level gating). Persiste dans
	/// `instances/zone_<zoneId>/dungeon_portals.bin` (LCDP v1 ; lot B3 —
	/// chemin plat legacy en fallback de lecture / zoneId vide).
	class DungeonPortalDocument
	{
	public:
		using ChangeCallback = std::function<void(const DungeonPortalInstance&)>;
		using RemoveCallback = std::function<void(uint64_t)>;

		/// Lot B3 (audit 2026-06-10 §4.2) — Définit l'identifiant (sanitizé)
		/// de la zone éditée : les chemins disque deviennent
		/// `instances/zone_<zoneId>/dungeon_portals.bin` (écriture toujours
		/// namespacée ; lecture avec fallback sur l'ancien chemin plat).
		/// Chaîne vide = chemins legacy plats (tests, boot). À appeler à
		/// chaque changement de carte, AVANT Save/LoadFromDisk.
		void SetZoneId(std::string zoneId) { m_zoneId = std::move(zoneId); }

		/// Identifiant de zone courant ("" = chemins legacy plats).
		const std::string& GetZoneId() const { return m_zoneId; }

		uint64_t NextGuid() { return ++m_nextGuid; }

		uint64_t Add(DungeonPortalInstance instance);
		bool     Remove(uint64_t guid);
		bool     Update(uint64_t guid, const DungeonPortalInstance& newData);

		const DungeonPortalInstance* GetByGuid(uint64_t guid) const;
		const std::vector<DungeonPortalInstance>& All() const { return m_instances; }
		size_t Size() const { return m_instances.size(); }

		bool IsDirty() const noexcept { return m_dirty; }
		void ClearDirty() noexcept    { m_dirty = false; }

		/// M100.46 — Vide intégralement le document. Marque `m_dirty`.
		/// Utilisé par `WorldMapEditDocumentReset` avant un zone preset.
		void Reset() noexcept
		{
			m_instances.clear();
			m_nextGuid = 0u;
			m_dirty    = true;
		}

		/// Sauve dans `<paths.content>/instances/zone_<zoneId>/dungeon_portals.bin`
		/// (chemin plat legacy si zoneId vide — lot B3).
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);
		/// Charge depuis `<paths.content>/instances/zone_<zoneId>/dungeon_portals.bin`
		/// (fallback LECTURE sur le chemin plat legacy si le fichier
		/// namespacé n'existe pas — lot B3). Si absent : doc vide, pas d'erreur.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

		void SetOnAdded(ChangeCallback cb)   { m_onAdded   = std::move(cb); }
		void SetOnUpdated(ChangeCallback cb) { m_onUpdated = std::move(cb); }
		void SetOnRemoved(RemoveCallback cb) { m_onRemoved = std::move(cb); }

	private:
		std::vector<DungeonPortalInstance> m_instances;
		/// Lot B3 — identifiant (sanitizé) de la zone, namespace des chemins.
		std::string m_zoneId;
		uint64_t m_nextGuid = 0u;
		bool     m_dirty    = false;

		ChangeCallback m_onAdded;
		ChangeCallback m_onUpdated;
		RemoveCallback m_onRemoved;
	};
}
