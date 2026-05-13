#pragma once

namespace engine::editor::world
{
	/// Paramètres globaux d'un océan unique par zone.
	/// Stocké dans `WaterDocument::m_ocean`, sérialisé dans
	/// `instances/water.bin` (kWaterVersion v3 après M100.37).
	///
	/// **Évolution stricte (pas de refactor) :**
	///   - M100.36 : introduit `seaLevelMeters` (water.bin v2). Source de
	///     vérité unique du sea level, lue par M100.36 watershed et
	///     M100.38/.39 erosions.
	///   - M100.37 : ajoute `bottomColor`, `turbidity`, `windInfluence`,
	///     `enabled` (water.bin v3). Aucun champ existant n'est déplacé ;
	///     `seaLevelMeters` reste à sa place, `WatershedSimulationParams`
	///     reste sans seaLevel, `RunWatershedSimulation` continue à le lire
	///     via `WaterDocument::GetOcean()`.
	struct OceanSettings
	{
		float seaLevelMeters = 50.0f;                       // M100.36
		float bottomColor[3] = { 0.10f, 0.23f, 0.33f };     // M100.37 — RGB linéaire
		float turbidity      = 0.4f;                        // M100.37
		float windInfluence  = 0.2f;                        // M100.37 — multiplicateur sur les vagues globales (M100.26)
		bool  enabled        = true;                        // M100.37
	};
}
