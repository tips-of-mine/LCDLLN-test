#include "src/world_editor/terrain/TerrainSculptTool.h"

#include "src/world_editor/terrain/TerrainRaycast.h"
#include "src/world_editor/terrain/TerrainSculptCommand.h"
#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace engine::editor::world
{
	namespace
	{
		/// Compteur global monotone pour générer les `mergeKey` uniques par
		/// brushstroke. Démarre à 1 (la valeur 0 signifie "pas de fusion"
		/// dans l'API CommandStack).
		std::atomic<uint64_t> g_nextStrokeId{1};

		/// Taille d'un chunk en mètres (256 m × 256 m). Le bord à `x = 256`
		/// est partagé avec le chunk voisin `(i+1, j)` à `x = 0` — c'est ce
		/// recoupement qui garantit la couture sans faille.
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;

		/// Résolution moins 1 (dernier index valide en X ou Z).
		constexpr int kLastIndex =
			static_cast<int>(engine::world::terrain::kTerrainResolution) - 1;

		/// Convertit une coordonnée monde en (chunk, local) via floor div. Les
		/// coordonnées négatives sont supportées (chunks à gauche/derrière
		/// l'origine).
		void WorldToChunkLocal(float worldXOrZ, int& outChunk, float& outLocal)
		{
			outChunk = static_cast<int>(std::floor(worldXOrZ / kChunkSpanMeters));
			outLocal = worldXOrZ - static_cast<float>(outChunk) * kChunkSpanMeters;
		}
	}

	bool TerrainSculptTool::Init(CommandStack& stack, TerrainDocument& doc)
	{
		m_stack = &stack;
		m_doc = &doc;
		m_pressing = false;
		m_inFlight.clear();
		return true;
	}

	std::vector<TerrainSculptDeltaCell>& TerrainSculptTool::EnsureInFlightCells(
		engine::world::GlobalChunkCoord coord)
	{
		for (auto& slot : m_inFlight)
		{
			if (slot.coord == coord) return slot.cells;
		}
		TerrainSculptDeltaChunk fresh;
		fresh.coord = coord;
		m_inFlight.push_back(std::move(fresh));
		return m_inFlight.back().cells;
	}

	void TerrainSculptTool::ApplyTickAtWorldPoint(float worldX, float worldZ,
		const engine::core::Config& cfg, float dtSeconds)
	{
		if (!m_doc || !m_pressing) return;

		const float radius = std::max(0.0f, m_params.radiusMeters);
		if (radius <= 0.0f) return;

		// Détermine la box monde du brush et la convertit en plage de chunks.
		const float wxMin = worldX - radius;
		const float wxMax = worldX + radius;
		const float wzMin = worldZ - radius;
		const float wzMax = worldZ + radius;

		const int chunkXMin = static_cast<int>(std::floor(wxMin / kChunkSpanMeters));
		const int chunkXMax = static_cast<int>(std::floor(wxMax / kChunkSpanMeters));
		const int chunkZMin = static_cast<int>(std::floor(wzMin / kChunkSpanMeters));
		const int chunkZMax = static_cast<int>(std::floor(wzMax / kChunkSpanMeters));

		for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
		{
			for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
			{
				auto chunkPtr = m_doc->EnsureLoaded(cfg, cx, cz);
				if (!chunkPtr) continue;
				engine::world::terrain::TerrainChunk& chunk = *chunkPtr;

				// Centre exprimé dans le repère chunk-local.
				const float localX = worldX - static_cast<float>(cx) * kChunkSpanMeters;
				const float localZ = worldZ - static_cast<float>(cz) * kChunkSpanMeters;

				std::vector<TerrainSculptDeltaCell> applied;
				const uint32_t modified = ApplyBrushKernel(
					chunk, m_params, localX, localZ, dtSeconds, applied);
				if (modified == 0) continue;

				// Couture inter-chunks : pour toute cellule sur un bord du
				// chunk courant, propager la même valeur dans la cellule
				// miroir du chunk voisin (ce qui force la rangée à matcher).
				const engine::world::GlobalChunkCoord coord{cx, cz};
				auto& destCells = EnsureInFlightCells(coord);
				destCells.insert(destCells.end(), applied.begin(), applied.end());

				for (const auto& c : applied)
				{
					const bool onLeft   = (c.x == 0);
					const bool onRight  = (c.x == static_cast<uint16_t>(kLastIndex));
					const bool onBottom = (c.z == 0);
					const bool onTop    = (c.z == static_cast<uint16_t>(kLastIndex));
					if (!(onLeft || onRight || onBottom || onTop)) continue;

					// Construit la liste des chunks voisins à mettre à jour.
					struct Neigh { int dcx; int dcz; int nx; int nz; };
					Neigh neighbours[3]{};
					int nCount = 0;
					if (onLeft)
					{
						neighbours[nCount++] = {-1, 0, kLastIndex, c.z};
					}
					if (onRight)
					{
						neighbours[nCount++] = {+1, 0, 0, c.z};
					}
					if (onBottom)
					{
						neighbours[nCount++] = {0, -1, c.x, kLastIndex};
					}
					if (onTop)
					{
						neighbours[nCount++] = {0, +1, c.x, 0};
					}
					// Coins : combiner X et Z (deux voisins déjà ajoutés +
					// un coin diagonal).
					if ((onLeft || onRight) && (onBottom || onTop) && nCount >= 2)
					{
						const int dcx = onLeft ? -1 : +1;
						const int dcz = onBottom ? -1 : +1;
						const int nx = onLeft ? kLastIndex : 0;
						const int nz = onBottom ? kLastIndex : 0;
						neighbours[nCount++] = {dcx, dcz, nx, nz};
					}

					for (int i = 0; i < nCount; ++i)
					{
						const Neigh& nb = neighbours[i];
						const engine::world::GlobalChunkCoord ncoord{
							cx + nb.dcx, cz + nb.dcz};
						auto neighChunk = m_doc->EnsureLoaded(cfg,
							ncoord.x, ncoord.z);
						if (!neighChunk) continue;
						const size_t idx = static_cast<size_t>(nb.nz) *
							neighChunk->resolutionX + nb.nx;
						if (idx >= neighChunk->heights.size()) continue;
						const float oldH = neighChunk->heights[idx];
						const float newH = std::clamp(oldH + c.deltaMeters,
							engine::world::terrain::kTerrainHeightMinMeters,
							engine::world::terrain::kTerrainHeightMaxMeters);
						const float effectiveDelta = newH - oldH;
						if (effectiveDelta == 0.0f) continue;
						neighChunk->heights[idx] = newH;
						auto& neighCells = EnsureInFlightCells(ncoord);
						TerrainSculptDeltaCell mirror;
						mirror.x = static_cast<uint16_t>(nb.nx);
						mirror.z = static_cast<uint16_t>(nb.nz);
						mirror.deltaMeters = effectiveDelta;
						neighCells.push_back(mirror);
					}
				}
			}
		}
	}

	void TerrainSculptTool::OnMouseDown(const engine::render::Camera& cam,
		int sx, int sy, int vw, int vh,
		const engine::core::Config& cfg, float dtSeconds)
	{
		if (!m_doc || !m_stack) return;
		m_strokeId = g_nextStrokeId.fetch_add(1, std::memory_order_relaxed);
		m_inFlight.clear();
		m_pressing = true;

		// Raycast vers le terrain via callback de sample (lazy load).
		auto sampler = [this, &cfg](float wx, float wz) -> float
		{
			int cxi, czi;
			float localX, localZ;
			WorldToChunkLocal(wx, cxi, localX);
			WorldToChunkLocal(wz, czi, localZ);
			auto chunkPtr = m_doc->EnsureLoaded(cfg, cxi, czi);
			if (!chunkPtr) return 0.0f;
			return chunkPtr->SampleHeight(localX, localZ);
		};
		const TerrainHit hit = RaycastTerrain(cam, sx, sy, vw, vh, sampler);
		if (!hit.hit) return;
		ApplyTickAtWorldPoint(hit.worldX, hit.worldZ, cfg, dtSeconds);
	}

	void TerrainSculptTool::OnMouseMove(const engine::render::Camera& cam,
		int sx, int sy, int vw, int vh,
		const engine::core::Config& cfg, float dtSeconds)
	{
		if (!m_pressing) return;
		auto sampler = [this, &cfg](float wx, float wz) -> float
		{
			int cxi, czi;
			float localX, localZ;
			WorldToChunkLocal(wx, cxi, localX);
			WorldToChunkLocal(wz, czi, localZ);
			auto chunkPtr = m_doc->EnsureLoaded(cfg, cxi, czi);
			if (!chunkPtr) return 0.0f;
			return chunkPtr->SampleHeight(localX, localZ);
		};
		const TerrainHit hit = RaycastTerrain(cam, sx, sy, vw, vh, sampler);
		if (!hit.hit) return;
		ApplyTickAtWorldPoint(hit.worldX, hit.worldZ, cfg, dtSeconds);
	}

	void TerrainSculptTool::OnMouseUp()
	{
		if (!m_pressing) return;
		m_pressing = false;
		if (!m_stack || !m_doc || m_inFlight.empty())
		{
			m_inFlight.clear();
			return;
		}

		// Important : on a DÉJÀ appliqué les deltas dans `chunk.heights`
		// pendant le stroke. `Push` va appeler `Execute()` une nouvelle fois,
		// ce qui doublerait l'effet. On retire donc les deltas du chunk
		// avant Push pour que Execute() les rejoue proprement, garantissant
		// la symétrie Execute/Undo (et le bon comportement de Undo après
		// Push même sans avoir touché la souris une seule fois).
		for (const auto& deltaChunk : m_inFlight)
		{
			auto chunk = m_doc->Find(deltaChunk.coord);
			if (!chunk) continue;
			const uint32_t resX = chunk->resolutionX;
			for (auto it = deltaChunk.cells.rbegin();
				 it != deltaChunk.cells.rend(); ++it)
			{
				const auto& cell = *it;
				const size_t idx = static_cast<size_t>(cell.z) * resX + cell.x;
				if (idx < chunk->heights.size())
				{
					chunk->heights[idx] -= cell.deltaMeters;
				}
			}
		}

		auto cmd = std::make_unique<TerrainSculptCommand>(*m_doc,
			std::move(m_inFlight), m_strokeId);
		m_stack->Push(std::move(cmd));
		m_inFlight.clear();
	}
}
