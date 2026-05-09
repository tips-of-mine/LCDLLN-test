#include "src/world_editor/TerrainBrush.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		/// Table de permutations de Perlin 2D — 256 entrées fixes (déterministe).
		/// Doublée à 512 pour éviter les modulos lors de l'indexation.
		constexpr std::array<int, 256> kPermBase = {
			151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
			8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
			35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
			134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
			55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,
			169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
			250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
			189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
			172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
			228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
			107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
			138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
		};

		/// Fade quintic de Perlin : 6t^5 - 15t^4 + 10t^3.
		inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

		/// Lerp linéaire.
		inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

		/// Gradient 2D pour Perlin : pioche un des 8 vecteurs canoniques selon
		/// les bits bas du hash, puis dot avec (x, z).
		inline float Grad2(int hash, float x, float z)
		{
			const int h = hash & 7;
			const float u = h < 4 ? x : z;
			const float v = h < 4 ? z : x;
			return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
		}

		/// Perlin 2D classique avec table de permutations fixe (déterministe).
		/// Sortie approximativement dans [-1, 1].
		float Perlin2D(float x, float z)
		{
			const int xi = static_cast<int>(std::floor(x)) & 255;
			const int zi = static_cast<int>(std::floor(z)) & 255;
			const float xf = x - std::floor(x);
			const float zf = z - std::floor(z);
			const float u = Fade(xf);
			const float v = Fade(zf);

			// Indexation périodique sur la table fixe.
			auto P = [](int i) { return kPermBase[i & 255]; };
			const int aa = P(P(xi)     + zi    );
			const int ab = P(P(xi)     + zi + 1);
			const int ba = P(P(xi + 1) + zi    );
			const int bb = P(P(xi + 1) + zi + 1);

			const float x1 = Lerp(Grad2(aa, xf,        zf       ),
			                      Grad2(ba, xf - 1.0f, zf       ), u);
			const float x2 = Lerp(Grad2(ab, xf,        zf - 1.0f),
			                      Grad2(bb, xf - 1.0f, zf - 1.0f), u);
			return Lerp(x1, x2, v);
		}

		/// Smoothstep classique : 0 à `edge0`, 1 à `edge1`, transition cubique.
		/// Note : si edge0 > edge1, on inverse (utile pour falloff radial où on
		/// veut 1 au centre et 0 au bord).
		inline float SmoothStep01(float edge0, float edge1, float x)
		{
			if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
			float t = (x - edge0) / (edge1 - edge0);
			t = std::clamp(t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	float EvalSimplex2D(float x, float z, float freq, uint8_t octaves)
	{
		const uint8_t oct = std::clamp<uint8_t>(octaves, 1, 8);
		float total = 0.0f;
		float amp = 1.0f;
		float ampSum = 0.0f;
		float f = freq;
		for (uint8_t i = 0; i < oct; ++i)
		{
			total += Perlin2D(x * f, z * f) * amp;
			ampSum += amp;
			amp *= 0.5f;
			f *= 2.0f;
		}
		if (ampSum > 0.0f) total /= ampSum;
		return total;
	}

	uint32_t ApplyBrushKernel(engine::world::terrain::TerrainChunk& chunk,
		const TerrainBrushParams& params,
		float centerLocalX, float centerLocalZ,
		float dtSeconds,
		std::vector<TerrainSculptDeltaCell>& outDelta)
	{
		if (chunk.heights.empty()) return 0;
		const float cell = chunk.cellSizeMeters;
		if (cell <= 0.0f) return 0;
		const float radius = std::max(0.0f, params.radiusMeters);
		if (radius <= 0.0f) return 0;
		const float radiusSq = radius * radius;

		// Bornes de la box englobante en cellules.
		const int resX = static_cast<int>(chunk.resolutionX);
		const int resZ = static_cast<int>(chunk.resolutionZ);
		const int xMin = std::max(0,        static_cast<int>(std::floor((centerLocalX - radius) / cell)));
		const int xMax = std::min(resX - 1, static_cast<int>(std::ceil ((centerLocalX + radius) / cell)));
		const int zMin = std::max(0,        static_cast<int>(std::floor((centerLocalZ - radius) / cell)));
		const int zMax = std::min(resZ - 1, static_cast<int>(std::ceil ((centerLocalZ + radius) / cell)));
		if (xMin > xMax || zMin > zMax) return 0;

		// Hauteur cible pour le mode Flatten : capturée AVANT la boucle de
		// modification (sinon le sample bouge à mesure qu'on édite).
		const float flattenTarget = chunk.SampleHeight(centerLocalX, centerLocalZ);

		// Smoothstep : weight = 1 jusqu'à r*(1-falloff), 0 à r.
		const float falloff = std::clamp(params.falloff, 0.0f, 1.0f);
		const float innerRadius = radius * (1.0f - falloff);

		// Pour Smooth, on lit le buffer original (snapshot) afin que l'opérateur
		// box-blur soit isotrope. Capter aussi la base XZ chunk pour les modes
		// dépendants de coordonnées monde (Noise) : `centerLocal*` est déjà en
		// chunk-local, donc on passe directement (chunk.x = 0 dans le repère
		// brush — ce qui suffit pour le déterminisme du test, indépendant des
		// coords globales).
		std::vector<float> snapshot;
		if (params.mode == TerrainBrushMode::Smooth)
		{
			snapshot = chunk.heights;
		}

		uint32_t modified = 0;
		for (int z = zMin; z <= zMax; ++z)
		{
			for (int x = xMin; x <= xMax; ++x)
			{
				const float cx = static_cast<float>(x) * cell - centerLocalX;
				const float cz = static_cast<float>(z) * cell - centerLocalZ;
				const float dSq = cx * cx + cz * cz;
				if (dSq > radiusSq) continue;

				const float dist = std::sqrt(dSq);
				// SmoothStep01(inner, radius, dist) : 0 à inner, 1 à radius. On
				// veut l'inverse (1 au centre, 0 au bord) → 1 - smoothstep.
				const float w = 1.0f - SmoothStep01(innerRadius, radius, dist);
				if (w <= 0.0f) continue;

				const size_t idx = static_cast<size_t>(z) * resX + x;
				float h = chunk.heights[idx];
				float delta = 0.0f;

				switch (params.mode)
				{
					case TerrainBrushMode::Raise:
						delta = params.strengthMps * dtSeconds * w;
						break;
					case TerrainBrushMode::Lower:
						delta = -params.strengthMps * dtSeconds * w;
						break;
					case TerrainBrushMode::Smooth:
					{
						// Box blur 3x3 sur `snapshot` (clamp aux bords).
						float sum = 0.0f;
						int count = 0;
						for (int dz = -1; dz <= 1; ++dz)
						{
							for (int dx = -1; dx <= 1; ++dx)
							{
								const int nx = std::clamp(x + dx, 0, resX - 1);
								const int nz = std::clamp(z + dz, 0, resZ - 1);
								sum += snapshot[static_cast<size_t>(nz) * resX + nx];
								++count;
							}
						}
						const float avg = sum / static_cast<float>(count);
						// Rapprochement pondéré du voisinage. La force module
						// le pourcentage de convergence par tick.
						const float k = std::clamp(params.strengthMps * dtSeconds * w, 0.0f, 1.0f);
						delta = (avg - h) * k;
						break;
					}
					case TerrainBrushMode::Flatten:
					{
						const float k = std::clamp(params.strengthMps * dtSeconds * w, 0.0f, 1.0f);
						delta = (flattenTarget - h) * k;
						break;
					}
					case TerrainBrushMode::Noise:
					{
						const float worldX = static_cast<float>(x) * cell;
						const float worldZ = static_cast<float>(z) * cell;
						const float n = EvalSimplex2D(worldX, worldZ,
							params.noiseFreq, params.noiseOctaves);
						delta = params.strengthMps * dtSeconds * w * n;
						break;
					}
				}

				if (delta == 0.0f) continue;

				// Clamp vers les bornes admissibles globales pour éviter de
				// sortir du domaine valide (cf. constants TerrainChunk).
				const float newH = std::clamp(h + delta,
					engine::world::terrain::kTerrainHeightMinMeters,
					engine::world::terrain::kTerrainHeightMaxMeters);
				const float effectiveDelta = newH - h;
				if (effectiveDelta == 0.0f) continue;

				chunk.heights[idx] = newH;

				TerrainSculptDeltaCell c;
				c.x = static_cast<uint16_t>(x);
				c.z = static_cast<uint16_t>(z);
				c.deltaMeters = effectiveDelta;
				outDelta.push_back(c);
				++modified;
			}
		}
		return modified;
	}
}
