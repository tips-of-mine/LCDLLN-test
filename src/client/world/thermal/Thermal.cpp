// M100.27 — Implémentation de ComputeTemperature (pure).

#include "src/client/world/thermal/Thermal.h"

#include <cmath>

namespace engine::world::thermal
{
	float ComputeTemperature(const ThermalContext& ctx, float altitudeM, float shade01,
	                         float distanceToWaterM)
	{
		constexpr float kTwoPi = 6.28318530718f;
		const float base = SeasonBaseTemp(ctx.season);
		// Pic à 14 h, creux ~2 h.
		const float diurnal = 8.0f * std::cos((ctx.todHours - 14.0f) / 24.0f * kTwoPi);
		const float altGrad = -altitudeM * 0.0065f; // -6.5 °C / 1000 m
		const float canopyFull = IsSummer(ctx.season) ? -8.0f : 3.0f; // ombre rafraîchit l'été, abrite l'hiver
		const float s = (shade01 < 0.0f) ? 0.0f : (shade01 > 1.0f ? 1.0f : shade01);
		const float canopy = canopyFull * s;
		const float weather = WeatherDelta(ctx.weather);
		const float water = (distanceToWaterM < 30.0f) ? -1.0f : 0.0f;
		return base + diurnal + altGrad + canopy + weather + water;
	}
}
