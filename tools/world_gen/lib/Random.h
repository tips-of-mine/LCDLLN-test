#pragma once

// tools/world_gen — hasard déterministe et bruits procéduraux.
//
// Tout le générateur dérive son hasard de fonctions de HACHAGE pures (pas
// d'état global, pas de std::random_device, pas de distribution std:: dont
// l'implémentation varie entre bibliothèques standard) : mêmes entrées ⇒
// mêmes octets en sortie, quelle que soit la plateforme.

#include <cmath>
#include <cstdint>

namespace tools::world_gen
{
	/// Mélangeur SplitMix64 (finaliseur de qualité, bijectif).
	inline uint64_t Mix64(uint64_t x)
	{
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	/// Hash combiné de plusieurs entiers (ordre significatif).
	inline uint64_t Hash(uint64_t a, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0)
	{
		uint64_t h = Mix64(a);
		h = Mix64(h ^ (b + 0x632BE59BD9B4E019ull));
		h = Mix64(h ^ (c + 0x8CB92BA72F3D8DD7ull));
		h = Mix64(h ^ (d + 0xA0761D6478BD642Full));
		return h;
	}

	/// Hash d'une chaîne (FNV-1a 64) — pour dériver des graines par identifiant.
	inline uint64_t HashString(const char* s)
	{
		uint64_t h = 0xCBF29CE484222325ull;
		for (; *s; ++s) { h ^= static_cast<unsigned char>(*s); h *= 0x100000001B3ull; }
		return h;
	}

	/// Réel uniforme dans [0,1) depuis un hash 64 bits (53 bits de mantisse).
	inline double ToUnit(uint64_t h)
	{
		return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
	}

	/// Petit générateur à état (flux déterministe local à une tâche).
	class Rng
	{
	public:
		explicit Rng(uint64_t seed) : m_state(Mix64(seed ^ 0xD1B54A32D192ED03ull)) {}
		uint64_t NextU64() { m_state = Mix64(m_state); return m_state; }
		/// Uniforme [0,1).
		double Next() { return ToUnit(NextU64()); }
		/// Uniforme [lo,hi).
		double Range(double lo, double hi) { return lo + (hi - lo) * Next(); }
		/// Entier uniforme [0,n) (n > 0).
		uint32_t Below(uint32_t n) { return n == 0 ? 0u : static_cast<uint32_t>(NextU64() % n); }
		/// Vrai avec probabilité p.
		bool Chance(double p) { return Next() < p; }
	private:
		uint64_t m_state;
	};

	/// Bruit de valeur 2D lissé (quintique) dans [-1,1], période infinie.
	/// \param x,z coordonnées (unité = taille d'une cellule de bruit)
	inline double ValueNoise2(double x, double z, uint64_t seed)
	{
		const double fx = std::floor(x), fz = std::floor(z);
		const int64_t ix = static_cast<int64_t>(fx), iz = static_cast<int64_t>(fz);
		const double tx = x - fx, tz = z - fz;
		auto smooth = [](double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
		auto lat = [seed](int64_t a, int64_t b) {
			return ToUnit(Hash(seed, static_cast<uint64_t>(a), static_cast<uint64_t>(b))) * 2.0 - 1.0;
		};
		const double sx = smooth(tx), sz = smooth(tz);
		const double v00 = lat(ix, iz), v10 = lat(ix + 1, iz);
		const double v01 = lat(ix, iz + 1), v11 = lat(ix + 1, iz + 1);
		const double a = v00 + (v10 - v00) * sx;
		const double b = v01 + (v11 - v01) * sx;
		return a + (b - a) * sz;
	}

	/// Mouvement brownien fractionnaire (somme d'octaves) dans ~[-1,1].
	/// \param wavelength longueur d'onde de la 1re octave (m)
	inline double Fbm(double x, double z, double wavelength, int octaves, uint64_t seed,
		double persistence = 0.5, double lacunarity = 2.0)
	{
		double sum = 0.0, amp = 1.0, norm = 0.0, f = 1.0 / wavelength;
		for (int o = 0; o < octaves; ++o)
		{
			sum += amp * ValueNoise2(x * f, z * f, seed + static_cast<uint64_t>(o) * 7919ull);
			norm += amp;
			amp *= persistence;
			f *= lacunarity;
		}
		return norm > 0.0 ? sum / norm : 0.0;
	}

	/// Bruit « ridged » (crêtes montagneuses) dans [0,1].
	inline double Ridged(double x, double z, double wavelength, int octaves, uint64_t seed)
	{
		double sum = 0.0, amp = 1.0, norm = 0.0, f = 1.0 / wavelength, weight = 1.0;
		for (int o = 0; o < octaves; ++o)
		{
			double n = 1.0 - std::fabs(ValueNoise2(x * f, z * f, seed + static_cast<uint64_t>(o) * 104729ull));
			n *= n;
			n *= weight;
			weight = std::fmin(1.0, std::fmax(0.0, n * 2.0));
			sum += n * amp;
			norm += amp;
			amp *= 0.5;
			f *= 2.0;
		}
		return norm > 0.0 ? sum / norm : 0.0;
	}

	/// Interpolation de Hermite bornée (comme GLSL smoothstep).
	inline double SmoothStep(double e0, double e1, double x)
	{
		if (e1 == e0) return x < e0 ? 0.0 : 1.0;
		double t = (x - e0) / (e1 - e0);
		t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
		return t * t * (3.0 - 2.0 * t);
	}

	inline double Lerp(double a, double b, double t) { return a + (b - a) * t; }
	inline double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
}
