#include "engine/render/DayNightCycle.h"

#include "engine/core/Log.h"

#include <cmath>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// Constants
	// -------------------------------------------------------------------------

	namespace
	{
		constexpr float kPi    = 3.14159265358979323846f;
		constexpr float kTwoPi = 6.28318530717958647692f;

		// ---- Sky colour keyframes  (time in hours) --------------------------

		/// Night sky (0 h = midnight).
		constexpr float kSkyZenithNight[3]  = { 0.01f, 0.02f, 0.08f };
		constexpr float kSkyHorizonNight[3] = { 0.04f, 0.06f, 0.14f };

		/// Dawn / dusk sky (≈ 6 h / 18 h).
		constexpr float kSkyZenithDawn[3]  = { 0.30f, 0.18f, 0.45f };
		constexpr float kSkyHorizonDawn[3] = { 0.90f, 0.50f, 0.20f };

		/// Noon sky (12 h).
		constexpr float kSkyZenithNoon[3]  = { 0.08f, 0.22f, 0.68f };
		constexpr float kSkyHorizonNoon[3] = { 0.55f, 0.76f, 0.96f };

		// ---- Light colour keyframes ------------------------------------------

		/// Noon sunlight colour (white-ish).
		constexpr float kLightColorNoon[3]    = { 1.00f, 0.98f, 0.90f };
		/// Sunrise / sunset colour (warm orange).
		constexpr float kLightColorSunrise[3] = { 1.00f, 0.55f, 0.20f };
		/// Moonlight colour (cold, dim).
		constexpr float kLightColorMoon[3]    = { 0.20f, 0.25f, 0.45f };

		/// Intensity multipliers for the directional light.
		constexpr float kIntensityDay   = 1.00f;
		constexpr float kIntensityNight = 0.10f; ///< Moonlight (spec: 0.1).

		/// Minimum ambient to avoid pitch-black nights (spec: "avoid pitch black night").
		constexpr float kAmbientNightMin = 0.04f;

		// ---- Azimuth: fixed east->west sun path (simplified) ----------------
		/// Azimuth angle at dawn (radians) — sun rises in the east.
		constexpr float kAzimuthDawn  = kPi * 0.75f; // 135°
		/// Azimuth angle at dusk (radians) — sun sets in the west.
		constexpr float kAzimuthDusk  = kPi * 0.25f; // 45°
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	/*static*/ void DayNightCycle::LerpColor3(const float a[3], const float b[3], float t, float out[3])
	{
		out[0] = a[0] + (b[0] - a[0]) * t;
		out[1] = a[1] + (b[1] - a[1]) * t;
		out[2] = a[2] + (b[2] - a[2]) * t;
	}

	/*static*/ float DayNightCycle::Clamp(float v, float lo, float hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}

	// -------------------------------------------------------------------------
	// DayNightCycle::Init
	// -------------------------------------------------------------------------

	void DayNightCycle::Init(const Params& params)
	{
		m_timeOfDay = params.initialTimeOfDay;
		m_timeScale = (params.timeScale > 0.0f) ? params.timeScale : 60.0f;

		// Wrap initial time into [0, 24).
		while (m_timeOfDay >= 24.0f) m_timeOfDay -= 24.0f;
		while (m_timeOfDay <   0.0f) m_timeOfDay += 24.0f;

		ComputeState();

		LOG_INFO(Render, "[DayNightCycle] Init OK (time={:.2f}h timeScale={:.1f}x)",
		         m_timeOfDay, m_timeScale);
	}

	// -------------------------------------------------------------------------
	// DayNightCycle::Advance
	// -------------------------------------------------------------------------

	void DayNightCycle::Advance(float deltaSeconds)
	{
		// Advance in-game time: each real second = timeScale in-game hours / 3600 * timeScale.
		// timeScale encodes "game-hours per real-hour", so:
		//   inGameHoursPerSecond = timeScale / 3600.
		// However the spec says "timeScale 60 = 1h realtime = 1day ingame", which means:
		//   60 real minutes → 24 game hours → timeScale=60 game-hours per real-hour.
		//   inGameHoursPerRealSecond = 60 / 3600 = 1/60.
		// So: deltaTimeH = deltaSeconds * timeScale / 3600.
		const float deltaHours = deltaSeconds * (m_timeScale / 3600.0f);
		m_timeOfDay += deltaHours;

		// Wrap to [0, 24).
		while (m_timeOfDay >= 24.0f) m_timeOfDay -= 24.0f;

		ComputeState();
	}

	// -------------------------------------------------------------------------
	// DayNightCycle::SetTime
	// -------------------------------------------------------------------------

	void DayNightCycle::SetTime(float hours)
	{
		while (hours >= 24.0f) hours -= 24.0f;
		while (hours <   0.0f) hours += 24.0f;
		m_timeOfDay = hours;
		ComputeState();
		LOG_INFO(Render, "[DayNightCycle] SetTime → {:.2f}h", m_timeOfDay);
	}

	/// Met a jour le timeScale (secondes reelles par heure in-game). Borne a
	/// [0.1, 1000.0] pour eviter les problemes de division ou wraparound. Appele
	/// par le panneau "Atmosphere" de l'editeur monde quand l'utilisateur deplace
	/// le slider de vitesse du temps.
	void DayNightCycle::SetTimeScale(float realSecondsPerHour)
	{
		if (realSecondsPerHour < 0.1f) realSecondsPerHour = 0.1f;
		if (realSecondsPerHour > 1000.0f) realSecondsPerHour = 1000.0f;
		m_timeScale = realSecondsPerHour;
		LOG_INFO(Render, "[DayNightCycle] SetTimeScale → {:.2f}s/h", m_timeScale);
	}

	// -------------------------------------------------------------------------
	// DayNightCycle::ComputeState
	// -------------------------------------------------------------------------
	//
	// Celestial mechanics (simplified, as specified in ticket):
	//
	//   sunElevation = sin(timeOfDay * π / 12 − π/2)
	//
	// This gives:
	//   t=0h  (midnight) → elevation = sin(−π/2)   = −1   (directly below)
	//   t=6h  (sunrise)  → elevation = sin(0)       =  0   (on horizon)
	//   t=12h (noon)     → elevation = sin(π/2)     = +1   (directly above)
	//   t=18h (sunset)   → elevation = sin(π)       =  0   (on horizon)
	//   t=24h (midnight) → elevation = sin(3π/2)    = −1   (directly below)
	//
	// Azimuth sweeps from dawn azimuth to dusk azimuth over the daytime arc.
	// Moon is the sun offset by 12 h (opposite side).
	//
	// -------------------------------------------------------------------------

	void DayNightCycle::ComputeState()
	{
		m_state.timeOfDay = m_timeOfDay;

		// ---- Sun elevation (per spec) ----------------------------------------
		const float sunElevation = std::sin(m_timeOfDay * kPi / 12.0f - kPi / 2.0f);

		// ---- Azimuth (linear sweep east→west across daytime arc) ------------
		// Maps time 6h→18h (daytime) onto azimuth dawn→dusk.
		const float dayFraction = Clamp((m_timeOfDay - 6.0f) / 12.0f, 0.0f, 1.0f);
		const float sunAzimuth  = kAzimuthDawn + (kAzimuthDusk - kAzimuthDawn) * dayFraction;

		// ---- Sun direction (toward-sun unit vector) --------------------------
		// When elevation < 0 the sun is below the horizon; we switch to the moon.
		const bool isSunUp = (sunElevation > 0.0f);
		m_state.isDaytime  = isSunUp;

		float activeLightElevation = sunElevation;
		float activeLightAzimuth   = sunAzimuth;

		if (!isSunUp)
		{
			// Moon is 12 h offset from the sun (opposite side of the sky).
			const float moonTime      = m_timeOfDay + 12.0f;
			const float moonTimeW     = moonTime >= 24.0f ? moonTime - 24.0f : moonTime;
			activeLightElevation      = std::sin(moonTimeW * kPi / 12.0f - kPi / 2.0f);
			const float moonDayFrac   = Clamp((moonTimeW - 6.0f) / 12.0f, 0.0f, 1.0f);
			activeLightAzimuth        = kAzimuthDawn + (kAzimuthDusk - kAzimuthDawn) * moonDayFrac;
		}

		// Clamp active elevation to avoid the light going underground when both are down.
		// Per spec: minimum ambient keeps the scene never pitch-black.
		const float clampedElev = Clamp(activeLightElevation, -1.0f, 1.0f);

		m_state.lightDir[0] = std::cos(clampedElev) * std::cos(activeLightAzimuth);
		m_state.lightDir[1] = clampedElev;   // Y = up/down component
		m_state.lightDir[2] = std::cos(clampedElev) * std::sin(activeLightAzimuth);

		// Normalise.
		const float lenSq = m_state.lightDir[0] * m_state.lightDir[0]
		                  + m_state.lightDir[1] * m_state.lightDir[1]
		                  + m_state.lightDir[2] * m_state.lightDir[2];
		if (lenSq > 1e-6f)
		{
			const float invLen = 1.0f / std::sqrt(lenSq);
			m_state.lightDir[0] *= invLen;
			m_state.lightDir[1] *= invLen;
			m_state.lightDir[2] *= invLen;
		}
		else
		{
			// Fallback: straight up when degenerate.
			m_state.lightDir[0] = 0.0f;
			m_state.lightDir[1] = 1.0f;
			m_state.lightDir[2] = 0.0f;
		}

		// ---- Light colour and intensity ---------------------------------------
		// sunElevation in [−1, +1]; map to [0, 1] for day blend.
		const float dayT = Clamp(sunElevation, 0.0f, 1.0f);

		if (isSunUp)
		{
			// Sunrise/sunset transition zone: elevation in [0, 0.25] → warm orange.
			// Full daylight: elevation in [0.25, 1] → white.
			const float sunriseT = Clamp(sunElevation / 0.25f, 0.0f, 1.0f);
			LerpColor3(kLightColorSunrise, kLightColorNoon, sunriseT, m_state.lightColor);

			// Intensity: ramp from 0 at horizon to full 1.0 by elevation 0.1.
			const float intensityT = Clamp(sunElevation / 0.1f, 0.0f, 1.0f);
			const float intensity  = kIntensityNight + (kIntensityDay - kIntensityNight) * intensityT;
			m_state.lightColor[0] *= intensity;
			m_state.lightColor[1] *= intensity;
			m_state.lightColor[2] *= intensity;
		}
		else
		{
			// Nighttime: moonlight (dim, cold).
			m_state.lightColor[0] = kLightColorMoon[0] * kIntensityNight;
			m_state.lightColor[1] = kLightColorMoon[1] * kIntensityNight;
			m_state.lightColor[2] = kLightColorMoon[2] * kIntensityNight;
		}

		// ---- Ambient colour --------------------------------------------------
		// Ambient scales with sun elevation; minimum kAmbientNightMin to avoid pitch black.
		const float ambientScale = kAmbientNightMin + (1.0f - kAmbientNightMin) * dayT;
		m_state.ambientColor[0] = 0.04f * ambientScale + 0.02f * dayT;
		m_state.ambientColor[1] = 0.04f * ambientScale + 0.04f * dayT;
		m_state.ambientColor[2] = 0.08f * ambientScale + 0.06f * dayT;

		// ---- Sky gradient colours -------------------------------------------
		// Blend between night → dawn/dusk → noon based on sun elevation.
		if (isSunUp)
		{
			// Map elevation 0→0.2 as sunrise/sunset zone (dawn colours).
			// Map elevation 0.2→1.0 as daytime zone (noon colours).
			const float noonT = Clamp((sunElevation - 0.2f) / 0.8f, 0.0f, 1.0f);
			LerpColor3(kSkyZenithDawn,  kSkyZenithNoon,  noonT, m_state.skyZenith);
			LerpColor3(kSkyHorizonDawn, kSkyHorizonNoon, noonT, m_state.skyHorizon);
		}
		else
		{
			// Transition from night → dawn in the last hour before sunrise (t=5..6).
			const float preDawnT = Clamp(m_timeOfDay - 5.0f, 0.0f, 1.0f)
			                     * Clamp(1.0f - (m_timeOfDay - 18.0f), 0.0f, 1.0f);
			LerpColor3(kSkyZenithNight,  kSkyZenithDawn,  preDawnT, m_state.skyZenith);
			LerpColor3(kSkyHorizonNight, kSkyHorizonDawn, preDawnT, m_state.skyHorizon);
		}
	}

} // namespace engine::render
