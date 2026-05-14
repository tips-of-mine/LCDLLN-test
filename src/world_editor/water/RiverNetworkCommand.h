#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/water/OceanSettings.h"
#include "src/world_editor/water/RiverNetworkResult.h"

#include <cstddef>

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;

	/// Commande qui matérialise un `RiverNetworkResult` (M100.36). Insère les
	/// rivières et lacs auto dans `WaterDocument`, applique les deltas de
	/// carving dans `TerrainDocument`, écrit la valeur slidée d'`OceanSettings`.
	///
	/// Undo strictement réversible : retire les instances (noms exacts), inverse
	/// les deltas de carving (delta négatif → +delta), restaure le snapshot
	/// `previousOcean`.
	///
	/// Effet de bord : modifie `WaterDocument` + `TerrainDocument` à
	/// `Execute()` / `Undo()`. Marque les chunks dirty + appelle `OnCommit`
	/// pour la régen LOD M100.8.
	class RiverNetworkCommand final : public ICommand
	{
	public:
		RiverNetworkCommand(TerrainDocument& terrain,
			WaterDocument& water,
			RiverNetworkResult result,
			OceanSettings newOcean,
			OceanSettings previousOcean);

		const char* GetLabel()           const override { return "River Network"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

		const RiverNetworkResult& Result() const { return m_result; }

	private:
		void ApplyCarveDeltas(float sign);

		TerrainDocument*   m_terrain = nullptr;
		WaterDocument*     m_water   = nullptr;
		RiverNetworkResult m_result;
		OceanSettings      m_newOcean;
		OceanSettings      m_previousOcean;
		// Noms des instances effectivement ajoutées (servent à les retirer
		// précisément à l'Undo, même si d'autres modifications ont été faites
		// entre Execute et Undo).
		std::vector<std::string> m_insertedRiverNames;
		std::vector<std::string> m_insertedLakeNames;
	};
}
