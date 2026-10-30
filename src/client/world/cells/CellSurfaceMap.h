#pragma once

// SND.2 — Carte des sols d'une cellule (côté CPU), pour les sons de pas.
//
// Le splat d'une cellule (fichier SLAP, RGBA = herbe / terre / roche / neige,
// 2048² texels sur 10 km ≈ 4,9 m) part en mémoire graphique pour le rendu ;
// cette classe en garde, côté CPU, la couche DOMINANTE par texel (1 octet,
// ≈ 4 Mo par cellule) afin de savoir sur quoi marche le personnage.
//
// Construite sur le thread de chargement des cellules (CellAssetLoader), lue
// au main thread. Module pur : testé par cell_surface_map_tests.

#include <cstdint>
#include <span>
#include <vector>

namespace engine::world::cells
{
	class CellSurfaceMap
	{
	public:
		/// Lit un fichier SLAP complet (magic "SLAP", largeur, hauteur, RGBA8) et
		/// garde l'indice du canal dominant de chaque texel (0..3).
		/// \return false (carte vide) si le fichier est invalide.
		bool BuildFromSlap(std::span<const uint8_t> slapFile);

		bool Empty() const { return m_layers.empty(); }
		uint32_t Width() const { return m_width; }
		uint32_t Height() const { return m_height; }

		/// Couche dominante au point GLOBAL (x, z) d'une cellule dont le coin min
		/// est (minX, minZ) et le côté `sizeM` (m). Ligne 0 = Z minimal (nord).
		/// \return 0..3, ou -1 si hors de la cellule ou carte vide.
		int LayerAt(double x, double z, double minX, double minZ, double sizeM) const;

	private:
		std::vector<uint8_t> m_layers;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
	};
}
