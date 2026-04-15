#pragma once

#include "engine/world/ProbeData.h"

#include <cstdint>

namespace engine::render
{
	/// Day-night cycle: time of day (0–24 h), sun/moon positions, sky gradient
	/// colours, and directional light parameters (M38.1).
	///
	/// Usage:
	///   - Call Init() once at startup.
	///   - Call Update(deltaSeconds) every frame.
	///   - Call SetTime() to jump to a specific time (/time set command).
	///   - Read GetSkyState() and convert via ToAtmosphereSettings() to drive
	///     the existing LightingPass::LightParams (sunDirection, sunColor,
	///     ambientColor).
	class DayNightCycle final
	{
	public:
		/// Per-frame sky snapshot produced by the cycle (M38.1).
		struct SkyState
		{
			float timeHours         = 6.0f;  ///< Current in-game time [0, 24).

			/// Sun elevation sine: +1 = zenith (noon), 0 = horizon, -1 = nadir.
			/// Formula: sin(timeHours * π / 12 − π / 2).
			float sunElevation      = 0.0f;

			/// Normalised world-space direction *toward* the sun (Y-up coords).
			float sunDirection[3]   = { 1.0f, 0.0f, 0.0f };

			/// Normalised world-space direction toward the moon (opposite sun).
			float moonDirection[3]  = { -1.0f, 0.0f, 0.0f };

			/// Directional light colour (RGB). White at noon, orange at dawn/dusk,
			/// pale-blue at night (moonlight).
			float lightColor[3]     = { 1.0f, 0.95f, 0.85f };

			/// Directional light intensity scalar [0.1, 1.0].
			float lightIntensity    = 1.0f;

			/// Ambient sky colour reflected onto the scene.
			float ambientColor[3]   = { 0.03f, 0.03f, 0.05f };

			/// Sky zenith colour (top of sky dome).
			float skyZenith[3]      = { 0.10f, 0.30f, 0.80f };

			/// Sky horizon colour (bottom of sky dome).
			float skyHorizon[3]     = { 0.70f, 0.80f, 0.90f };
		};

		DayNightCycle()                              = default;
		DayNightCycle(const DayNightCycle&)          = delete;
		DayNightCycle& operator=(const DayNightCycle&) = delete;

		/// Initialise the cycle.
		/// \param initialTimeHours  Starting in-game hour [0, 24). Default 6 = dawn.
		/// \param timeScale         Speed multiplier:
		///   - 1 = realtime (1 real second = 1 game second),
		///   - 3600 = fast (1 real second = 1 game hour, 24 real seconds = 1 day).
		void Init(float initialTimeHours = 6.0f, float timeScale = 3600.0f);

		/// Advance the cycle by \p deltaSeconds real-world seconds.
		void Update(double deltaSeconds);

		/// Teleport to a specific in-game time (e.g. for /time set 12).
		/// \p timeHours is clamped / wrapped to [0, 24).
		void SetTime(float timeHours);

		/// Current in-game time in hours [0, 24).
		float GetTimeHours() const { return m_state.timeHours; }

		/// Current sky state (computed after each Update/SetTime call).
		const SkyState& GetSkyState() const { return m_state; }

		/// Convenience: produce an AtmosphereSettings suitable for the existing
		/// LightingPass / m_zoneAtmosphere field in Engine.
		/// lightColor is premultiplied by lightIntensity.
		engine::world::AtmosphereSettings ToAtmosphereSettings() const;

		/// True after a successful Init() call.
		bool IsInitialized() const { return m_initialized; }

	private:
		/// Recompute m_state from the current m_state.timeHours.
		void ComputeSkyState();

		/// Lerp a, b by t.
		static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

		/// Smoothstep clamp of t to [0, 1].
		static float Smoothstep(float t);

		SkyState m_state{};
		float    m_timeScale   = 3600.0f;
		bool     m_initialized = false;
	};

} // namespace engine::render
