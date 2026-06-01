/// M45.4 — Mapping octaédrique (directions de vue des impostors).
///
/// Le mapping ci-dessous DOIT rester identique côté runtime (M45.5) pour que
/// le décodage des vues corresponde exactement. Toute modification de la
/// formule doit bumper kImpostorVersion.
///
/// Convention :
///   - L'espace [0,1]² (u,v) est mappé sur la sphère unité par le mapping
///     octaédrique standard (octahedron complet, hémisphères haut+bas).
///   - Pour une grille N×N de vues, la vue (i,j) (i = colonne X, j = ligne Y,
///     0..N-1) échantillonne le CENTRE de sa cellule :
///         u = (i + 0.5) / N,  v = (j + 0.5) / N
///   - La direction renvoyée est la direction DEPUIS le mesh VERS la caméra
///     (axe de vue inverse), unitaire.

#pragma once

#include <cmath>

namespace tools::impostor_builder
{
	/// Direction 3D simple (autonome, pas de dépendance moteur).
	struct OctDir
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	/// Décode une coordonnée octaédrique (u,v) ∈ [0,1]² en direction unitaire.
	/// Mapping octaédrique standard (cf. Cigolle et al. 2014) :
	///   1. Remappe (u,v) de [0,1] vers [-1,1] : p = (2u-1, 2v-1).
	///   2. z = 1 - |p.x| - |p.y|.
	///   3. Si z < 0 (hémisphère inférieur), replie les composantes :
	///        x' = (1 - |p.y|) * sign(p.x)
	///        y' = (1 - |p.x|) * sign(p.y)
	///   4. Normalise (x, y, z).
	/// \return Direction unitaire correspondant à (u,v).
	inline OctDir OctaToDir(float u, float v)
	{
		const float px = 2.0f * u - 1.0f;
		const float py = 2.0f * v - 1.0f;

		float x = px;
		float y = py;
		float z = 1.0f - std::fabs(px) - std::fabs(py);

		if (z < 0.0f)
		{
			const float oldX = x;
			const float signX = (x >= 0.0f) ? 1.0f : -1.0f;
			const float signY = (y >= 0.0f) ? 1.0f : -1.0f;
			x = (1.0f - std::fabs(y)) * signX;
			y = (1.0f - std::fabs(oldX)) * signY;
		}

		const float len = std::sqrt(x * x + y * y + z * z);
		if (len > 1e-8f) { x /= len; y /= len; z /= len; }
		return OctDir{x, y, z};
	}

	/// Direction de vue pour la tile (i,j) d'une grille viewsPerAxis × viewsPerAxis.
	/// Échantillonne le centre de la cellule (i+0.5, j+0.5).
	/// \param i            Index colonne (X), 0..viewsPerAxis-1.
	/// \param j            Index ligne (Y), 0..viewsPerAxis-1.
	/// \param viewsPerAxis Nombre de vues par axe (N).
	/// \return Direction unitaire mesh→caméra pour cette vue.
	inline OctDir ViewDir(unsigned i, unsigned j, unsigned viewsPerAxis)
	{
		const float n = static_cast<float>(viewsPerAxis);
		const float u = (static_cast<float>(i) + 0.5f) / n;
		const float v = (static_cast<float>(j) + 0.5f) / n;
		return OctaToDir(u, v);
	}
}
