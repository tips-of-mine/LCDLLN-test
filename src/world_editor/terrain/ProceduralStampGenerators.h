#pragma once

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Archétype d'un générateur procédural de stamp terrain (M100.7). Chaque
	/// archétype produit une silhouette radialement symétrique normalisée dans
	/// [-1, 1]. Le `TerrainStampTool` multiplie ensuite cette grille par la
	/// `strengthMeters` choisie par l'utilisateur pour obtenir un delta en
	/// mètres.
	enum class ProceduralStamp : uint8_t
	{
		Mountain = 0, ///< Cône lissé (smoothstep), 1 au centre, 0 au bord.
		Valley   = 1, ///< Inverse du Mountain : -1 au centre, 0 au bord.
		Crater   = 2, ///< Creux central jusqu'à d/r=0.8 + anneau positif au bord.
	};

	/// Génère une grille `outResolution × outResolution` de poids selon
	/// l'archétype `kind`. Déterministe pour les mêmes inputs (utile pour la
	/// reproductibilité des tests et la prévisualisation live).
	///
	/// La grille est carrée, échantillonnée en coordonnées normalisées
	/// `[-1, +1]` autour du centre. Pour chaque cellule, on calcule la distance
	/// `d/r` au centre (où r=1 par convention) et on applique :
	///
	///   - Mountain : weight = smoothstep(1, 0, d/r) — cône lissé
	///     (1 au centre, 0 au bord, transition smooth Hermite).
	///   - Valley   : weight = -smoothstep(1, 0, d/r) — cône inversé.
	///   - Crater   : weight = -smoothstep(0.6, 0.8, d/r)
	///                       + smoothstep(0.8, 1.0, d/r)
	///     (creux central jusqu'à d=0.8r, anneau positif au-delà).
	///
	/// Les cellules à `d/r > 1` sont fixées à 0 pour éviter tout débordement
	/// hors du disque. La grille est retournée en row-major (`grid[z*N + x]`).
	///
	/// \param kind          Archétype voulu.
	/// \param outResolution Côté de la grille (nombre de cellules par axe).
	///                      Si 0, retourne un vecteur vide.
	/// \return Vecteur de `outResolution * outResolution` poids.
	std::vector<float> GenerateProceduralStamp(ProceduralStamp kind,
		uint32_t outResolution);
}
