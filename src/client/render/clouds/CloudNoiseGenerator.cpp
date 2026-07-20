// CloudNoiseGenerator — implémentation. Voir CloudNoiseGenerator.h.

#include "src/client/render/clouds/CloudNoiseGenerator.h"

#include <algorithm>
#include <cmath>

namespace engine::render::clouds
{
	namespace
	{
		/// Hash entier 3D + seed → 32 bits décorrélés. Déterministe et
		/// identique sur toutes plateformes (uint32 wrap défini par la norme).
		uint32_t Hash3(uint32_t x, uint32_t y, uint32_t z, uint32_t seed)
		{
			uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu
				^ seed * 0x9e3779b9u;
			h ^= h >> 13;
			h *= 0x85ebca6bu;
			h ^= h >> 16;
			return h;
		}

		/// Extrait un flottant [0,1) des 24 bits hauts d'un hash.
		float HashToFloat(uint32_t h)
		{
			return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
		}

		/// Modulo positif (les lattices périodiques wrappen des indices
		/// potentiellement négatifs).
		int WrapIndex(int v, int period)
		{
			const int m = v % period;
			return (m < 0) ? m + period : m;
		}

		/// Fade quintique de Perlin (6t⁵-15t⁴+10t³).
		float Fade(float t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		/// Gradient de Perlin : une des 12 directions d'arêtes de cube,
		/// choisie par le hash, projetée sur (fx, fy, fz).
		float Grad(uint32_t h, float fx, float fy, float fz)
		{
			switch (h & 15u)
			{
				case 0:  return  fx + fy;
				case 1:  return -fx + fy;
				case 2:  return  fx - fy;
				case 3:  return -fx - fy;
				case 4:  return  fx + fz;
				case 5:  return -fx + fz;
				case 6:  return  fx - fz;
				case 7:  return -fx - fz;
				case 8:  return  fy + fz;
				case 9:  return -fy + fz;
				case 10: return  fy - fz;
				case 11: return -fy - fz;
				case 12: return  fx + fy;
				case 13: return -fx + fy;
				case 14: return -fy + fz;
				default: return -fy - fz;
			}
		}

		/// Bruit de Perlin 3D périodique, valeur ~[-1,1].
		float TileablePerlin(float x, float y, float z, int period, uint32_t seed)
		{
			const float px = x * static_cast<float>(period);
			const float py = y * static_cast<float>(period);
			const float pz = z * static_cast<float>(period);
			const int ix = static_cast<int>(std::floor(px));
			const int iy = static_cast<int>(std::floor(py));
			const int iz = static_cast<int>(std::floor(pz));
			const float fx = px - static_cast<float>(ix);
			const float fy = py - static_cast<float>(iy);
			const float fz = pz - static_cast<float>(iz);

			const float u = Fade(fx);
			const float v = Fade(fy);
			const float w = Fade(fz);

			// Hash des 8 coins du cube, indices wrappés sur la période.
			auto corner = [&](int dx, int dy, int dz)
			{
				return Hash3(
					static_cast<uint32_t>(WrapIndex(ix + dx, period)),
					static_cast<uint32_t>(WrapIndex(iy + dy, period)),
					static_cast<uint32_t>(WrapIndex(iz + dz, period)),
					seed);
			};
			const float n000 = Grad(corner(0, 0, 0), fx,        fy,        fz);
			const float n100 = Grad(corner(1, 0, 0), fx - 1.0f, fy,        fz);
			const float n010 = Grad(corner(0, 1, 0), fx,        fy - 1.0f, fz);
			const float n110 = Grad(corner(1, 1, 0), fx - 1.0f, fy - 1.0f, fz);
			const float n001 = Grad(corner(0, 0, 1), fx,        fy,        fz - 1.0f);
			const float n101 = Grad(corner(1, 0, 1), fx - 1.0f, fy,        fz - 1.0f);
			const float n011 = Grad(corner(0, 1, 1), fx,        fy - 1.0f, fz - 1.0f);
			const float n111 = Grad(corner(1, 1, 1), fx - 1.0f, fy - 1.0f, fz - 1.0f);

			const float nx00 = n000 + u * (n100 - n000);
			const float nx10 = n010 + u * (n110 - n010);
			const float nx01 = n001 + u * (n101 - n001);
			const float nx11 = n011 + u * (n111 - n011);
			const float nxy0 = nx00 + v * (nx10 - nx00);
			const float nxy1 = nx01 + v * (nx11 - nx01);
			return nxy0 + w * (nxy1 - nxy0);
		}

		/// Quantifie [0,1] → octet.
		uint8_t ToByte(float v)
		{
			const float c = std::clamp(v, 0.0f, 1.0f);
			return static_cast<uint8_t>(c * 255.0f + 0.5f);
		}
	}

