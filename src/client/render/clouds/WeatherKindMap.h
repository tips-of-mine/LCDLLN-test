#pragma once
// Conversion PURE du WeatherKind serveur (wire opcode 156, uint8) vers le
// WeatherState client (engine::render::WeatherState). Les deux enums DIFFERENT
// (indices Fog/Storm inversés ; pas de Sandstorm côté client) : mapping explicite,
// jamais un cast. Testable sans GPU.

#include "src/client/render/WeatherSystem.h" // WeatherState

#include <cstdint>

namespace engine::render
{
	/// \param serverKind valeur wire (Clear=0,Rain=1,Snow=2,Storm=3,Sandstorm=4,Fog=5).
	/// \return WeatherState client correspondant ; Clear pour toute valeur inconnue.
	inline WeatherState MapServerKindToWeatherState(uint8_t serverKind)
	{
		switch (serverKind)
		{
		case 0: return WeatherState::Clear;
		case 1: return WeatherState::Rain;
		case 2: return WeatherState::Snow;
		case 3: return WeatherState::Storm;      // serveur Storm
		case 4: return WeatherState::Storm;      // serveur Sandstorm -> repli Storm
		case 5: return WeatherState::Fog;        // serveur Fog
		default: return WeatherState::Clear;
		}
	}
}
