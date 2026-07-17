#pragma once
// ActiveTool — identifiant de l'outil actif du shell éditeur monde.
// Réorganisation UI 2026-07-17 : extrait de WorldEditorShell.h pour que les
// modules purs (ToolPaletteModel, tests ctest Linux) puissent référencer
// l'enum sans tirer tout le shell (outils, documents, panneaux).

#include <cstdint>

namespace engine::editor::world
{
	/// Identifiant de l'outil actif dans le shell éditeur monde (M100.6+).
	/// `None` est l'état initial après `Init`. `TerrainSculpt` est activé
	/// par le raccourci `B` (M100.6) ; `TerrainStamp` est activé par le
	/// raccourci `N` (M100.7) ; `SplatPaint` est activé par le raccourci `P`
	/// (M100.10). Les futurs outils (place…) s'ajouteront ici.
	enum class ActiveTool : uint8_t
	{
		None          = 0,
		TerrainSculpt = 1,
		TerrainStamp  = 2,
		SplatPaint    = 3,
		Lake          = 4,  // M100.13 — raccourci L
		River         = 5,  // M100.13 — raccourci R
		MountainRange    = 6,   // M100.35 — raccourci Ctrl+Shift+M
		ValleyChain      = 7,   // M100.35 — raccourci Ctrl+Shift+V
		RiverNetwork     = 8,   // M100.36 — raccourci Ctrl+Shift+N (network)
		Coastline           = 9,   // M100.37 — raccourci Ctrl+Shift+C
		HydraulicErosion    = 10,  // M100.38 — raccourci Ctrl+Shift+H
		ThermalWindErosion  = 11,  // M100.39 — raccourci Ctrl+Shift+T
		Cave                = 12,  // M100.40 — raccourci Ctrl+Shift+G (Grotte)
		Overhang            = 13,  // M100.41 — raccourci Ctrl+Shift+O (Overhang)
		Arch                = 14,  // M100.42 — raccourci Ctrl+Shift+A (Arche)
		DungeonPortal       = 15,  // M100.43 — raccourci Ctrl+Shift+D (Donjon)
	};

	/// Nombre d'outils « réels » (hors None). Sert aux tests d'exhaustivité
	/// (palette d'outils : chaque outil apparaît exactement une fois).
	inline constexpr int kActiveToolCount = 15;
}
