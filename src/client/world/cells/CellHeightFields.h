#pragma once

// Monde « Royaumes » côté client — hauteur du sol sur plusieurs cellules.
//
// Les heightmaps CPU (2049² × 16 bits ≈ 8 Mo par cellule) des cellules
// proches du joueur sont gardées en mémoire, indépendamment du rendu GPU :
// collision du joueur, pose au sol des PNJ, des entités distantes et du décor
// fonctionnent dès qu'une cellule est connue, même avant que son terrain soit
// affiché. Les requêtes arrivent en coordonnées de RENDU (\ref RenderFrame) ;
// la cellule et la position dans la heightmap sont calculées en double.
//
// Thread : main thread (aucun verrou).

#include "src/client/render/terrain/HeightmapLoader.h"
#include "src/client/world/cells/CellWorldFrame.h"
#include "src/client/world/terrain/IHeightField.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace engine::world::cells
{
	/// Heightmap CPU d'une cellule et son cadrage GLOBAL.
	struct CellHeightTile
	{
		CellCoord cell{};
		engine::render::terrain::HeightmapData data;
		double minX = 0.0;          ///< coin min X global (m)
		double minZ = 0.0;          ///< coin min Z global (m)
		double worldSize = 10000.0; ///< côté couvert par la heightmap (m)
		double heightScale = 1200.0;///< altitude du blanc (m)
	};

	/// Ensemble des heightmaps résidentes, interrogeable en coordonnées de rendu.
	class CellHeightFields final : public engine::world::terrain::IHeightField
	{
	public:
		/// \param frame repère de rendu courant (non possédé, doit survivre à l'objet).
		explicit CellHeightFields(const RenderFrame* frame) : m_frame(frame) {}

		/// Ajoute ou remplace la heightmap d'une cellule.
		void Put(CellHeightTile tile);
		/// Retire la heightmap d'une cellule (no-op si absente).
		void Remove(CellCoord cell);
		bool Has(CellCoord cell) const;
		std::size_t Count() const { return m_tiles.size(); }
		/// Tuile d'une cellule (nullptr si absente).
		const CellHeightTile* Find(CellCoord cell) const;

		/// Altitude (m) au point GLOBAL ; false si sa cellule n'est pas résidente.
		bool SampleGlobal(double globalX, double globalZ, float& outHeight) const;

		/// IHeightField — coordonnées de RENDU. 0 si la cellule n'est pas résidente.
		float HeightAt(float renderX, float renderZ) const override;
		/// IHeightField — vrai si la cellule du point (rendu) est résidente.
		bool IsLoadedAt(float renderX, float renderZ) const override;

		/// Cellules résidentes (ordre quelconque).
		void ListCells(std::vector<CellCoord>& out) const;

	private:
		const RenderFrame* m_frame = nullptr;
		std::unordered_map<uint32_t, CellHeightTile> m_tiles;
	};
}
