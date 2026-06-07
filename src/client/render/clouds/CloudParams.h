#pragma once
// Apparence des nuages volumétriques, dérivée de la météo (CloudWeatherMapper)
// puis interpolée pour les transitions douces. Pur CPU, aucune dépendance Vulkan.

namespace engine::render
{
	/// Paramètres d'apparence d'une couche nuageuse. Toutes les valeurs sont
	/// continues pour permettre l'interpolation entre deux états météo.
	struct CloudParams
	{
		float coverage      = 0.4f;   ///< [0..1] fraction de ciel couverte.
		float density       = 0.6f;   ///< [0..2] opacité/épaisseur du milieu.
		float baseAltMeters = 800.0f; ///< Altitude (m) de la base des nuages.
		float topAltMeters  = 2200.0f;///< Altitude (m) du sommet des nuages.
		float tintR         = 1.0f;   ///< Teinte multiplicative R (1 = neutre).
		float tintG         = 1.0f;   ///< Teinte multiplicative G.
		float tintB         = 1.0f;   ///< Teinte multiplicative B.

		/// Interpolation linéaire composant par composant. \p t est clampé à [0,1].
		static CloudParams Lerp(const CloudParams& a, const CloudParams& b, float t)
		{
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			CloudParams r;
			r.coverage      = a.coverage      + (b.coverage      - a.coverage)      * t;
			r.density       = a.density       + (b.density       - a.density)       * t;
			r.baseAltMeters = a.baseAltMeters + (b.baseAltMeters - a.baseAltMeters) * t;
			r.topAltMeters  = a.topAltMeters  + (b.topAltMeters  - a.topAltMeters)  * t;
			r.tintR         = a.tintR         + (b.tintR         - a.tintR)         * t;
			r.tintG         = a.tintG         + (b.tintG         - a.tintG)         * t;
			r.tintB         = a.tintB         + (b.tintB         - a.tintB)         * t;
			return r;
		}
	};
}
