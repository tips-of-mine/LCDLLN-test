#pragma once

// M100.16 — Outil de pose de hazards (paramètres + création). Rendu ImGui
// guardé Windows. Standalone (l'intégration au système ActiveTool/ToolProperties
// du shell est laissée à un câblage ultérieur, non couvert par les tests).

#include "src/client/world/hazard/HazardVolumes.h"
#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	class HazardTool
	{
	public:
		/// Change le type courant et applique ses défauts (cf. tableau M100.16).
		void SetType(engine::world::hazard::HazardType type)
		{
			const engine::world::hazard::HazardShape keepShape = m_params.shape;
			m_params = engine::world::hazard::MakeDefaultHazard(type);
			m_params.shape = keepShape;
		}

		/// Construit un volume aux paramètres courants, posé à `pos`.
		engine::world::hazard::HazardVolume CreateAt(const engine::math::Vec3& pos) const
		{
			engine::world::hazard::HazardVolume v = m_params;
			v.position = pos;
			return v;
		}

		engine::world::hazard::HazardVolume& Params() { return m_params; }
		const engine::world::hazard::HazardVolume& Params() const { return m_params; }

		/// Rend le panneau de propriétés de l'outil (ImGui, Windows uniquement).
		void Render();

	private:
		engine::world::hazard::HazardVolume m_params =
			engine::world::hazard::MakeDefaultHazard(engine::world::hazard::HazardType::Quicksand);
	};
}
