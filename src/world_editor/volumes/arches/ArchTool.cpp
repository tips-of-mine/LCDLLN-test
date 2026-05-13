#include "src/world_editor/volumes/arches/ArchTool.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/arches/PlaceArchCommand.h"

#include <cmath>
#include <memory>
#include <utility>

namespace engine::editor::world::volumes::arches
{
	namespace
	{
		constexpr float kRadToDeg = 57.29577951308232f;
	}

	bool ArchTool::Init(engine::editor::world::CommandStack& stack,
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

	void ArchTool::Reset()
	{
		m_selectedId.clear();
		m_pointAX = m_pointAY = m_pointAZ = 0.0f;
		m_pointBX = 10.0f; m_pointBY = 0.0f; m_pointBZ = 0.0f;
		m_castsShadow         = true;
		m_lightProbeIntensity = 1.0f;
		m_minScaleRatio       = 0.25f;
		m_maxScaleRatio       = 4.0f;
	}

	void ArchTool::LoadCatalog(const std::string& contentRoot)
	{
		std::string err;
		(void)m_catalog.LoadFromContent(contentRoot, err);
	}

	float ArchTool::SpanMeters() const
	{
		const float dx = m_pointBX - m_pointAX;
		const float dz = m_pointBZ - m_pointAZ;
		return std::sqrt(dx * dx + dz * dz);
	}

	float ArchTool::DerivedScale() const
	{
		if (m_selectedId.empty()) return 1.0f;
		const ArchCatalogEntry* entry = m_catalog.FindById(m_selectedId);
		if (entry == nullptr) return 1.0f;
		const float native = entry->NativeSpanMeters();
		if (native < 0.001f) return 1.0f;
		return SpanMeters() / native;
	}

	float ArchTool::DerivedYawDeg() const
	{
		const float dx = m_pointBX - m_pointAX;
		const float dz = m_pointBZ - m_pointAZ;
		return std::atan2(dz, dx) * kRadToDeg;
	}

	bool ArchTool::Place()
	{
		if (m_stack == nullptr || m_meshDoc == nullptr) return false;
		if (m_selectedId.empty()) return false;
		const ArchCatalogEntry* entry = m_catalog.FindById(m_selectedId);
		if (entry == nullptr) return false;
		const float scale = DerivedScale();
		if (scale < m_minScaleRatio || scale > m_maxScaleRatio) return false;

		MeshInsertInstance inst;
		inst.guid             = 0u;
		inst.gltfRelativePath = entry->gltfRelativePath;
		// Position = milieu du segment AB.
		inst.worldPosition.x  = 0.5f * (m_pointAX + m_pointBX);
		inst.worldPosition.y  = 0.5f * (m_pointAY + m_pointBY);
		inst.worldPosition.z  = 0.5f * (m_pointAZ + m_pointBZ);
		inst.eulerRotationDeg = { 0.0f, DerivedYawDeg(), 0.0f };
		inst.uniformScale     = scale;
		inst.insertCategory   = "arch";
		inst.displayName      = entry->displayName.empty() ? entry->id : entry->displayName;
		inst.hasInteriorVolume   = false;
		inst.castsShadow         = m_castsShadow;
		inst.receivesAudioReverb = false;
		inst.allowsWaterIngress  = false;
		inst.lightProbeIntensity = m_lightProbeIntensity;

		auto cmd = std::make_unique<PlaceArchCommand>(*m_meshDoc, std::move(inst));
		m_stack->Push(std::move(cmd));
		return true;
	}

	void ArchTool::Cancel()
	{
		m_selectedId.clear();
	}
}
