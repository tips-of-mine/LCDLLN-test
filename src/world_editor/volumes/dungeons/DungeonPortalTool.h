#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/dungeons/DungeonCatalog.h"

#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes::dungeons
{
	class DungeonPortalDocument;

	/// Outil de placement de portails de donjon (M100.43). Workflow :
	///   - charge `meshes/dungeons/catalog.json`,
	///   - sélection d'un template_id,
	///   - position cible (raycast viewport reporté à M100.17 — input
	///     manuel en MVP),
	///   - sliders triggerRadius / yaw / requiredLevel / difficulty
	///     (verrouillés au range du catalog),
	///   - clic `Place` → pousse `PlaceDungeonPortalCommand`.
	///
	/// Le portail posé est persisté dans `instances/dungeon_portals.bin`
	/// (LCDP v1). Sa connexion au master via `kOpcodeEnterDungeonRequest`
	/// (opcode 197, M100.43-réservé) sera câblée en M100.44 quand le
	/// `EnterDungeonHandler` shard sera disponible.
	class DungeonPortalTool
	{
	public:
		bool Init(engine::editor::world::CommandStack& stack,
			DungeonPortalDocument& doc, const engine::core::Config& cfg);

		void Reset();
		void LoadCatalog(const std::string& contentRoot);
		const DungeonCatalog& Catalog() const { return m_catalog; }

		const std::string& SelectedTemplateId() const { return m_selectedTemplateId; }
		void SelectByTemplateId(const std::string& id);

		float& TargetWorldX() { return m_targetWorldX; }
		float& TargetWorldY() { return m_targetWorldY; }
		float& TargetWorldZ() { return m_targetWorldZ; }
		float& YawDeg()       { return m_yawDeg; }

		float& TriggerRadius() { return m_triggerRadius; }
		uint16_t& RequiredLevel() { return m_requiredLevel; }
		uint8_t&  MinDifficulty() { return m_minDifficulty; }
		uint8_t&  MaxDifficulty() { return m_maxDifficulty; }
		bool&     IsOneShot()           { return m_isOneShot; }
		bool&     PersistsAcrossLogin() { return m_persistsAcrossLogin; }

		/// Pousse une `PlaceDungeonPortalCommand`. Retourne false si :
		///   - aucun template sélectionné, ou
		///   - template introuvable dans le catalogue, ou
		///   - difficulty range incohérent (`min > max`), ou
		///   - difficulty hors bornes du catalog (gating cohérence).
		bool Place();

		void Cancel();

	private:
		engine::editor::world::CommandStack* m_stack = nullptr;
		DungeonPortalDocument*               m_doc   = nullptr;
		const engine::core::Config*          m_cfg   = nullptr;

		DungeonCatalog m_catalog;
		std::string    m_selectedTemplateId;

		float m_targetWorldX = 0.0f, m_targetWorldY = 0.0f, m_targetWorldZ = 0.0f;
		float m_yawDeg       = 0.0f;
		float m_triggerRadius = 3.0f;

		uint16_t m_requiredLevel = 1u;
		uint8_t  m_minDifficulty = 1u;
		uint8_t  m_maxDifficulty = 1u;
		bool     m_isOneShot           = false;
		bool     m_persistsAcrossLogin = false;
	};
}
