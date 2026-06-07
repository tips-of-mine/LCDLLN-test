#pragma once
// Mapper PUR : type météo client (engine::render::WeatherState) -> CloudParams.
// Aucune dépendance Vulkan. Source unique de vérité de l'apparence des nuages
// en fonction de l'état météo serveur déjà diffusé (réutilise WeatherSystem).

#include "src/client/render/clouds/CloudParams.h"
#include "src/client/render/WeatherSystem.h" // WeatherState

namespace engine::render
{
	class CloudWeatherMapper
	{
	public:
		/// Retourne les CloudParams cibles pour un état météo donné.
		/// Déterministe, sans état, testable sans GPU.
		static CloudParams ParamsFor(WeatherState state);
	};
}
