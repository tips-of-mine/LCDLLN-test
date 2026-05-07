#include "engine/editor/world/TerrainStampTool.h"

#include "engine/editor/world/StampLibrary.h"
#include "engine/editor/world/TerrainStampCommand.h"
#include "engine/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace engine::editor::world
{
	namespace
	{
		/// Taille d'un chunk en mètres (256 m × 256 m). Identique à la
		/// constante côté `TerrainSculptTool`.
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;

		/// Échantillonne bilinéairement la grille `src` carrée `srcRes × srcRes`
		/// au point flottant `(u, v)` (en cellules, repère row-major). Renvoie
		/// 0 hors-grille.
		float SampleBilinear(const std::vector<float>& src, uint32_t srcRes,
			float u, float v)
		{
			if (src.empty() || srcRes == 0) return 0.0f;
			if (u < 0.0f || v < 0.0f) return 0.0f;
			const float maxIdx = static_cast<float>(srcRes - 1);
			if (u > maxIdx || v > maxIdx) return 0.0f;
			const int x0 = static_cast<int>(std::floor(u));
			const int z0 = static_cast<int>(std::floor(v));
			const int x1 = std::min(x0 + 1, static_cast<int>(srcRes - 1));
			const int z1 = std::min(z0 + 1, static_cast<int>(srcRes - 1));
			const float tx = u - static_cast<float>(x0);
			const float tz = v - static_cast<float>(z0);
			const float h00 = src[static_cast<size_t>(z0) * srcRes + x0];
			const float h10 = src[static_cast<size_t>(z0) * srcRes + x1];
			const float h01 = src[static_cast<size_t>(z1) * srcRes + x0];
			const float h11 = src[static_cast<size_t>(z1) * srcRes + x1];
			const float h0 = h00 * (1.0f - tx) + h10 * tx;
			const float h1 = h01 * (1.0f - tx) + h11 * tx;
			return h0 * (1.0f - tz) + h1 * tz;
		}
	}

	std::vector<float> RasterizeStamp(const StampParams& params,
		uint32_t outResolution)
	{
		if (outResolution == 0) return {};

		// 1) Charge / génère la grille source.
		std::vector<float> source;
		uint32_t srcRes = 0;
		if (params.useProcedural)
		{
			source = GenerateProceduralStamp(params.procedural, outResolution);
			srcRes = outResolution;
		}
		else
		{
			std::string err;
			if (!LoadStampPng16(params.libraryPngPath, source, srcRes, err))
			{
				return {}; // échec chargement → no-op pour le caller
			}
		}

		if (source.empty() || srcRes == 0) return {};

		// 2) Si pas de rotation et que la résolution source == sortie, retour direct.
		const float angleRad = params.rotationYDeg * 3.14159265358979323846f / 180.0f;
		const bool noRotation = std::fabs(angleRad) < 1e-6f;
		if (noRotation && srcRes == outResolution)
		{
			return source;
		}

		// 3) Sinon, échantillonne bilinéairement dans le repère tourné autour du
		// centre. La rotation Y dans le plan XZ correspond à une rotation 2D
		// classique de la grille : pour chaque cellule (x, z) de la sortie, on
		// calcule la position (u, v) dans la grille source via la rotation
		// inverse (-angle) autour du centre.
		std::vector<float> out(static_cast<size_t>(outResolution) * outResolution, 0.0f);
		const float outCenter = static_cast<float>(outResolution - 1) * 0.5f;
		const float srcCenter = static_cast<float>(srcRes - 1) * 0.5f;
		const float scale = (outResolution > 1)
			? static_cast<float>(srcRes - 1) / static_cast<float>(outResolution - 1)
			: 1.0f;
		const float c = std::cos(-angleRad);
		const float s = std::sin(-angleRad);
		for (uint32_t z = 0; z < outResolution; ++z)
		{
			for (uint32_t x = 0; x < outResolution; ++x)
			{
				// Centré, mis à l'échelle de la grille source.
				const float dx = (static_cast<float>(x) - outCenter) * scale;
				const float dz = (static_cast<float>(z) - outCenter) * scale;
				const float ru = dx * c - dz * s;
				const float rv = dx * s + dz * c;
				const float u = ru + srcCenter;
				const float v = rv + srcCenter;
				out[static_cast<size_t>(z) * outResolution + x] =
					SampleBilinear(source, srcRes, u, v);
			}
		}
		return out;
	}

	bool TerrainStampTool::Init(CommandStack& stack, TerrainDocument& doc)
	{
		m_stack = &stack;
		m_doc = &doc;
		m_previewDeltas.clear();
		m_hasPreview = false;
		return true;
	}

	void TerrainStampTool::OnClickAt(const engine::core::Config& cfg,
		float worldX, float worldZ)
	{
		if (!m_doc) return;

		// Reset preview précédente.
		m_previewDeltas.clear();
		m_hasPreview = false;

		const float footprint = std::max(0.0f, m_params.footprintMeters);
		if (footprint <= 0.0f) return;

		// La footprint est exprimée en mètres ; convertit en nombre de cellules
		// (cellSizeMeters par défaut = 1 m). On arrondit vers le haut pour
		// garantir au moins 1 cellule, et on capote à une borne raisonnable
		// pour éviter les rasterisations énormes (plusieurs Mo).
		const float cellSize = engine::world::terrain::kTerrainCellSizeMeters;
		uint32_t footprintCells = static_cast<uint32_t>(std::ceil(footprint / cellSize));
		if (footprintCells < 2) footprintCells = 2;
		if (footprintCells > 2048) footprintCells = 2048;

		// Rasterise la grille de stamp (source procédurale ou PNG, après
		// rotation Y).
		std::vector<float> grid = RasterizeStamp(m_params, footprintCells);
		if (grid.empty()) return;

		const float radius = footprint * 0.5f;
		const float strength = m_params.strengthMeters;
		const StampMode mode = m_params.mode;

		// Box monde du stamp → plage de chunks impactés.
		const float wxMin = worldX - radius;
		const float wxMax = worldX + radius;
		const float wzMin = worldZ - radius;
		const float wzMax = worldZ + radius;

		const int chunkXMin = static_cast<int>(std::floor(wxMin / kChunkSpanMeters));
		const int chunkXMax = static_cast<int>(std::floor(wxMax / kChunkSpanMeters));
		const int chunkZMin = static_cast<int>(std::floor(wzMin / kChunkSpanMeters));
		const int chunkZMax = static_cast<int>(std::floor(wzMax / kChunkSpanMeters));

		// Indices min/max de la grille source (cellules).
		const float gridMaxIdx = static_cast<float>(footprintCells - 1);

		for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
		{
			for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
			{
				auto chunkPtr = m_doc->EnsureLoaded(cfg, cx, cz);
				if (!chunkPtr) continue;
				engine::world::terrain::TerrainChunk& chunk = *chunkPtr;
				const uint32_t resX = chunk.resolutionX;
				const uint32_t resZ = chunk.resolutionZ;

				TerrainSculptDeltaChunk deltaChunk;
				deltaChunk.coord = engine::world::GlobalChunkCoord{cx, cz};

				// Pour chaque cellule du chunk impactée, trouve la position
				// correspondante dans la grille de stamp et applique le mode.
				for (uint32_t lz = 0; lz < resZ; ++lz)
				{
					// Coord monde Z du noeud (cellule).
					const float wz = static_cast<float>(cz) * kChunkSpanMeters
						+ static_cast<float>(lz) * cellSize;
					if (wz < wzMin || wz > wzMax) continue;
					// Indice dans la grille source : 0 à gauche du stamp,
					// (footprintCells - 1) à droite.
					const float gv = (wz - wzMin) / footprint * gridMaxIdx;
					if (gv < 0.0f || gv > gridMaxIdx) continue;

					for (uint32_t lx = 0; lx < resX; ++lx)
					{
						const float wx = static_cast<float>(cx) * kChunkSpanMeters
							+ static_cast<float>(lx) * cellSize;
						if (wx < wxMin || wx > wxMax) continue;
						const float gu = (wx - wxMin) / footprint * gridMaxIdx;
						if (gu < 0.0f || gu > gridMaxIdx) continue;

						const float w = SampleBilinear(grid, footprintCells, gu, gv);
						if (w == 0.0f) continue;

						const float target = w * strength;
						const size_t hidx = static_cast<size_t>(lz) * resX + lx;
						const float h = chunk.heights[hidx];

						float delta = 0.0f;
						switch (mode)
						{
							case StampMode::Add:
								delta = target;
								break;
							case StampMode::Replace:
								delta = target - h;
								break;
							case StampMode::Max:
								delta = std::max(0.0f, target - h);
								break;
							case StampMode::Min:
								delta = std::min(0.0f, target - h);
								break;
						}
						if (delta == 0.0f) continue;

						TerrainSculptDeltaCell cell;
						cell.x = static_cast<uint16_t>(lx);
						cell.z = static_cast<uint16_t>(lz);
						cell.deltaMeters = delta;
						deltaChunk.cells.push_back(cell);
					}
				}

				if (!deltaChunk.cells.empty())
				{
					m_previewDeltas.push_back(std::move(deltaChunk));
				}
			}
		}

		m_hasPreview = !m_previewDeltas.empty();
	}

	void TerrainStampTool::Apply()
	{
		if (!m_hasPreview) return;
		if (!m_stack || !m_doc)
		{
			m_previewDeltas.clear();
			m_hasPreview = false;
			return;
		}
		auto cmd = std::make_unique<TerrainStampCommand>(*m_doc,
			std::move(m_previewDeltas));
		m_stack->Push(std::move(cmd));
		m_previewDeltas.clear();
		m_hasPreview = false;
	}

	void TerrainStampTool::Cancel()
	{
		m_previewDeltas.clear();
		m_hasPreview = false;
	}
}
