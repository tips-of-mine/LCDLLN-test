#include "src/world_editor/volumes/caves/CaveTool.h"

#include "src/shared/core/Config.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/caves/PlaceCaveCommand.h"

#include <memory>
#include <utility>

namespace engine::editor::world::volumes::caves
{
	bool CaveTool::Init(engine::editor::world::CommandStack& stack,
		MeshInsertDocument& meshDoc,
		engine::editor::world::TerrainDocument& terrain,
		const engine::core::Config& cfg)
	{
		m_stack   = &stack;
		m_meshDoc = &meshDoc;
		m_terrain = &terrain;
		m_cfg     = &cfg;
		Reset();
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		LoadCatalog(contentRoot);
		return true;
	}

	void CaveTool::Reset()
	{
		m_selectedId.clear();
		m_targetWorldX = 0.0f;
		m_targetWorldY = 0.0f;
		m_targetWorldZ = 0.0f;
		m_rotationYDeg        = 0.0f;
		m_uniformScale        = 1.0f;
		m_snapToGround        = true;
		m_camouflageEnabled   = true;
		m_camouflageRadius    = 8.0f;
		m_camouflageStrength  = 0.6f;
		m_hasInteriorVolume   = true;
		m_receivesAudioReverb = true;
		m_allowsWaterIngress  = false;
		m_lightProbeIntensity = 0.4f;
	}

	void CaveTool::LoadCatalog(const std::string& contentRoot)
	{
		std::string err;
		(void)m_catalog.LoadFromContent(contentRoot, err);
		// Si erreur, on tolère silencieusement : le tool reste utilisable
		// (l'utilisateur ne voit juste pas de catalogue).
	}

	bool CaveTool::Place()
	{
		if (m_stack == nullptr || m_meshDoc == nullptr || m_terrain == nullptr) return false;
		if (m_selectedId.empty()) return false;
		const CaveCatalogEntry* entry = m_catalog.FindById(m_selectedId);
		if (entry == nullptr) return false;

		PlaceCaveCommand::Data data;
		MeshInsertInstance& inst = data.instance;
		inst.guid             = 0u;   // sera assigné par le document
		inst.gltfRelativePath = entry->gltfRelativePath;
		inst.worldPosition    = { m_targetWorldX, m_targetWorldY, m_targetWorldZ };
		// Snap au sol : ajuste worldPosition.y = terrain_y - entrancePoint.y
		// pour que entrancePoint coïncide avec le sol.
		if (m_snapToGround)
		{
			inst.worldPosition.y -= entry->entrancePoint.y;
		}
		inst.eulerRotationDeg = { 0.0f, m_rotationYDeg, 0.0f };
		inst.uniformScale     = m_uniformScale;
		inst.insertCategory   = "cave";
		inst.displayName      = entry->displayName.empty() ? entry->id : entry->displayName;
		inst.hasInteriorVolume   = m_hasInteriorVolume;
		inst.receivesAudioReverb = m_receivesAudioReverb;
		inst.allowsWaterIngress  = m_allowsWaterIngress;
		inst.lightProbeIntensity = m_lightProbeIntensity;

		if (m_camouflageEnabled)
		{
			CaveSplatPatch patch;
			patch.worldX       = m_targetWorldX;
			patch.worldZ       = m_targetWorldZ;
			patch.radiusMeters = m_camouflageRadius;
			patch.strength     = m_camouflageStrength;
			patch.splatLayer   = 5u; // "rock" par convention layer_palette M100.9
			data.splatPatch    = patch;

			// Précharge les splatmaps des chunks impactés.
			constexpr int kRes = static_cast<int>(
				engine::world::terrain::kTerrainResolution);
			const float chunkSpan =
				(kRes - 1) * engine::world::terrain::kTerrainCellSizeMeters;
			const float r = m_camouflageRadius;
			const int chunkXMin = static_cast<int>(std::floor((m_targetWorldX - r) / chunkSpan));
			const int chunkXMax = static_cast<int>(std::floor((m_targetWorldX + r) / chunkSpan));
			const int chunkZMin = static_cast<int>(std::floor((m_targetWorldZ - r) / chunkSpan));
			const int chunkZMax = static_cast<int>(std::floor((m_targetWorldZ + r) / chunkSpan));
			for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
			{
				for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
				{
					(void)m_terrain->EnsureSplatLoaded(*m_cfg, cx, cz);
				}
			}
		}

		auto cmd = std::make_unique<PlaceCaveCommand>(*m_meshDoc, *m_terrain,
			std::move(data));
		m_stack->Push(std::move(cmd));
		return true;
	}

	void CaveTool::Cancel()
	{
		m_selectedId.clear();
	}
}
