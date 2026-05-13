#include "src/world_editor/volumes/dungeons/DungeonPortalTool.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/PlaceDungeonPortalCommand.h"

#include <memory>
#include <utility>

namespace engine::editor::world::volumes::dungeons
{
	bool DungeonPortalTool::Init(engine::editor::world::CommandStack& stack,
		DungeonPortalDocument& doc, const engine::core::Config& cfg)
	{
		m_stack = &stack;
		m_doc   = &doc;
		m_cfg   = &cfg;
		Reset();
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		LoadCatalog(contentRoot);
		return true;
	}

	void DungeonPortalTool::Reset()
	{
		m_selectedTemplateId.clear();
		m_targetWorldX = m_targetWorldY = m_targetWorldZ = 0.0f;
		m_yawDeg               = 0.0f;
		m_triggerRadius        = 3.0f;
		m_requiredLevel        = 1u;
		m_minDifficulty        = 1u;
		m_maxDifficulty        = 1u;
		m_isOneShot            = false;
		m_persistsAcrossLogin  = false;
	}

	void DungeonPortalTool::LoadCatalog(const std::string& contentRoot)
	{
		std::string err;
		(void)m_catalog.LoadFromContent(contentRoot, err);
	}

	void DungeonPortalTool::SelectByTemplateId(const std::string& id)
	{
		m_selectedTemplateId = id;
		// Préremplit les sliders depuis le catalog pour aider l'utilisateur.
		const DungeonCatalogEntry* entry = m_catalog.FindById(id);
		if (entry != nullptr)
		{
			m_requiredLevel = entry->requiredLevel;
			m_minDifficulty = entry->minDifficulty;
			m_maxDifficulty = entry->maxDifficulty;
		}
	}

	bool DungeonPortalTool::Place()
	{
		if (m_stack == nullptr || m_doc == nullptr) return false;
		if (m_selectedTemplateId.empty()) return false;
		const DungeonCatalogEntry* entry = m_catalog.FindById(m_selectedTemplateId);
		if (entry == nullptr) return false;
		if (m_minDifficulty == 0u || m_maxDifficulty < m_minDifficulty) return false;
		if (m_minDifficulty < entry->minDifficulty) return false;
		if (m_maxDifficulty > entry->maxDifficulty) return false;

		DungeonPortalInstance inst;
		inst.guid              = 0u;
		inst.dungeonTemplateId = m_selectedTemplateId;
		inst.displayName       = entry->displayName.empty() ? entry->id : entry->displayName;
		inst.decorativeMeshPath = entry->decorativeMeshPath;
		inst.worldPosition    = { m_targetWorldX, m_targetWorldY, m_targetWorldZ };
		inst.eulerRotationDeg = { 0.0f, m_yawDeg, 0.0f };
		inst.triggerRadius    = m_triggerRadius;
		inst.requiredLevel    = m_requiredLevel;
		inst.minDifficulty    = m_minDifficulty;
		inst.maxDifficulty    = m_maxDifficulty;
		inst.isOneShot            = m_isOneShot;
		inst.persistsAcrossLogin  = m_persistsAcrossLogin;

		auto cmd = std::make_unique<PlaceDungeonPortalCommand>(*m_doc, std::move(inst));
		m_stack->Push(std::move(cmd));
		return true;
	}

	void DungeonPortalTool::Cancel()
	{
		m_selectedTemplateId.clear();
	}
}
