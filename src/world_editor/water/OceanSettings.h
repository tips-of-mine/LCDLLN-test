#pragma once

namespace engine::editor::world
{
	/// Source de vérité unique du niveau de mer global de la zone (M100.36).
	/// Stockée dans `WaterDocument::m_ocean`, persistée dans `instances/water.bin`
	/// (kWaterVersion v2).
	///
	/// M100.36 introduit le champ minimal `seaLevelMeters`. M100.37 (Coastline)
	/// AJOUTE des champs supplémentaires (`bottomColor`, `turbidity`, …) — pas
	/// de déplacement ni de refactor du champ existant. Le contrat est : tout
	/// consommateur du sea level lit `WaterDocument::GetOcean().seaLevelMeters`
	/// et jamais une variable locale dupliquée.
	struct OceanSettings
	{
		float seaLevelMeters = 50.0f;
	};
}
