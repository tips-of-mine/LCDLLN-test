#pragma once

#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainChunk.h"

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Mode de la brosse de sculpture terrain (M100.6). Chaque mode applique
	/// un kernel différent à la heightmap : Raise/Lower modifient additivement,
	/// Smooth applique un blur 3x3 pondéré, Flatten tire vers la hauteur sous
	/// le centre du clic, Noise injecte un bruit Simplex 2D.
	enum class TerrainBrushMode : uint8_t
	{
		Raise   = 0,
		Lower   = 1,
		Smooth  = 2,
		Flatten = 3,
		Noise   = 4,
	};

	/// Paramètres "live" de la brosse — modifiés par le panneau Tool Properties
	/// (M100.6) et lus à chaque tick d'application. Toutes les unités sont en
	/// mètres / mètres-par-seconde sauf indication contraire.
	struct TerrainBrushParams
	{
		/// Mode courant de la brosse (Raise par défaut).
		TerrainBrushMode mode = TerrainBrushMode::Raise;
		/// Rayon en mètres de la brosse (footprint disque). Cellules à
		/// distance > radius sont ignorées.
		float radiusMeters = 6.0f;
		/// Force d'application en m/s : delta par tick = strength * dt * weight.
		float strengthMps = 3.0f;
		/// Falloff smoothstep : weight = 1 sur l'intervalle
		/// [0, radius*(1-falloff)], puis décroît linéairement-smooth jusqu'à 0
		/// à `radius`. falloff=0 => bord dur, falloff=1 => transition pleine.
		float falloff = 0.7f;
		/// Fréquence du bruit Simplex (mode Noise uniquement). Unité : 1/m.
		float noiseFreq = 0.05f;
		/// Octaves du bruit (mode Noise uniquement). Borné [1, 6] côté UI.
		uint8_t noiseOctaves = 3;
		/// Mirror sur l'axe X (le delta sera symétrisé autour de l'axe X local
		/// du chunk). M100.6 : flag plumbed mais pas encore appliqué dans le
		/// kernel — l'application se fera côté tool dans un follow-up.
		bool mirrorX = false;
		/// Mirror sur l'axe Z (idem).
		bool mirrorZ = false;
	};

	/// Une cellule modifiée par la brosse. Coordonnées chunk-locales (cellule
	/// d'index `[x, z]` dans `TerrainChunk::heights`). `deltaMeters` est la
	/// variation de hauteur appliquée (positive = monte, négative = descend).
	struct TerrainSculptDeltaCell
	{
		uint16_t x = 0;
		uint16_t z = 0;
		float    deltaMeters = 0.0f;
	};

	/// Lot de cellules modifiées dans un même chunk. Plusieurs lots cohabitent
	/// dans une commande quand le brushstroke chevauche plusieurs chunks.
	struct TerrainSculptDeltaChunk
	{
		engine::world::GlobalChunkCoord coord{0, 0};
		std::vector<TerrainSculptDeltaCell> cells;
	};

	/// Évalue une fonction de bruit pseudo-Simplex 2D déterministe à la
	/// position monde `(x, z)`. Renvoie une valeur dans [-1, 1] (approximative
	/// — les bornes ne sont pas garanties à 1e-6 près mais suffisantes pour
	/// une brosse). L'implémentation utilise une table de permutations fixe
	/// (256 entrées) et somme `octaves` octaves de Perlin 2D classique avec
	/// fréquence doublée et amplitude moitié à chaque palier.
	/// \param x      Coordonnée monde en mètres (axe X).
	/// \param z      Coordonnée monde en mètres (axe Z).
	/// \param freq   Fréquence du premier octave (1/mètre).
	/// \param octaves Nombre d'octaves (≥1, capé à 8 en interne).
	/// \return Valeur de bruit ~[-1, 1], déterministe pour les mêmes entrées.
	float EvalSimplex2D(float x, float z, float freq, uint8_t octaves);

	/// Applique le kernel correspondant à `params.mode` sur `chunk` autour du
	/// point chunk-local `(centerLocalX, centerLocalZ)` (en mètres). Les
	/// cellules touchées sont accumulées dans `outDelta` (les anciennes
	/// entrées sont conservées, ce qui permet d'appeler la fonction plusieurs
	/// fois pour accumuler un brushstroke avant `OnMouseUp`).
	///
	/// Effet de bord : modifie directement `chunk.heights`. Ne met pas à jour
	/// `chunk.heightMin/heightMax` (laissé au caller via `RecomputeBounds` au
	/// moment du commit / save).
	///
	/// Contraintes thread/timing : main thread (le caller commit la commande
	/// immédiatement après).
	///
	/// \param chunk         Chunk à éditer (résolution fixe 257²).
	/// \param params        Paramètres live de la brosse.
	/// \param centerLocalX  Centre du clic en mètres chunk-locaux (axe X).
	/// \param centerLocalZ  Centre du clic en mètres chunk-locaux (axe Z).
	/// \param dtSeconds     Delta temps depuis le tick précédent (secondes).
	/// \param outDelta      Reçoit les cellules modifiées (append-only).
	/// \return Nombre de cellules modifiées par cet appel.
	uint32_t ApplyBrushKernel(engine::world::terrain::TerrainChunk& chunk,
		const TerrainBrushParams& params,
		float centerLocalX, float centerLocalZ,
		float dtSeconds,
		std::vector<TerrainSculptDeltaCell>& outDelta);
}
