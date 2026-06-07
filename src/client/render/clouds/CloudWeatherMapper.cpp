#include "src/client/render/clouds/CloudWeatherMapper.h"

namespace engine::render
{
	CloudParams CloudWeatherMapper::ParamsFor(WeatherState state)
	{
		CloudParams p;
		switch (state)
		{
		case WeatherState::Clear:
			p.coverage = 0.25f; p.density = 0.45f;
			p.baseAltMeters = 1200.0f; p.topAltMeters = 2600.0f;
			p.tintR = 1.0f; p.tintG = 1.0f; p.tintB = 1.0f;
			break;
		case WeatherState::Rain:
			p.coverage = 0.85f; p.density = 1.1f;
			p.baseAltMeters = 700.0f; p.topAltMeters = 2200.0f;
			p.tintR = 0.7f; p.tintG = 0.72f; p.tintB = 0.78f;
			break;
		case WeatherState::Snow:
			p.coverage = 0.7f; p.density = 0.8f;
			p.baseAltMeters = 800.0f; p.topAltMeters = 2000.0f;
			p.tintR = 0.92f; p.tintG = 0.94f; p.tintB = 1.0f;
			break;
		case WeatherState::Fog:
			p.coverage = 0.6f; p.density = 0.7f;
			p.baseAltMeters = 300.0f; p.topAltMeters = 1200.0f; // couche basse
			p.tintR = 0.85f; p.tintG = 0.86f; p.tintB = 0.88f;
			break;
		case WeatherState::Storm:
			p.coverage = 0.97f; p.density = 1.8f;
			p.baseAltMeters = 600.0f; p.topAltMeters = 3000.0f; // cumulonimbus
			p.tintR = 0.42f; p.tintG = 0.44f; p.tintB = 0.5f;
			break;
		default:
			break; // garde les defauts de CloudParams (clear-ish).
		}
		return p;
	}
}
