#include "src/world_editor/water/CoastlineEditorTool.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/water/CoastlineCliffs.h"
#include "src/world_editor/water/CoastlineCommand.h"
#include "src/world_editor/water/CoastlineSmoothing.h"
#include "src/world_editor/water/WaterDocument.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace engine::editor::world
{
	namespace
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		/// Marge externe du polygone océan en mètres (cf. spec §"Génération
		/// du polygone océan").
		constexpr float kOceanMarginMeters = 1000.0f;

		/// Construit un `ConsolidatedHeightGrid` à partir des chunks
		/// actuellement chargés dans `terrain`, sur une plage donnée
		/// (déterminée par les coords des chunks chargés). Pour M100.37
		/// MVP : on construit autour du chunk (0, 0) si chargé ; sinon
		/// retourne un grid vide. Le ticket d'optimisation pourra étendre
		/// pour zone-complète.
		ConsolidatedHeightGrid BuildGridFromLoadedChunks(
			TerrainDocument& terrain, const engine::core::Config& cfg)
		{
			ConsolidatedHeightGrid grid;
			grid.cellSizeMeters = engine::world::terrain::kTerrainCellSizeMeters;
			// MVP : on assemble juste 2×2 chunks autour de l'origine.
			constexpr int kChunksDim = 2;
			constexpr int kBaseX = 0;
			constexpr int kBaseZ = 0;
			grid.originCellX = kBaseX * (kRes - 1);
			grid.originCellZ = kBaseZ * (kRes - 1);
			const int W = kChunksDim * (kRes - 1) + 1;
			const int H = kChunksDim * (kRes - 1) + 1;
			grid.width = W;
			grid.height = H;
			grid.heights.assign(static_cast<size_t>(W) * H, 0.0f);
			for (int cz = 0; cz < kChunksDim; ++cz)
			{
				for (int cx = 0; cx < kChunksDim; ++cx)
				{
					auto chunk = terrain.EnsureLoaded(cfg, kBaseX + cx, kBaseZ + cz);
					if (!chunk) continue;
					const int baseX = cx * (kRes - 1);
					const int baseZ = cz * (kRes - 1);
					for (int iz = 0; iz < kRes; ++iz)
					{
						for (int ix = 0; ix < kRes; ++ix)
						{
							const int gx = baseX + ix;
							const int gz = baseZ + iz;
							if (gx >= W || gz >= H) continue;
							grid.heights[static_cast<size_t>(gz) * W + gx] =
								chunk->heights[static_cast<size_t>(iz) * kRes + ix];
						}
					}
				}
			}
			return grid;
		}
	}

	bool CoastlineEditorTool::Init(CommandStack& stack, TerrainDocument& terrain,
		WaterDocument& water, const engine::core::Config& cfg)
	{
		m_stack   = &stack;
		m_terrain = &terrain;
		m_water   = &water;
		m_cfg     = &cfg;
		Reset();
		return true;
	}

	void CoastlineEditorTool::Reset()
	{
		if (m_water != nullptr) m_oceanBuffer = m_water->GetOcean();
		m_smoothingEnabled       = false;
		m_smoothingBandMeters    = 5.0f;
		m_smoothingForce         = 0.3f;
		m_cliffsEnabled          = false;
		m_cliffsThresholdMeters  = 8.0f;
		m_cliffsSlopeThresholdDeg = 45.0f;
		m_cliffsLandSideMeters   = 6.0f;
		m_cliffsSeaSideMeters    = 3.0f;
		m_beachSplatEnabled      = false;
		m_beachLandBandMeters    = 8.0f;
		m_beachSeaBandMeters     = 3.0f;
		m_segments.clear();
		m_stats = CoastlineStats{};
	}

	void CoastlineEditorTool::RefreshPreview()
	{
		if (m_terrain == nullptr || m_cfg == nullptr) return;
		const ConsolidatedHeightGrid grid =
			BuildGridFromLoadedChunks(*m_terrain, *m_cfg);
		m_segments = ExtractCoastlineSegments(grid, m_oceanBuffer.seaLevelMeters);
		m_stats = ComputeCoastlineStats(grid, m_oceanBuffer.seaLevelMeters,
			m_beachLandBandMeters, m_beachSeaBandMeters,
			std::span<const CoastlineSegment>(m_segments));
	}

	bool CoastlineEditorTool::Apply()
	{
		if (m_stack == nullptr || m_terrain == nullptr || m_water == nullptr) return false;

		CoastlineCommand::ApplyData data;
		data.previousOcean = m_water->GetOcean();
		data.newOcean      = m_oceanBuffer;

		// Recherche d'un océan existant.
		auto& scene = m_water->Mutable();
		int existingIndex = -1;
		for (size_t i = 0; i < scene.lakes.size(); ++i)
		{
			if (scene.lakes[i].isOcean)
			{
				existingIndex = static_cast<int>(i);
				break;
			}
		}
		data.existingOceanIndex = existingIndex;
		if (existingIndex >= 0)
		{
			data.previousOceanLake = scene.lakes[static_cast<size_t>(existingIndex)];
		}
		else
		{
			// Crée le polygone océan : rectangle englobant la zone + marge
			// (5 vertices, sens horaire, dans l'espace zone).
			engine::world::water::LakeInstance lake;
			lake.name = "ocean";
			constexpr float kZoneSizeMeters =
				static_cast<float>(engine::world::kZoneSize);
			lake.polygon = {
				{ -kOceanMarginMeters, m_oceanBuffer.seaLevelMeters, -kOceanMarginMeters },
				{ kZoneSizeMeters + kOceanMarginMeters, m_oceanBuffer.seaLevelMeters, -kOceanMarginMeters },
				{ kZoneSizeMeters + kOceanMarginMeters, m_oceanBuffer.seaLevelMeters,  kZoneSizeMeters + kOceanMarginMeters },
				{ -kOceanMarginMeters, m_oceanBuffer.seaLevelMeters,  kZoneSizeMeters + kOceanMarginMeters },
				{ -kOceanMarginMeters, m_oceanBuffer.seaLevelMeters, -kOceanMarginMeters },
			};
			lake.waterLevelY = m_oceanBuffer.seaLevelMeters;
			lake.bottomColor = engine::math::Vec3{
				m_oceanBuffer.bottomColor[0],
				m_oceanBuffer.bottomColor[1],
				m_oceanBuffer.bottomColor[2] };
			lake.turbidity   = m_oceanBuffer.turbidity;
			lake.isOcean     = true;
			data.oceanToInsert = std::move(lake);
		}

		// Smoothing + falaises : assemblent leurs deltas à partir du grid
		// pristine (lecture seule).
		if (m_smoothingEnabled || m_cliffsEnabled)
		{
			const ConsolidatedHeightGrid grid =
				BuildGridFromLoadedChunks(*m_terrain, *m_cfg);
			if (m_smoothingEnabled)
			{
				auto smoothDeltas = ComputeCoastlineSmoothingDeltas(
					grid, m_oceanBuffer.seaLevelMeters,
					m_smoothingBandMeters, m_smoothingForce);
				for (auto& kv : smoothDeltas)
				{
					for (auto& cell : kv.second)
					{
						data.heightmapDeltas[kv.first][cell.first] += cell.second;
					}
				}
			}
			if (m_cliffsEnabled)
			{
				auto cliffDeltas = ComputeCoastlineCliffsDeltas(
					grid, m_oceanBuffer.seaLevelMeters,
					m_cliffsThresholdMeters, m_cliffsSlopeThresholdDeg,
					m_cliffsLandSideMeters, m_cliffsSeaSideMeters);
				for (auto& kv : cliffDeltas)
				{
					for (auto& cell : kv.second)
					{
						data.heightmapDeltas[kv.first][cell.first] += cell.second;
					}
				}
			}
		}

		auto cmd = std::make_unique<CoastlineCommand>(*m_terrain, *m_water,
			std::move(data));
		m_stack->Push(std::move(cmd));
		Reset();
		return true;
	}

	void CoastlineEditorTool::Cancel()
	{
		Reset();
	}
}
