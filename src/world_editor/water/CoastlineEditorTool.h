#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/water/CoastlineSegmentExtractor.h"
#include "src/world_editor/water/CoastlineStats.h"
#include "src/world_editor/water/OceanSettings.h"

#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;

	/// Outil "Coastline" (M100.37). Gère :
	///   - le slider sea level (buffer local, écrit dans WaterDocument
	///     uniquement à l'Apply),
	///   - les sliders couleur de fond / turbidité / wind influence /
	///     enabled (idem),
	///   - les paramètres de smoothing / falaises / plage (cette dernière
	///     marquée non-câblée dans l'MVP M100.37),
	///   - le recalcul live des segments marching squares et des statistiques
	///     (debounce externe — l'UI appelle `RefreshPreview()` quand un
	///     paramètre observable change).
	///
	/// L'Apply assemble la `CoastlineCommand` et la pousse sur `CommandStack`.
	/// Le buffer local est réinitialisé à la valeur du document à `Reset()`.
	class CoastlineEditorTool
	{
	public:
		bool Init(CommandStack& stack, TerrainDocument& terrain,
			WaterDocument& water, const engine::core::Config& cfg);

		void Reset();

		/// Recalcule les segments marching squares + statistiques à partir
		/// de la heightmap actuelle (chunks chargés autour du centre de la
		/// zone éditée). Coûteux : à appeler en debounce 200 ms.
		void RefreshPreview();

		/// Buffer local des paramètres ocean. Modifiés par les sliders ;
		/// commités à `WaterDocument` uniquement à `Apply`.
		OceanSettings&       MutableOceanBuffer()       { return m_oceanBuffer; }
		const OceanSettings& OceanBuffer()        const { return m_oceanBuffer; }

		// Paramètres heightmap.
		float& SmoothingBandMeters() { return m_smoothingBandMeters; }
		float& SmoothingForce()      { return m_smoothingForce; }
		bool&  SmoothingEnabled()    { return m_smoothingEnabled; }

		float& CliffsThresholdMeters() { return m_cliffsThresholdMeters; }
		float& CliffsSlopeThresholdDeg() { return m_cliffsSlopeThresholdDeg; }
		float& CliffsLandSideMeters() { return m_cliffsLandSideMeters; }
		float& CliffsSeaSideMeters() { return m_cliffsSeaSideMeters; }
		bool&  CliffsEnabled()        { return m_cliffsEnabled; }

		// Beach splat (UI-only en M100.37 MVP — pas appliqué par la command).
		bool&  BeachSplatEnabled()    { return m_beachSplatEnabled; }
		float& BeachLandBandMeters()  { return m_beachLandBandMeters; }
		float& BeachSeaBandMeters()   { return m_beachSeaBandMeters; }

		const std::vector<CoastlineSegment>& Segments() const { return m_segments; }
		const CoastlineStats& Stats() const { return m_stats; }

		/// Pousse une `CoastlineCommand` sur `CommandStack`. Reset le buffer
		/// après push (le slider repart sur la valeur fraîchement appliquée).
		bool Apply();

		/// Abandonne le buffer local sans rien pousser.
		void Cancel();

	private:
		CommandStack*               m_stack   = nullptr;
		TerrainDocument*            m_terrain = nullptr;
		WaterDocument*              m_water   = nullptr;
		const engine::core::Config* m_cfg     = nullptr;

		OceanSettings  m_oceanBuffer;
		float m_smoothingBandMeters = 5.0f;
		float m_smoothingForce      = 0.3f;
		bool  m_smoothingEnabled    = false;

		float m_cliffsThresholdMeters   = 8.0f;
		float m_cliffsSlopeThresholdDeg = 45.0f;
		float m_cliffsLandSideMeters    = 6.0f;
		float m_cliffsSeaSideMeters     = 3.0f;
		bool  m_cliffsEnabled           = false;

		bool  m_beachSplatEnabled  = false;
		float m_beachLandBandMeters = 8.0f;
		float m_beachSeaBandMeters  = 3.0f;

		std::vector<CoastlineSegment> m_segments;
		CoastlineStats                m_stats;
	};
}
