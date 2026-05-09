#pragma once
// CMANGOS.42 (Phase 4.42a) — WeatherManager : driver server-side
// pour la meteo par zone. Etats discrets (Clear / Rain / Snow /
// Storm / Sandstorm) avec transitions deterministes via seed +
// nowMs. Pure data, pas de wire ni DB.

#include <cstdint>
#include <random>
#include <unordered_map>

namespace engine::server::weather
{
	using ZoneId = uint32_t;

	enum class WeatherKind : uint8_t
	{
		Clear     = 0,
		Rain      = 1,
		Snow      = 2,
		Storm     = 3,
		Sandstorm = 4,
		Fog       = 5,
	};

	struct ZoneWeatherState
	{
		WeatherKind kind         = WeatherKind::Clear;
		float       intensity    = 0.0f;       ///< 0..1
		uint64_t    nextChangeTsMs = 0;        ///< prochain reroll
	};

	struct ZoneWeatherProfile
	{
		ZoneId    zoneId          = 0;
		uint32_t  changeIntervalMs = 60 * 60 * 1000;  ///< default 1h
		/// Probabilites cumulees pour chaque WeatherKind ; somme idealement 1.0.
		float pClear     = 0.5f;
		float pRain      = 0.2f;
		float pSnow      = 0.0f;
		float pStorm     = 0.05f;
		float pSandstorm = 0.0f;
		float pFog       = 0.05f;
	};

	class WeatherManager
	{
	public:
		void RegisterZone(ZoneWeatherProfile profile)
		{
			m_profiles[profile.zoneId] = profile;
			ZoneWeatherState s;
			s.nextChangeTsMs = 0;  // first tick reroll immediately
			m_state[profile.zoneId] = s;
		}

		ZoneWeatherState GetState(ZoneId zoneId) const
		{
			auto it = m_state.find(zoneId);
			return (it == m_state.end()) ? ZoneWeatherState{} : it->second;
		}

		/// Tick called periodically (tick rate doesn't have to match
		/// changeIntervalMs ; nextChangeTsMs gates the actual reroll).
		void Tick(uint64_t nowMs, std::mt19937& rng)
		{
			for (auto& [zone, state] : m_state)
			{
				if (nowMs < state.nextChangeTsMs) continue;
				const auto it = m_profiles.find(zone);
				if (it == m_profiles.end()) continue;
				state.kind          = RollKind(it->second, rng);
				state.intensity     = RollIntensity(rng);
				state.nextChangeTsMs = nowMs + it->second.changeIntervalMs;
			}
		}

		size_t ZoneCount() const noexcept { return m_state.size(); }

	private:
		static WeatherKind RollKind(const ZoneWeatherProfile& p, std::mt19937& rng)
		{
			std::uniform_real_distribution<float> d(0.0f, 1.0f);
			float r = d(rng);
			if ((r -= p.pClear)     < 0) return WeatherKind::Clear;
			if ((r -= p.pRain)      < 0) return WeatherKind::Rain;
			if ((r -= p.pSnow)      < 0) return WeatherKind::Snow;
			if ((r -= p.pStorm)     < 0) return WeatherKind::Storm;
			if ((r -= p.pSandstorm) < 0) return WeatherKind::Sandstorm;
			if ((r -= p.pFog)       < 0) return WeatherKind::Fog;
			return WeatherKind::Clear;
		}

		static float RollIntensity(std::mt19937& rng)
		{
			std::uniform_real_distribution<float> d(0.3f, 1.0f);
			return d(rng);
		}

		std::unordered_map<ZoneId, ZoneWeatherProfile> m_profiles;
		std::unordered_map<ZoneId, ZoneWeatherState>   m_state;
	};
}
