#pragma once

// M100.19 — Outil Field (rectangle aligné, blé/maïs). Rendu ImGui guardé ; la
// génération est la fonction pure GenerateField (ForestFieldGen). Le tagging
// splat (WheatField/CornField) est différé (intégration TerrainDocument).

#include <cstdint>

#include "src/world_editor/ForestFieldGen.h"

namespace engine::editor::world
{
	enum class FieldCrop : uint8_t { Wheat = 0, Corn = 1 };

	class FieldTool
	{
	public:
		FieldParams& Params() { return m_params; }
		const FieldParams& Params() const { return m_params; }
		FieldCrop Crop() const { return m_crop; }
		void SetCrop(FieldCrop c) { m_crop = c; }
		bool AutoTagSplat() const { return m_autoTagSplat; }

		void Render();

	private:
		FieldParams m_params;
		FieldCrop m_crop = FieldCrop::Wheat;
		bool m_autoTagSplat = true;
	};
}
