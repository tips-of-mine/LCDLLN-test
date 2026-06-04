#pragma once

// M100.49 — WidgetTargetRegistry : enregistre les rectangles écran des widgets
// par leur id stable, pour que l'overlay sache où dessiner le surlignage.
// Pure (rectangle = struct simple, pas d'ImVec2) → testable headless. Le shell
// éditeur remplit le registre chaque frame depuis les positions ImGui réelles.

#include <string>
#include <unordered_map>

#include "src/world_editor/tutorial/Tutorial.h"

namespace engine::editor::world::help
{
	/// Rectangle écran (coordonnées pixels, top-left + bottom-right).
	struct WidgetRect
	{
		float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
		bool Valid() const { return x1 > x0 && y1 > y0; }
	};

	/// Registre id → rectangle. Rempli par frame ; lu par l'overlay.
	class WidgetTargetRegistry
	{
	public:
		/// Enregistre/maj le rectangle d'un widget.
		void Register(const WidgetTargetId& id, const WidgetRect& rect) { m_rects[id] = rect; }

		/// Récupère le rectangle d'un widget. `*found` indique la présence.
		WidgetRect Get(const WidgetTargetId& id, bool* found = nullptr) const
		{
			auto it = m_rects.find(id);
			if (it == m_rects.end()) { if (found) *found = false; return {}; }
			if (found) *found = true;
			return it->second;
		}

		/// Vide le registre (début de frame).
		void Clear() { m_rects.clear(); }

		size_t Count() const { return m_rects.size(); }

	private:
		std::unordered_map<WidgetTargetId, WidgetRect> m_rects;
	};
}
