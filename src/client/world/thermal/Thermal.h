#pragma once

// M100.27 — Modèle thermique : contexte (saison/météo/heure) + fonction PURE
// ComputeTemperature, identique côté éditeur (debug) et client (gameplay).
//
// NB : la formule diurne utilise +8·cos((tod-14)/24·2π) → pic à 14 h, creux la
// nuit (cohérent avec « pic 14h » et les cas de test du ticket ; le signe « -8 »
// du brouillon de spec était une coquille).

#include <cstdint>

namespace engine::world::thermal
{
	enum class ThermalSeason : uint8_t { Spring = 0, Summer = 1, Autumn = 2, Winter = 3 };
	enum class ThermalWeather : uint8_t { Clear = 0, Rain = 1, Snow = 2, Drought = 3, Fog = 4 };

	struct ThermalContext
	{
		ThermalSeason season = ThermalSeason::Summer;
		ThermalWeather weather = ThermalWeather::Clear;
		float todHours = 14.0f;
	};

	inline float SeasonBaseTemp(ThermalSeason s)
	{
		switch (s)
		{
			case ThermalSeason::Spring: return 12.0f;
			case ThermalSeason::Summer: return 22.0f;
			case ThermalSeason::Autumn: return 10.0f;
			case ThermalSeason::Winter: return 2.0f;
		}
		return 15.0f;
	}

	inline bool IsSummer(ThermalSeason s) { return s == ThermalSeason::Summer; }

	inline float WeatherDelta(ThermalWeather w)
	{
		switch (w)
		{
			case ThermalWeather::Clear:   return 0.0f;
			case ThermalWeather::Rain:    return -3.0f;
			case ThermalWeather::Snow:    return -5.0f;
			case ThermalWeather::Drought: return 2.0f;
			case ThermalWeather::Fog:     return -1.0f;
		}
		return 0.0f;
	}

	/// Température (°C) à une cellule donnée. `shade01` ∈ [0,1] (1 = canopée
	/// pleine), `altitudeM` en mètres, `distanceToWaterM` distance à l'eau.
	float ComputeTemperature(const ThermalContext& ctx, float altitudeM, float shade01,
	                         float distanceToWaterM);
}
