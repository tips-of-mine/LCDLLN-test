// src/client/world/water/SeaGrid.h
#pragma once

// EAU.6 — Maillage fin de la mer autour du joueur.
//
// Avant EAU.6, la mer d'une cellule était un carré de 4 sommets (10 km de côté) :
// impossible de la soulever en vagues. Ce module construit, autour d'un centre
// (le joueur, recalé par pas fixe pour que la grille ne « glisse » pas), une
// grille régulière limitée aux nappes de mer (`isOcean`, rectangles alignés sur
// les axes : cellules générées et mer de repli). Chaque sommet porte :
//   - la profondeur d'eau sous lui (niveau de la mer − sol) : la houle s'éteint
//     au bord et la vague de bord monte sur la plage (SeaSwell.h) ;
//   - un fondu de bord (1 au centre, 0 au bord de la grille) : la grille se
//     raccorde sans marche à la mer plate qui l'entoure.
// Le reste de chaque rectangle de mer (hors grille) reste une nappe plate
// découpée en rectangles (SubtractSeaRect) : aucune zone dessinée deux fois.
//
// Module PUR (pas de Vulkan) : testé par sea_grid_tests. Lacs et rivières ne
// sont jamais touchés.

#include "src/shared/math/Math.h"
#include "src/shared/world/water/WaterSurfaces.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace engine::world::water
{
	/// Rectangle aligné sur les axes dans le plan XZ (x0 < x1, z0 < z1).
	struct SeaRect
	{
		float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
		bool Contains(float x, float z) const { return x >= x0 && x < x1 && z >= z0 && z < z1; }
		bool Empty() const { return !(x1 > x0) || !(z1 > z0); }
	};

	/// Réglages de la grille. Défauts : 512 m de côté, un sommet tous les 4 m,
	/// recalage tous les 64 m de déplacement du joueur (moins d'envois au GPU).
	struct SeaGridSettings
	{
		float halfSizeM = 256.0f;  ///< demi-côté de la grille (m)
		float stepM     = 4.0f;    ///< pas entre deux sommets (m)
		float snapM     = 64.0f;   ///< pas de recalage du centre (m) : joueur à ≤ 32 m du centre
		float edgeFadeM = 48.0f;   ///< largeur du fondu vers la mer plate (m)
		/// Profondeur (m, négative = terrain émergé) en dessous de laquelle un
		/// carreau dont les 4 coins sont plus hauts est sauté : il est sous la terre,
		/// la vague de bord (≤ kSeaRunUpM) ne l'atteint jamais.
		float dryCutoffM = -1.0f;
	};

	/// Sommet de la grille (repère de rendu).
	struct SeaGridVertex
	{
		engine::math::Vec3 position;  ///< y = niveau moyen de la mer
		float depthM = 0.0f;          ///< niveau de la mer − sol (m) ; kSeaDeepWaterM si sol inconnu
		float edgeFade = 0.0f;        ///< 1 = vagues entières, 0 = mer plate (bord de grille)
	};

	/// Grille de mer prête à l'upload. Vide (Valid() == false) s'il n'y a pas de
	/// mer rectangulaire sous le centre demandé.
	struct SeaGridCpu
	{
		std::vector<SeaGridVertex> vertices;
		std::vector<uint32_t>      indices;     ///< 3 par triangle, normale +Y
		SeaRect  area{};                        ///< carré couvert par la grille
		uint32_t oceanLakeIndex = 0;            ///< lac `isOcean` dont la grille reprend les paramètres
		float    centerX = 0.0f, centerZ = 0.0f;
		/// Sommets dont le sol n'était pas encore chargé (traités comme le large) :
		/// l'appelant reconstruit la grille plus tard pour affiner le bord.
		uint32_t unknownSamples = 0;
		bool Valid() const { return !indices.empty(); }
	};

	/// Hauteur du sol (m) au point (x, z) du repère de rendu ; \p known = false
	/// si le terrain n'est pas chargé (le point est alors traité comme le large).
	using SeaGroundSampler = std::function<float(float x, float z, bool& known)>;

	/// True si \p lake est une nappe de mer rectangulaire alignée sur les axes
	/// (4 sommets) ; \p out reçoit son rectangle.
	bool OceanRect(const LakeInstance& lake, SeaRect& out);

	/// Recale \p v sur le multiple de \p step le plus proche.
	float SnapToStep(float v, float step);

	/// \p rect privé de \p hole : 0 à 4 rectangles ajoutés à \p out (\p rect
	/// entier s'ils ne se recoupent pas).
	void SubtractSeaRect(const SeaRect& rect, const SeaRect& hole, std::vector<SeaRect>& out);

	/// Construit la grille autour de (centerX, centerZ) déjà recalé.
	/// Un carreau est gardé si son centre est dans un rectangle de mer et s'il
	/// n'est pas entièrement au-dessus de `settings.dryCutoffM`.
	/// \return true si la grille contient au moins un triangle.
	bool BuildSeaGrid(const std::vector<LakeInstance>& lakes, float centerX, float centerZ,
		const SeaGridSettings& settings, const SeaGroundSampler& ground, SeaGridCpu& out);
}
