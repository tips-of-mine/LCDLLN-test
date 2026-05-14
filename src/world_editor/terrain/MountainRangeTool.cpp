#include "src/world_editor/terrain/MountainRangeTool.h"

#include "src/world_editor/terrain/MountainRangeCommand.h"
#include "src/world_editor/terrain/TerrainDocument.h"

#include <cmath>
#include <memory>
#include <utility>

namespace engine::editor::world
{
	namespace
	{
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;
	}

	bool MountainRangeTool::Init(CommandStack& stack, TerrainDocument& doc,
		const engine::core::Config& cfg)
	{
		m_stack = &stack;
		m_doc   = &doc;
		m_cfg   = &cfg;
		Reset();
		return true;
	}

	void MountainRangeTool::Reset()
	{
		m_params.vertices.clear();
		m_params.mode           = PolylineMode::Open;
		m_params.profile        = FlankProfile::Smoothstep;
		m_params.noiseSeed      = 0;
		m_params.noiseFrequency = 0.005f;
		m_activeVertex          = 0;
	}

	void MountainRangeTool::AddVertex(float worldX, float worldZ)
	{
		if (m_params.vertices.size() >= kMacroPolylineMaxVertices) return;
		PolylineVertex v;
		v.worldX = worldX;
		v.worldZ = worldZ;
		// Reprend les paramètres du vertex précédent si disponible — UX :
		// permet à l'utilisateur de tracer une polyline cohérente sans
		// retoucher les sliders à chaque clic.
		if (!m_params.vertices.empty())
		{
			const PolylineVertex& prev = m_params.vertices.back();
			v.widthMeters    = prev.widthMeters;
			v.heightMeters   = prev.heightMeters;
			v.noiseAmplitude = prev.noiseAmplitude;
			v.asymmetry      = prev.asymmetry;
		}
		m_params.vertices.push_back(v);
		m_activeVertex = m_params.vertices.size() - 1u;
	}

	void MountainRangeTool::RemoveVertex(size_t idx)
	{
		if (idx >= m_params.vertices.size()) return;
		m_params.vertices.erase(m_params.vertices.begin() +
			static_cast<std::ptrdiff_t>(idx));
		if (m_activeVertex >= m_params.vertices.size() &&
			!m_params.vertices.empty())
		{
			m_activeVertex = m_params.vertices.size() - 1u;
		}
	}

	void MountainRangeTool::MoveVertex(size_t idx, float worldX, float worldZ)
	{
		if (idx >= m_params.vertices.size()) return;
		m_params.vertices[idx].worldX = worldX;
		m_params.vertices[idx].worldZ = worldZ;
	}

	void MountainRangeTool::ToggleLoop()
	{
		m_params.mode = (m_params.mode == PolylineMode::Open)
			? PolylineMode::Loop
			: PolylineMode::Open;
	}

	void MountainRangeTool::SetGlobalParams(FlankProfile profile,
		uint32_t seed, float freq)
	{
		m_params.profile        = profile;
		m_params.noiseSeed      = seed;
		m_params.noiseFrequency = (freq > 0.0f) ? freq : 0.001f;
	}

	SparseChunkDeltas MountainRangeTool::BuildDeltas() const
	{
		return RasterizeMacroPolyline(m_params, /*invert=*/false);
	}

	bool MountainRangeTool::Apply()
	{
		if (m_stack == nullptr || m_doc == nullptr || m_cfg == nullptr) return false;
		if (m_params.vertices.size() < 2u) return false;
		const engine::core::Config& cfg = *m_cfg;

		// 1) Pré-charge les chunks impactés. La rasterisation pure renvoie
		//    une map sparse mais ne charge rien — c'est ici qu'on s'assure
		//    que `Find()` retournera un chunk valide à l'Execute.
		//    On reproduit le bounding box utilisé par le rasterizer pour
		//    couvrir exactement les mêmes chunks.
		float maxWidth = 0.0f;
		float bbMinX = +1e30f, bbMaxX = -1e30f;
		float bbMinZ = +1e30f, bbMaxZ = -1e30f;
		for (const auto& v : m_params.vertices)
		{
			maxWidth = std::max(maxWidth, v.widthMeters);
			bbMinX = std::min(bbMinX, v.worldX);
			bbMaxX = std::max(bbMaxX, v.worldX);
			bbMinZ = std::min(bbMinZ, v.worldZ);
			bbMaxZ = std::max(bbMaxZ, v.worldZ);
		}
		const float pad = maxWidth * 1.5f;
		bbMinX -= pad; bbMaxX += pad;
		bbMinZ -= pad; bbMaxZ += pad;
		const int chunkXMin = static_cast<int>(std::floor(bbMinX / kChunkSpanMeters));
		const int chunkXMax = static_cast<int>(std::floor(bbMaxX / kChunkSpanMeters));
		const int chunkZMin = static_cast<int>(std::floor(bbMinZ / kChunkSpanMeters));
		const int chunkZMax = static_cast<int>(std::floor(bbMaxZ / kChunkSpanMeters));
		for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
		{
			for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
			{
				(void)m_doc->EnsureLoaded(cfg, cx, cz);
			}
		}

		SparseChunkDeltas deltas = BuildDeltas();
		if (deltas.empty()) return false;

		auto cmd = std::make_unique<MountainRangeCommand>(*m_doc, std::move(deltas));
		m_stack->Push(std::move(cmd));
		Reset();
		return true;
	}

	void MountainRangeTool::Cancel()
	{
		Reset();
	}
}
