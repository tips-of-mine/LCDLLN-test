// engine/editor/world/LakeTool.h
#pragma once

#include "engine/editor/world/WaterDocument.h"
#include "engine/math/Math.h"

#include <vector>

namespace engine::editor::world
{
	class CommandStack;

	/// Outil d'édition d'un lac (M100.13). État : un polygone en cours de
	/// construction (pas encore committé) + référence au document partagé.
	/// Workflow : AddPoint(xz) répété → ClosePolygon() commit le lac dans
	/// le document via AddLakeCommand. Cancel() abandonne sans commit.
	class LakeTool
	{
	public:
		/// Init avec la pile undo + le doc water partagé. Doit être appelé
		/// une fois avant tout AddPoint/ClosePolygon. Pas d'état par session.
		bool Init(CommandStack& stack, WaterDocument& waterDoc) noexcept;

		/// Ajoute un point au polygone en cours. Y = m_currentWaterLevelY.
		/// Pas d'effet si Init n'a pas été appelé.
		void AddPoint(float worldX, float worldZ);

		/// Ferme le polygone et commit comme nouveau lac via AddLakeCommand
		/// (push sur la pile undo). No-op si < 3 points. Vide la liste après commit.
		void ClosePolygon();

		/// Abandonne le polygone en cours (vide la liste de points).
		void Cancel() noexcept;

		bool   HasActivePolygon() const noexcept { return !m_currentPoints.empty(); }
		size_t GetPointCount()    const noexcept { return m_currentPoints.size(); }
		const std::vector<engine::math::Vec3>& GetCurrentPoints() const { return m_currentPoints; }

		/// Accès mutable aux paramètres par défaut du prochain lac.
		/// Le panel ToolPropertiesPanel les édite via SliderFloat/ColorEdit.
		float& MutableWaterLevelY() noexcept { return m_currentWaterLevelY; }
		engine::math::Vec3& MutableBottomColor() noexcept { return m_currentBottomColor; }
		float& MutableTurbidity() noexcept { return m_currentTurbidity; }

	private:
		CommandStack*  m_stack = nullptr;
		WaterDocument* m_doc   = nullptr;
		std::vector<engine::math::Vec3> m_currentPoints;
		float m_currentWaterLevelY = 0.0f;
		engine::math::Vec3 m_currentBottomColor{ 0.05f, 0.20f, 0.30f };
		float m_currentTurbidity = 0.4f;
	};
}
