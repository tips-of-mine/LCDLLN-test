#include "src/world_editor/volumes/overhangs/OverhangTool.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/overhangs/PlaceOverhangCommand.h"

#include <memory>
#include <utility>

namespace engine::editor::world::volumes::overhangs
{
	bool OverhangTool::Init(engine::editor::world::CommandStack& stack,
		MeshInsertDocument& meshDoc, const engine::core::Config& cfg)
	{
		m_stack   = &stack;
		m_meshDoc = &meshDoc;
		m_cfg     = &cfg;
		Reset();
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		LoadCatalog(contentRoot);
		return true;
	}

	void OverhangTool::Reset()
	{
		m_selectedId.clear();
		m_targetWorldX = 0.0f;
		m_targetWorldY = 0.0f;
		m_targetWorldZ = 0.0f;
		m_wallNormalYawDeg    = 0.0f;
		m_tiltDeg             = 0.0f;
		m_uniformScale        = 1.0f;
		m_requiredSlopeDeg    = 35.0f;
		m_observedSlopeDeg    = 35.0f;
		m_castsShadow         = true;
		m_receivesAudioReverb = false;
		m_lightProbeIntensity = 0.6f;
	}

	void OverhangTool::LoadCatalog(const std::string& contentRoot)
	{
		std::string err;
		(void)m_catalog.LoadFromContent(contentRoot, err);
	}

	bool OverhangTool::Place()
	{
		if (m_stack == nullptr || m_meshDoc == nullptr) return false;
		if (m_selectedId.empty()) return false;
		const OverhangCatalogEntry* entry = m_catalog.FindById(m_selectedId);
		if (entry == nullptr) return false;
		if (!IsSlopeOk()) return false;

		MeshInsertInstance inst;
		inst.guid             = 0u;
		inst.gltfRelativePath = entry->gltfRelativePath;
		inst.worldPosition    = { m_targetWorldX, m_targetWorldY, m_targetWorldZ };
		// Tilt sur Z monde + yaw sur Y monde.
		inst.eulerRotationDeg = { 0.0f, m_wallNormalYawDeg, m_tiltDeg };
		inst.uniformScale     = m_uniformScale;
		inst.insertCategory   = "overhang";
		inst.displayName      = entry->displayName.empty() ? entry->id : entry->displayName;
		// Un overhang n'a pas de volume intérieur jouable (c'est un toit
		// rocheux). Pas d'eau ingress.
		inst.hasInteriorVolume   = false;
		inst.castsShadow         = m_castsShadow;
		inst.receivesAudioReverb = m_receivesAudioReverb;
		inst.allowsWaterIngress  = false;
		inst.lightProbeIntensity = m_lightProbeIntensity;

		auto cmd = std::make_unique<PlaceOverhangCommand>(*m_meshDoc, std::move(inst));
		m_stack->Push(std::move(cmd));
		return true;
	}

	void OverhangTool::Cancel()
	{
		m_selectedId.clear();
	}
}
