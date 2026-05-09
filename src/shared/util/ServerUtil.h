#pragma once
// CMANGOS.41 (Phase 5.41a) — ServerUtil : utilitaires serveur reutilisables
// (random deterministe, parse helpers, time helpers). Header-only.

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace engine::server::util
{
	/// Generateur deterministe (Mersenne Twister) pour seedable random.
	/// Utile pour tests reproductibles et loot/spawn deterministes.
	class DeterministicRng
	{
	public:
		explicit DeterministicRng(uint64_t seed) : m_rng(seed) {}

		/// Entier uniforme dans [lo, hi] inclus.
		int32_t IntRange(int32_t lo, int32_t hi)
		{
			std::uniform_int_distribution<int32_t> d(lo, hi);
			return d(m_rng);
		}

		/// Float uniforme dans [0, 1).
		float Unit() { std::uniform_real_distribution<float> d(0.0f, 1.0f); return d(m_rng); }

		/// Retourne true avec probabilite p (0..1).
		bool Chance(float p) { return Unit() < p; }

	private:
		std::mt19937_64 m_rng;
	};

	/// Split d'une chaine sur un separateur. Tokens vides preserves.
	inline std::vector<std::string_view> Split(std::string_view s, char sep)
	{
		std::vector<std::string_view> out;
		size_t start = 0;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == sep)
			{
				out.push_back(s.substr(start, i - start));
				start = i + 1;
			}
		}
		out.push_back(s.substr(start));
		return out;
	}

	/// Parse un entier non signe simple. Retourne nullopt si invalide.
	inline std::optional<uint64_t> ParseU64(std::string_view s)
	{
		if (s.empty()) return std::nullopt;
		uint64_t v = 0;
		for (char c : s)
		{
			if (c < '0' || c > '9') return std::nullopt;
			v = v * 10 + static_cast<uint64_t>(c - '0');
		}
		return v;
	}

	/// Format duree ms -> "HH:MM:SS" (depasse 24h, format reste 99:59:59 max).
	inline std::string FormatDurationMs(uint64_t ms)
	{
		uint64_t s = ms / 1000;
		uint64_t h = s / 3600; s %= 3600;
		uint64_t m = s / 60;   s %= 60;
		if (h > 99) h = 99;
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu",
		              static_cast<unsigned long long>(h),
		              static_cast<unsigned long long>(m),
		              static_cast<unsigned long long>(s));
		return std::string(buf);
	}
}
