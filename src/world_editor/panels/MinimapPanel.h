#pragma once

// M100.34 — MinimapPanel : mini-carte 2D top-down du chunk courant et de ses
// voisins. La construction de la grille (BuildMinimapView) est PURE et testable
// headless ; le rendu ImGui (Render) est guardé Windows.

#include <cstdint>
#include <utility>
#include <vector>

namespace engine::editor::world
{
	/// Une cellule de la mini-carte (un chunk).
	struct MinimapCell
	{
		int32_t chunkX    = 0;
		int32_t chunkZ    = 0;
		bool    isCurrent = false; ///< true pour le chunk centré (caméra).
		bool    isLoaded  = false; ///< true si le chunk est chargé/visible.
	};

	/// Vue de mini-carte : grille (2*radius+1)² centrée sur (centerX, centerZ).
	struct MinimapView
	{
		int32_t centerX = 0;
		int32_t centerZ = 0;
		int32_t radius  = 0;
		std::vector<MinimapCell> cells; ///< Ordre ligne par ligne (z croissant, x croissant).
	};

	/// Construit la vue centrée sur (centerX, centerZ) avec un rayon `radius`
	/// (en chunks). `loaded` liste les chunks chargés (marqués isLoaded). Le
	/// chunk central est toujours marqué isCurrent.
	/// \param radius rayon en chunks (clampé ≥ 0).
	MinimapView BuildMinimapView(int32_t centerX, int32_t centerZ, int32_t radius,
		const std::vector<std::pair<int32_t, int32_t>>& loaded);

	/// Panneau ImGui de mini-carte.
	class MinimapPanel
	{
	public:
		void SetCenter(int32_t cx, int32_t cz) { m_centerX = cx; m_centerZ = cz; }
		void SetRadius(int32_t r) { m_radius = r < 0 ? 0 : r; }
		void SetLoadedChunks(std::vector<std::pair<int32_t, int32_t>> loaded) { m_loaded = std::move(loaded); }

		/// Rendu ImGui. No-op hors Windows. Main thread, frame ImGui active.
		void Render();

	private:
		int32_t m_centerX = 0;
		int32_t m_centerZ = 0;
		int32_t m_radius  = 3;
		std::vector<std::pair<int32_t, int32_t>> m_loaded;
	};
}
