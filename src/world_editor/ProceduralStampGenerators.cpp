#include "src/world_editor/world/ProceduralStampGenerators.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		/// Polynôme Hermite classique `3t² - 2t³` borné à `[edge0, edge1]` puis
		/// normalisé sur [0..1]. Identique à GLSL `smoothstep`. Supporte les
		/// edges inversées (edge0 > edge1) pour fournir une rampe descendante :
		/// `smoothstep(1, 0, x)` = 1 en x=0, 0 en x=1, smooth entre les deux.
		/// \param edge0 Borne basse de la transition (ou borne haute si edge0 > edge1).
		/// \param edge1 Borne haute de la transition (ou borne basse si edge0 > edge1).
		/// \param x     Valeur d'entrée (clampée dans [min(e0,e1), max(e0,e1)] avant
		///              évaluation).
		float Smoothstep(float edge0, float edge1, float x)
		{
			if (edge0 == edge1)
			{
				// Discontinuité : retourne 1 si x ≥ edge0, 0 sinon.
				return (x >= edge0) ? 1.0f : 0.0f;
			}
			float t = (x - edge0) / (edge1 - edge0);
			t = std::clamp(t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		/// Évalue le poids pour un archétype donné à la distance normalisée
		/// `dr ∈ [0, +∞[` (où 1 = bord du disque). Renvoie 0 hors du disque
		/// (dr > 1) pour tous les archétypes — c'est le caller qui décide de
		/// l'extension vers les coins du carré contenant le disque.
		float EvalProceduralWeight(ProceduralStamp kind, float dr)
		{
			if (dr > 1.0f) return 0.0f;
			switch (kind)
			{
				case ProceduralStamp::Mountain:
					// smoothstep(1, 0, dr) = 1 au centre (dr=0), 0 au bord (dr=1).
					return Smoothstep(1.0f, 0.0f, dr);
				case ProceduralStamp::Valley:
					// Inverse du Mountain : -1 au centre, 0 au bord.
					return -Smoothstep(1.0f, 0.0f, dr);
				case ProceduralStamp::Crater:
					// Creux central jusqu'à dr=0.8 (négatif puis remonte vers 0
					// entre 0.6 et 0.8), puis anneau positif entre 0.8 et 1.0.
					return -Smoothstep(0.6f, 0.8f, dr)
					     + Smoothstep(0.8f, 1.0f, dr);
			}
			return 0.0f;
		}
	}

	std::vector<float> GenerateProceduralStamp(ProceduralStamp kind,
		uint32_t outResolution)
	{
		if (outResolution == 0) return {};

		std::vector<float> grid(static_cast<size_t>(outResolution) * outResolution, 0.0f);

		// Centre du disque en coordonnées de cellule, en utilisant la convention
		// "centre cellule" : pour N cellules indexées 0..N-1, le centre du disque
		// se situe à `(N-1) / 2`. Cela garantit que pour N pair, deux cellules
		// adjacentes encadrent le centre (aucune n'a dr=0 strict mais la sommet
		// est uniformément réparti) ; pour N impair, la cellule centrale a
		// exactement dr=0.
		const float centerIdx = static_cast<float>(outResolution - 1) * 0.5f;
		// Rayon en cellules : on inscrit le disque dans le carré, donc le rayon
		// est `(N-1) / 2` (= centerIdx). Pour outResolution=1, on évite la
		// division par zéro en posant un poids unique = peak (Mountain → 1).
		if (outResolution == 1)
		{
			grid[0] = EvalProceduralWeight(kind, 0.0f);
			return grid;
		}
		const float radiusCells = centerIdx;

		for (uint32_t z = 0; z < outResolution; ++z)
		{
			for (uint32_t x = 0; x < outResolution; ++x)
			{
				const float dx = static_cast<float>(x) - centerIdx;
				const float dz = static_cast<float>(z) - centerIdx;
				const float dist = std::sqrt(dx * dx + dz * dz);
				const float dr = dist / radiusCells;
				grid[static_cast<size_t>(z) * outResolution + x] =
					EvalProceduralWeight(kind, dr);
			}
		}
		return grid;
	}
}
