#pragma once

// M100.27 — Service de requête thermique (RAM client). Header-only. Recalcule
// implicitement via le contexte : OnSeasonChanged/OnWeatherChanged mettent à
// jour le contexte (et incrémentent un compteur de rebuild) ; Query évalue
// ComputeTemperature avec le contexte courant + les échantillonneurs fournis.

#include <cstdint>
#include <functional>

#include "src/client/world/thermal/Thermal.h"

namespace engine::world::thermal
{
	class ThermalQueryService
	{
	public:
		using FieldSampler = std::function<float(float worldX, float worldZ)>;

		void Init(const ThermalContext& ctx, FieldSampler altitudeM, FieldSampler shade01,
		          FieldSampler distanceToWaterM)
		{
			m_ctx = ctx;
			m_altitude = std::move(altitudeM);
			m_shade = std::move(shade01);
			m_water = std::move(distanceToWaterM);
			++m_rebuildCount;
		}

		void OnSeasonChanged(ThermalSeason s) { m_ctx.season = s; ++m_rebuildCount; }
		void OnWeatherChanged(ThermalWeather w) { m_ctx.weather = w; ++m_rebuildCount; }
		void SetTimeOfDay(float hours) { m_ctx.todHours = hours; }

		int RebuildCount() const { return m_rebuildCount; }
		const ThermalContext& Context() const { return m_ctx; }

		float Query(float worldX, float worldZ) const
		{
			const float alt = m_altitude ? m_altitude(worldX, worldZ) : 0.0f;
			const float shade = m_shade ? m_shade(worldX, worldZ) : 0.0f;
			const float water = m_water ? m_water(worldX, worldZ) : 1.0e9f;
			return ComputeTemperature(m_ctx, alt, shade, water);
		}

	private:
		ThermalContext m_ctx;
		FieldSampler m_altitude;
		FieldSampler m_shade;
		FieldSampler m_water;
		int m_rebuildCount = 0;
	};
}
