#include "src/world_editor/SplatPaintTool.h"

#include "src/world_editor/SplatRules.h"
#include "src/world_editor/TerrainRaycast.h"
#include "src/client/world/terrain/SplatMap.h"
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
		/// brushstroke et par auto-rules apply. Démarre à 1 (la valeur 0
		/// signifie "pas de fusion" dans l'API CommandStack).
		std::atomic<uint64_t> g_nextStrokeId{1};

		/// Taille d'un chunk en mètres (256 m × 256 m). Aligné sur le sculpt
		/// (M100.6) : la résolution heightmap est 257² avec cellSize=1m, donc
		/// le span vaut (kTerrainResolution - 1) cellules.
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;

		/// Dernier index valide d'une cellule de splat sur un axe.
		constexpr int kLastSplatIndex =
			static_cast<int>(engine::world::terrain::kSplatResolution) - 1;

		/// Convertit une coordonnée monde en (chunk, local) via floor div.
		void WorldToChunkLocal(float worldXOrZ, int& outChunk, float& outLocal)
		{
			outChunk = static_cast<int>(std::floor(worldXOrZ / kChunkSpanMeters));
			outLocal = worldXOrZ - static_cast<float>(outChunk) * kChunkSpanMeters;
		}

		/// Smoothstep canonique `[0..1]` : 0 si t<=0, 1 si t>=1, lissage
		/// 3t²-2t³ entre les deux. Sert au falloff radial de la brosse.
		float SmoothStep01(float t)
		{
			if (t <= 0.0f) return 0.0f;
			if (t >= 1.0f) return 1.0f;
			return t * t * (3.0f - 2.0f * t);
		}

		/// Calcule le poids de la brosse au point distant `dist` (en mètres)
		/// du centre, pour un rayon `radius` et un facteur `falloff` `[0..1]`.
		///
		/// - `dist >= radius` : 0 (hors brosse).
		/// - `dist <= innerRadius = radius * (1 - falloff)` : 1 (plein effet).
		/// - entre les deux : smoothstep décroissant.
		///
		/// La spec M100.10 suggère : `weight = smoothstep(radius, radius*(1-falloff), dist)`.
		float ComputeFalloffWeight(float dist, float radius, float falloff)
		{
			if (radius <= 0.0f) return 0.0f;
			if (dist <= 0.0f) return 1.0f;
			if (dist >= radius) return 0.0f;
			const float clampedFalloff = std::clamp(falloff, 0.0f, 1.0f);
			const float innerRadius = radius * (1.0f - clampedFalloff);
			if (dist <= innerRadius) return 1.0f;
			// Mappe (innerRadius..radius) -> (1..0) via smoothstep décroissant.
			const float denom = std::max(1e-6f, radius - innerRadius);
			const float t = 1.0f - (dist - innerRadius) / denom;
			return SmoothStep01(t);
		}

		/// Ajoute `delta` à la layer `activeLayer` puis renormalise pour somme=255.
		/// Si l'ajout déborde 255 ou si la somme totale dépasse 255, on
		/// décrémente proportionnellement les autres layers (sumOthers > 0).
		/// Si `sumOthers == 0` (cellule pure layer active), on cap simplement
		/// à 255 sur la layer active.
		///
		/// \param weights Poids `[8]` lus puis modifiés en place.
		/// \param activeLayer Index `[0..7]` (clampé à 0 si hors range).
		/// \param delta Quantité à ajouter (`uint8` : `[0..255]`).
		void ApplyDeltaAndRenormalize(std::array<uint8_t, 8>& weights,
			uint8_t activeLayer, uint8_t delta)
		{
			if (activeLayer >= 8) activeLayer = 0;
			if (delta == 0)
			{
				// Pas de changement, mais on s'assure que la somme reste 255
				// (no-op si l'invariant est respecté en entrée).
				return;
			}

			// 1) Boost sur la layer active, saturé à 255.
			const int oldActive = static_cast<int>(weights[activeLayer]);
			const int newActive = std::min(255, oldActive + static_cast<int>(delta));
			weights[activeLayer] = static_cast<uint8_t>(newActive);

			// 2) Calcul de la somme actuelle. Si elle dépasse 255 (cas typique
			// quand la cellule était mixte), on retire l'excès aux autres layers.
			int total = 0;
			for (uint32_t l = 0; l < 8; ++l) total += static_cast<int>(weights[l]);
			int excess = total - 255;
			if (excess <= 0)
			{
				// Sous-jacent : il y avait initialement somme=255 et on a
				// ajouté `delta` qui a été saturé à `(255 - oldActive)` ; le
				// total ne peut pas être < 255 sauf si l'invariant était
				// violé en entrée. On compense en injectant le déficit dans
				// la layer active pour atteindre exactement 255.
				if (excess < 0)
				{
					const int deficit = -excess;
					const int updated = std::min(255, static_cast<int>(weights[activeLayer]) + deficit);
					weights[activeLayer] = static_cast<uint8_t>(updated);
				}
				return;
			}

			// 3) Décrément proportionnel sur les "autres" layers.
			int sumOthers = 0;
			for (uint32_t l = 0; l < 8; ++l)
			{
				if (l == activeLayer) continue;
				sumOthers += static_cast<int>(weights[l]);
			}

			if (sumOthers == 0)
			{
				// Cellule était pure layer active : on cap simplement à 255.
				weights[activeLayer] = 255;
				return;
			}

			// On retire `excess` aux autres layers, proportionnellement à
			// leur poids. Pour éviter les arrondis qui violeraient l'invariant,
			// on accumule l'erreur et on l'absorbe à la fin sur la dernière
			// layer non-active non-nulle.
			int removed = 0;
			int lastNonActiveNonZero = -1;
			for (uint32_t l = 0; l < 8; ++l)
			{
				if (l == static_cast<uint32_t>(activeLayer)) continue;
				const int w = static_cast<int>(weights[l]);
				if (w == 0) continue;
				lastNonActiveNonZero = static_cast<int>(l);
				// dec = round(excess * w / sumOthers) — borné par w pour ne pas
				// passer en négatif.
				const int dec = std::min(w, (excess * w + sumOthers / 2) / sumOthers);
				weights[l] = static_cast<uint8_t>(w - dec);
				removed += dec;
			}

			// Absorbe l'erreur d'arrondi sur la dernière layer non-active.
			int residual = excess - removed;
			if (residual != 0 && lastNonActiveNonZero >= 0)
			{
				int w = static_cast<int>(weights[lastNonActiveNonZero]);
				if (residual > 0)
				{
					const int dec = std::min(w, residual);
					weights[lastNonActiveNonZero] = static_cast<uint8_t>(w - dec);
					residual -= dec;
				}
				else
				{
					// residual < 0 : on a trop retiré, on rend le complément.
					const int give = std::min(255 - w, -residual);
					weights[lastNonActiveNonZero] = static_cast<uint8_t>(w + give);
					residual += give;
				}
			}

			// 4) Sécurité : si la somme n'est pas exactement 255, on rééquilibre
			// sur la layer active.
			int finalTotal = 0;
			for (uint32_t l = 0; l < 8; ++l) finalTotal += static_cast<int>(weights[l]);
			const int diff = 255 - finalTotal;
			if (diff != 0)
			{
				int w = static_cast<int>(weights[activeLayer]);
				const int updated = std::clamp(w + diff, 0, 255);
				weights[activeLayer] = static_cast<uint8_t>(updated);
			}
		}

		/// Lit les 8 poids de la cellule `(x, z)` du splat.
		void ReadCellWeights(const engine::world::terrain::SplatMap& splat,
			uint16_t x, uint16_t z, std::array<uint8_t, 8>& out)
		{
			const uint32_t res = splat.resolution;
			const uint32_t lc  = splat.layerCount;
			const size_t base = (static_cast<size_t>(z) * res + x) * lc;
			for (uint32_t l = 0; l < 8; ++l)
			{
				out[l] = (l < lc && base + l < splat.weights.size())
					? splat.weights[base + l] : 0;
			}
		}

		/// Écrit les 8 poids dans la cellule `(x, z)` du splat.
		void WriteCellWeights(engine::world::terrain::SplatMap& splat,
			uint16_t x, uint16_t z, const std::array<uint8_t, 8>& weights)
		{
			const uint32_t res = splat.resolution;
			const uint32_t lc  = splat.layerCount;
			const size_t base = (static_cast<size_t>(z) * res + x) * lc;
			for (uint32_t l = 0; l < 8 && l < lc; ++l)
			{
				if (base + l < splat.weights.size())
				{
					splat.weights[base + l] = weights[l];
				}
			}
		}
	}

	bool SplatPaintTool::Init(CommandStack& stack, TerrainDocument& doc)
	{
		m_stack = &stack;
		m_doc = &doc;
		m_pressing = false;
		m_inFlight.clear();
		return true;
	}

	std::vector<SplatDeltaCell>& SplatPaintTool::EnsureInFlightCells(
		engine::world::GlobalChunkCoord coord)
	{
		for (auto& slot : m_inFlight)
		{
			if (slot.coord == coord) return slot.cells;
		}
		SplatDeltaChunk fresh;
		fresh.coord = coord;
		m_inFlight.push_back(std::move(fresh));
		return m_inFlight.back().cells;
	}

	SplatDeltaCell* SplatPaintTool::FindInFlightCell(
		engine::world::GlobalChunkCoord coord, uint16_t x, uint16_t z)
	{
		for (auto& slot : m_inFlight)
		{
			if (slot.coord != coord) continue;
			for (auto& cell : slot.cells)
			{
				if (cell.x == x && cell.z == z) return &cell;
			}
			return nullptr;
		}
		return nullptr;
	}

	void SplatPaintTool::ApplyTickAtWorldPoint(float worldX, float worldZ,
		const engine::core::Config& cfg, float dtSeconds)
	{
		if (!m_doc || !m_pressing) return;

		const float radius = std::max(0.0f, m_params.radiusMeters);
		if (radius <= 0.0f) return;

		// Box monde du brush → plage de chunks.
		const float wxMin = worldX - radius;
		const float wxMax = worldX + radius;
		const float wzMin = worldZ - radius;
		const float wzMax = worldZ + radius;

		const int chunkXMin = static_cast<int>(std::floor(wxMin / kChunkSpanMeters));
		const int chunkXMax = static_cast<int>(std::floor(wxMax / kChunkSpanMeters));
		const int chunkZMin = static_cast<int>(std::floor(wzMin / kChunkSpanMeters));
		const int chunkZMax = static_cast<int>(std::floor(wzMax / kChunkSpanMeters));

		const float strengthDt = std::clamp(m_params.strength * dtSeconds, 0.0f, 1.0f);
		if (strengthDt <= 0.0f) return;

		const uint8_t activeLayer =
			static_cast<uint8_t>(std::min<uint32_t>(m_params.activeLayer, 7));

		for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
		{
			for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
			{
				auto chunkPtr = m_doc->EnsureLoaded(cfg, cx, cz);
				auto splatPtr = m_doc->EnsureSplatLoaded(cfg, cx, cz);
				if (!chunkPtr || !splatPtr) continue;
				engine::world::terrain::TerrainChunk& chunk = *chunkPtr;
				engine::world::terrain::SplatMap& splat = *splatPtr;

				const float chunkOriginX = static_cast<float>(cx) * kChunkSpanMeters;
				const float chunkOriginZ = static_cast<float>(cz) * kChunkSpanMeters;
				const float localCenterX = worldX - chunkOriginX;
				const float localCenterZ = worldZ - chunkOriginZ;

				// Plage de cellules splat à inspecter (cellSize splat = 1m si
				// kSplatResolution == kTerrainResolution avec cellSize=1m).
				const float cellSize = chunk.cellSizeMeters;
				const float invCell = (cellSize > 0.0f) ? 1.0f / cellSize : 1.0f;
				const int xCellMin = std::max(0,
					static_cast<int>(std::floor((localCenterX - radius) * invCell)));
				const int xCellMax = std::min(kLastSplatIndex,
					static_cast<int>(std::ceil((localCenterX + radius) * invCell)));
				const int zCellMin = std::max(0,
					static_cast<int>(std::floor((localCenterZ - radius) * invCell)));
				const int zCellMax = std::min(kLastSplatIndex,
					static_cast<int>(std::ceil((localCenterZ + radius) * invCell)));

				const engine::world::GlobalChunkCoord coord{cx, cz};

				for (int zi = zCellMin; zi <= zCellMax; ++zi)
				{
					for (int xi = xCellMin; xi <= xCellMax; ++xi)
					{
						const float cellWorldX = static_cast<float>(xi) * cellSize;
						const float cellWorldZ = static_cast<float>(zi) * cellSize;
						const float dx = cellWorldX - localCenterX;
						const float dz = cellWorldZ - localCenterZ;
						const float dist = std::sqrt(dx * dx + dz * dz);
						if (dist > radius) continue;

						// Filtre auto-rules si activé.
						if (m_params.autoRules)
						{
							if (!MatchesRules(chunk,
								static_cast<uint32_t>(xi),
								static_cast<uint32_t>(zi),
								m_params.slopeMinDeg, m_params.slopeMaxDeg,
								m_params.altMin, m_params.altMax))
							{
								continue;
							}
						}

						const float falloffWeight =
							ComputeFalloffWeight(dist, radius, m_params.falloff);
						const float weight = std::clamp(falloffWeight * strengthDt, 0.0f, 1.0f);
						if (weight <= 0.0f) continue;
						const uint8_t delta = static_cast<uint8_t>(
							std::clamp(static_cast<int>(weight * 255.0f + 0.5f), 0, 255));
						if (delta == 0) continue;

						const uint16_t ux = static_cast<uint16_t>(xi);
						const uint16_t uz = static_cast<uint16_t>(zi);

						// Cherche cellule existante : si présente, on met à
						// jour `next` (live preview) ; sinon on l'enregistre
						// avec `prev` lu une seule fois (au tout premier tick).
						SplatDeltaCell* existing = FindInFlightCell(coord, ux, uz);
						std::array<uint8_t, 8> currentNext;
						if (existing)
						{
							currentNext = existing->next;
						}
						else
						{
							ReadCellWeights(splat, ux, uz, currentNext);
						}
						std::array<uint8_t, 8> updated = currentNext;
						ApplyDeltaAndRenormalize(updated, activeLayer, delta);

						if (existing)
						{
							existing->next = updated;
						}
						else
						{
							SplatDeltaCell newCell{};
							newCell.x = ux;
							newCell.z = uz;
							newCell.prev = currentNext; // = lecture courante
							newCell.next = updated;
							auto& cells = EnsureInFlightCells(coord);
							cells.push_back(newCell);
						}

						// Live-preview : appliquer aussi la valeur dans la
						// splat-map directement, pour que le rendu (si
						// branché) voie la modification immédiatement et
						// que les ticks suivants partent du bon `prev`.
						WriteCellWeights(splat, ux, uz, updated);

						// Couture inter-chunks : si on est sur un bord, écrire
						// la cellule miroir du chunk voisin avec les mêmes
						// valeurs `prev`/`next`.
						const bool onLeft   = (xi == 0);
						const bool onRight  = (xi == kLastSplatIndex);
						const bool onBottom = (zi == 0);
						const bool onTop    = (zi == kLastSplatIndex);
						if (!(onLeft || onRight || onBottom || onTop)) continue;

						struct Neigh { int dcx; int dcz; int nx; int nz; };
						Neigh neighbours[3]{};
						int nCount = 0;
						if (onLeft)
						{
							neighbours[nCount++] = {-1, 0, kLastSplatIndex, zi};
						}
						if (onRight)
						{
							neighbours[nCount++] = {+1, 0, 0, zi};
						}
						if (onBottom)
						{
							neighbours[nCount++] = {0, -1, xi, kLastSplatIndex};
						}
						if (onTop)
						{
							neighbours[nCount++] = {0, +1, xi, 0};
						}
						if ((onLeft || onRight) && (onBottom || onTop) && nCount >= 2)
						{
							const int dcx = onLeft ? -1 : +1;
							const int dcz = onBottom ? -1 : +1;
							const int nxk = onLeft ? kLastSplatIndex : 0;
							const int nzk = onBottom ? kLastSplatIndex : 0;
							neighbours[nCount++] = {dcx, dcz, nxk, nzk};
						}

						for (int i = 0; i < nCount; ++i)
						{
							const Neigh& nb = neighbours[i];
							const engine::world::GlobalChunkCoord ncoord{
								cx + nb.dcx, cz + nb.dcz};
							auto neighSplat = m_doc->EnsureSplatLoaded(cfg,
								ncoord.x, ncoord.z);
							if (!neighSplat) continue;
							const uint16_t nx = static_cast<uint16_t>(nb.nx);
							const uint16_t nz = static_cast<uint16_t>(nb.nz);

							SplatDeltaCell* mirror = FindInFlightCell(ncoord, nx, nz);
							std::array<uint8_t, 8> mirrorPrev{};
							if (mirror)
							{
								mirror->next = updated;
							}
							else
							{
								ReadCellWeights(*neighSplat, nx, nz, mirrorPrev);
								SplatDeltaCell mc{};
								mc.x = nx;
								mc.z = nz;
								mc.prev = mirrorPrev;
								mc.next = updated;
								auto& neighCells = EnsureInFlightCells(ncoord);
								neighCells.push_back(mc);
							}
							WriteCellWeights(*neighSplat, nx, nz, updated);
						}
					}
				}
			}
		}
	}

	void SplatPaintTool::OnMouseDown(const engine::render::Camera& cam,
		int sx, int sy, int vw, int vh,
		const engine::core::Config& cfg, float dtSeconds)
	{
		if (!m_doc || !m_stack) return;
		m_strokeId = g_nextStrokeId.fetch_add(1, std::memory_order_relaxed);
		m_inFlight.clear();
		m_pressing = true;

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

	void SplatPaintTool::OnMouseMove(const engine::render::Camera& cam,
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

	void SplatPaintTool::OnMouseUp()
	{
		if (!m_pressing) return;
		m_pressing = false;
		if (!m_stack || !m_doc || m_inFlight.empty())
		{
			m_inFlight.clear();
			return;
		}

		// Note importante : le live-preview a DÉJÀ écrit les `next` dans la
		// splat-map (cf. `ApplyTickAtWorldPoint`). `Push` va appeler `Execute`
		// qui réécrira les mêmes `next` — idempotent (contrairement au sculpt
		// où `Execute` ajoute au lieu d'assigner). On peut donc pousser tel
		// quel sans avoir à "rejouer" le delta inverse au préalable.
		auto cmd = std::make_unique<SplatPaintCommand>(*m_doc,
			std::move(m_inFlight), m_strokeId);
		m_stack->Push(std::move(cmd));
		m_inFlight.clear();
	}

	void SplatPaintTool::ApplyAutoRulesToChunk(const engine::core::Config& cfg,
		int chunkX, int chunkZ)
	{
		if (!m_doc || !m_stack) return;
		auto chunkPtr = m_doc->EnsureLoaded(cfg, chunkX, chunkZ);
		auto splatPtr = m_doc->EnsureSplatLoaded(cfg, chunkX, chunkZ);
		if (!chunkPtr || !splatPtr) return;
		engine::world::terrain::TerrainChunk& chunk = *chunkPtr;
		engine::world::terrain::SplatMap& splat = *splatPtr;

		const uint8_t activeLayer =
			static_cast<uint8_t>(std::min<uint32_t>(m_params.activeLayer, 7));
		const float strength = std::clamp(m_params.strength, 0.0f, 1.0f);
		const uint8_t delta = static_cast<uint8_t>(
			std::clamp(static_cast<int>(strength * 255.0f + 0.5f), 0, 255));
		if (delta == 0) return;

		SplatDeltaChunk dc;
		dc.coord = engine::world::GlobalChunkCoord{chunkX, chunkZ};

		const uint32_t res = engine::world::terrain::kSplatResolution;
		for (uint32_t z = 0; z < res; ++z)
		{
			for (uint32_t x = 0; x < res; ++x)
			{
				if (!MatchesRules(chunk, x, z,
					m_params.slopeMinDeg, m_params.slopeMaxDeg,
					m_params.altMin, m_params.altMax))
				{
					continue;
				}
				std::array<uint8_t, 8> prev{};
				ReadCellWeights(splat,
					static_cast<uint16_t>(x), static_cast<uint16_t>(z), prev);
				std::array<uint8_t, 8> next = prev;
				ApplyDeltaAndRenormalize(next, activeLayer, delta);
				if (next == prev) continue;

				SplatDeltaCell cell{};
				cell.x = static_cast<uint16_t>(x);
				cell.z = static_cast<uint16_t>(z);
				cell.prev = prev;
				cell.next = next;
				dc.cells.push_back(cell);
				// Live-write pour que `Execute` à venir soit idempotent.
				WriteCellWeights(splat,
					static_cast<uint16_t>(x), static_cast<uint16_t>(z), next);
			}
		}

		if (dc.cells.empty()) return;
		std::vector<SplatDeltaChunk> deltas;
		deltas.push_back(std::move(dc));
		// mergeKey unique → non-fusionnable avec d'autres apply.
		const uint64_t key = g_nextStrokeId.fetch_add(1, std::memory_order_relaxed);
		auto cmd = std::make_unique<SplatPaintCommand>(*m_doc,
			std::move(deltas), key);
		m_stack->Push(std::move(cmd));
	}
}