	float TileableWorley(float x, float y, float z, int cells, uint32_t seed)
	{
		// Coordonnées en unités de cellule ; wrap sur [0, cells).
		const float cx = (x - std::floor(x)) * static_cast<float>(cells);
		const float cy = (y - std::floor(y)) * static_cast<float>(cells);
		const float cz = (z - std::floor(z)) * static_cast<float>(cells);
		const int ix = static_cast<int>(cx);
		const int iy = static_cast<int>(cy);
		const int iz = static_cast<int>(cz);

		float minDistSq = 1e9f;
		for (int dz = -1; dz <= 1; ++dz)
		{
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dx = -1; dx <= 1; ++dx)
				{
					// Cellule voisine (indices wrap pour la périodicité) ; le
					// point caractéristique vit dans la cellule NON wrappée
					// (position continue), son offset vient du hash wrappé.
					const int nx = ix + dx;
					const int ny = iy + dy;
					const int nz = iz + dz;
					const uint32_t h = Hash3(
						static_cast<uint32_t>(WrapIndex(nx, cells)),
						static_cast<uint32_t>(WrapIndex(ny, cells)),
						static_cast<uint32_t>(WrapIndex(nz, cells)),
						seed);
					const float ox = HashToFloat(h);
					const float oy = HashToFloat(Hash3(h, 0x27d4eb2fu, 0x165667b1u, seed));
					const float oz = HashToFloat(Hash3(h, 0x85ebca6bu, 0xc2b2ae35u, seed));
					const float px = static_cast<float>(nx) + ox;
					const float py = static_cast<float>(ny) + oy;
					const float pz = static_cast<float>(nz) + oz;
					const float ddx = px - cx;
					const float ddy = py - cy;
					const float ddz = pz - cz;
					const float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
					minDistSq = std::min(minDistSq, d2);
				}
			}
		}
		// Distance (en unités de cellule) au point caractéristique le plus
		// proche, clampée puis inversée : 1 SUR un point, 0 loin de tout —
		// donne les « boules cotonneuses » attendues pour des cumulus.
		const float d = std::sqrt(minDistSq);
		return 1.0f - std::clamp(d, 0.0f, 1.0f);
	}

	float TileablePerlinFbm(float x, float y, float z, int basePeriod,
		int octaves, uint32_t seed)
	{
		float sum = 0.0f;
		float amp = 0.5f;
		float norm = 0.0f;
		int period = basePeriod;
		for (int i = 0; i < std::max(octaves, 1); ++i)
		{
			// Fréquence ET période doublent : chaque octave reste périodique
			// sur [0,1) → la somme tuile.
			sum += amp * TileablePerlin(x, y, z, period,
				seed + static_cast<uint32_t>(i) * 0x68e31da4u);
			norm += amp;
			amp *= 0.5f;
			period *= 2;
		}
		// Normalise ~[-1,1] → [0,1].
		const float n = (norm > 0.0f) ? sum / norm : 0.0f;
		return std::clamp(n * 0.5f + 0.5f, 0.0f, 1.0f);
	}

	CloudNoiseData GenerateCloudNoise(uint32_t seed)
	{
		CloudNoiseData out;

		// -- Texture de base 64³ : R = Perlin fBm, G/B/A = Worley 8/16/32. --
		{
			const int s = kBaseNoiseSize;
			out.baseRgba.resize(static_cast<size_t>(s) * s * s * 4u);
			size_t idx = 0;
			for (int z = 0; z < s; ++z)
			{
				const float fz = (static_cast<float>(z) + 0.5f) / static_cast<float>(s);
				for (int y = 0; y < s; ++y)
				{
					const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(s);
					for (int x = 0; x < s; ++x)
					{
						const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(s);
						out.baseRgba[idx++] = ToByte(
							TileablePerlinFbm(fx, fy, fz, 4, 4, seed));
						out.baseRgba[idx++] = ToByte(
							TileableWorley(fx, fy, fz, 8, seed ^ 0xA341316Cu));
						out.baseRgba[idx++] = ToByte(
							TileableWorley(fx, fy, fz, 16, seed ^ 0xC8013EA4u));
						out.baseRgba[idx++] = ToByte(
							TileableWorley(fx, fy, fz, 32, seed ^ 0xAD90777Du));
					}
				}
			}
		}

		// -- Texture de détail 32³ : R/G/B = Worley 4/8/16, A = 255. --------
		{
			const int s = kDetailNoiseSize;
			out.detailRgba.resize(static_cast<size_t>(s) * s * s * 4u);
			size_t idx = 0;
			for (int z = 0; z < s; ++z)
			{
				const float fz = (static_cast<float>(z) + 0.5f) / static_cast<float>(s);
				for (int y = 0; y < s; ++y)
				{
					const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(s);
					for (int x = 0; x < s; ++x)
					{
						const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(s);
						out.detailRgba[idx++] = ToByte(
							TileableWorley(fx, fy, fz, 4, seed ^ 0x7B16763Bu));
						out.detailRgba[idx++] = ToByte(
							TileableWorley(fx, fy, fz, 8, seed ^ 0x2F2C58C1u));
						out.detailRgba[idx++] = ToByte(
							TileableWorley(fx, fy, fz, 16, seed ^ 0x91E10DA5u));
						out.detailRgba[idx++] = 255u;
					}
				}
			}
		}

		return out;
	}

	std::vector<uint8_t> GenerateWeatherCoverageMap(uint32_t seed)
	{
		// fBm 2D périodique via le Perlin 3D à tranche z FIGÉE : la coupe d'un
		// bruit périodique en x/y reste périodique en x/y (seule la périodicité
		// z est perdue, sans objet pour une texture 2D). Période de base 4
		// (grosses masses ~ quart de tuile), 4 octaves pour du relief de front.
		std::vector<uint8_t> out(static_cast<size_t>(kWeatherMapSize) * kWeatherMapSize);
		constexpr float kSliceZ = 0.37f;
		size_t idx = 0;
		for (int y = 0; y < kWeatherMapSize; ++y)
		{
			const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(kWeatherMapSize);
			for (int x = 0; x < kWeatherMapSize; ++x)
			{
				const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(kWeatherMapSize);
				const float n = TileablePerlinFbm(fx, fy, kSliceZ, 4, 4,
					seed ^ 0x5bd1e995u);
				// Accentue le contraste autour de la moyenne (~0.5) : creuse de
				// vraies trouées et de vrais paquets au lieu d'une brume uniforme.
				const float c = std::clamp((n - 0.30f) / 0.40f, 0.0f, 1.0f);
				out[idx++] = ToByte(c);
			}
		}
		return out;
	}
}
