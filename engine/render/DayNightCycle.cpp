#include "engine/render/DayNightCycle.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	namespace
	{
		static constexpr float kPi        = 3.14159265f;
		static constexpr float kTwoPi     = 6.28318530f;
		static constexpr float kInvSqrt3  = 0.57735027f;

		/// Clamp a value to [lo, hi].
		static float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		/// Normalise a 3-float array in-place.  No-op when near-zero.
		static void Normalise(float* v)
		{
			const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
			if (len2 < 1e-9f) return;
			const float inv = 1.0f / std::sqrt(len2);
			v[0] *= inv; v[1] *= inv; v[2] *= inv;
		}
	} // anonymous namespace

	// ── DayNightCycle ─────────────────────────────────────────────────────────

	float DayNightCycle::Smoothstep(float t)
	{
		const float c = Clamp(t, 0.0f, 1.0f);
		return c * c * (3.0f - 2.0f * c);
	}

	void DayNightCycle::Init(float initialTimeHours, float timeScale)
	{
		m_timeScale             = (timeScale > 0.0f) ? timeScale : 3600.0f;
		m_state.timeHours       = std::fmod(initialTimeHours, 24.0f);
		if (m_state.timeHours < 0.0f) m_state.timeHours += 24.0f;

		ComputeSkyState();
		m_initialized = true;

		LOG_INFO(Render, "[DayNightCycle] Init OK (startTime={:.2f}h, timeScale={:.0f})",
			m_state.timeHours, m_timeScale);
	}

	void DayNightCycle::Update(double deltaSeconds)
	{
		if (!m_initialized) return;

		// Advance in-game time.  timeScale=1 is realtime (1 real second = 1 game
		// second).  timeScale=3600 means 1 real second = 1 game hour.
		m_state.timeHours += static_cast<float>(deltaSeconds) * m_timeScale / 3600.0f;
		m_state.timeHours  = std::fmod(m_state.timeHours, 24.0f);
		if (m_state.timeHours < 0.0f) m_state.timeHours += 24.0f;

		ComputeSkyState();
	}

	void DayNightCycle::SetTime(float timeHours)
	{
		m_state.timeHours = std::fmod(timeHours, 24.0f);
		if (m_state.timeHours < 0.0f) m_state.timeHours += 24.0f;
		ComputeSkyState();
		LOG_INFO(Render, "[DayNightCycle] Time set to {:.2f}h", m_state.timeHours);
	}

	engine::world::AtmosphereSettings DayNightCycle::ToAtmosphereSettings() const
	{
		engine::world::AtmosphereSettings atm{};

		// Sun or moon provides the directional light depending on elevation.
		const bool isSunUp = m_state.sunElevation >= 0.0f;
		const float* activeDir = isSunUp ? m_state.sunDirection : m_state.moonDirection;
		atm.sunDirection[0] = activeDir[0];
		atm.sunDirection[1] = activeDir[1];
		atm.sunDirection[2] = activeDir[2];

		atm.sunColor[0] = m_state.lightColor[0] * m_state.lightIntensity;
		atm.sunColor[1] = m_state.lightColor[1] * m_state.lightIntensity;
		atm.sunColor[2] = m_state.lightColor[2] * m_state.lightIntensity;

		atm.ambientColor[0] = m_state.ambientColor[0];
		atm.ambientColor[1] = m_state.ambientColor[1];
		atm.ambientColor[2] = m_state.ambientColor[2];

		return atm;
	}

	// ── Sky computation ───────────────────────────────────────────────────────

	void DayNightCycle::ComputeSkyState()
	{
		const float t = m_state.timeHours;

		// ── Sun elevation ─────────────────────────────────────────────────────
		// Formula from spec: elevation = sin(time * π / 12 − π / 2)
		// Results: -1 at midnight(t=0), 0 at dawn(t=6), +1 at noon(t=12), 0 at dusk(t=18).
		const float sunAngle    = t * kPi / 12.0f - kPi / 2.0f;
		m_state.sunElevation    = std::sin(sunAngle);
		const float cosElev     = std::cos(sunAngle);

		// ── Sun direction (toward sun, Y-up world space) ──────────────────────
		// Sun travels East (+X) to West (-X) via zenith (+Y).
		// X = cos(angle) gives east at dawn, -east at dusk.
		// Y = elevation.
		// Z = slight southern inclination for realism (fixed constant).
		m_state.sunDirection[0] = cosElev;
		m_state.sunDirection[1] = m_state.sunElevation;
		m_state.sunDirection[2] = 0.0f;
		Normalise(m_state.sunDirection);

		// ── Moon direction (opposite the sun) ─────────────────────────────────
		m_state.moonDirection[0] = -m_state.sunDirection[0];
		m_state.moonDirection[1] = -m_state.sunDirection[1];
		m_state.moonDirection[2] = -m_state.sunDirection[2];

		// ── Time-of-day key colours ───────────────────────────────────────────
		// Night [0, 5) and (19, 24]: dark blue
		// Dawn [5, 8]: orange/warm blend
		// Day [8, 16]: blue sky
		// Dusk [16, 19]: red/orange blend

		// Normalised phase values.
		const float tDawn    = Clamp((t - 5.0f)  / 3.0f, 0.0f, 1.0f); // 0..1 over [5,8]
		const float tDusk    = Clamp((t - 16.0f) / 3.0f, 0.0f, 1.0f); // 0..1 over [16,19]
		const float tNightEnd= Clamp((t - 19.0f) / 2.0f, 0.0f, 1.0f); // 0..1 over [19,21]
		const float isDay    = Clamp((t - 8.0f)  / 2.0f, 0.0f, 1.0f) *
		                       Clamp((16.0f - t) / 2.0f, 0.0f, 1.0f);  // 1 in [10,14]

		// Key colours
		// Night sky
		static constexpr float kNightZenith[3]  = { 0.01f, 0.02f, 0.08f };
		static constexpr float kNightHoriz[3]   = { 0.02f, 0.04f, 0.12f };
		// Dawn sky
		static constexpr float kDawnZenith[3]   = { 0.20f, 0.25f, 0.70f };
		static constexpr float kDawnHoriz[3]    = { 0.90f, 0.55f, 0.15f };
		// Day sky
		static constexpr float kDayZenith[3]    = { 0.10f, 0.30f, 0.80f };
		static constexpr float kDayHoriz[3]     = { 0.55f, 0.78f, 0.95f };
		// Dusk sky
		static constexpr float kDuskZenith[3]   = { 0.18f, 0.18f, 0.55f };
		static constexpr float kDuskHoriz[3]    = { 0.95f, 0.35f, 0.10f };

		// Blend zenith colour
		float zenith[3];
		float horiz[3];
		for (int i = 0; i < 3; ++i)
		{
			// Start at night, blend dawn, blend day, blend dusk, blend back to night.
			float z = kNightZenith[i];
			z = Lerp(z, kDawnZenith[i], Smoothstep(tDawn));
			z = Lerp(z, kDayZenith[i],  Smoothstep(isDay));
			z = Lerp(z, kDuskZenith[i], Smoothstep(tDusk));
			z = Lerp(z, kNightZenith[i], Smoothstep(tNightEnd));
			zenith[i] = z;

			float h = kNightHoriz[i];
			h = Lerp(h, kDawnHoriz[i], Smoothstep(tDawn));
			h = Lerp(h, kDayHoriz[i],  Smoothstep(isDay));
			h = Lerp(h, kDuskHoriz[i], Smoothstep(tDusk));
			h = Lerp(h, kNightHoriz[i], Smoothstep(tNightEnd));
			horiz[i] = h;
		}
		m_state.skyZenith[0]  = zenith[0]; m_state.skyZenith[1]  = zenith[1]; m_state.skyZenith[2]  = zenith[2];
		m_state.skyHorizon[0] = horiz[0];  m_state.skyHorizon[1] = horiz[1];  m_state.skyHorizon[2] = horiz[2];

		// ── Light colour: white at noon, orange at dawn/dusk, pale-blue at night ─
		static constexpr float kNoonColor[3]  = { 1.00f, 0.98f, 0.90f };
		static constexpr float kDawnColor[3]  = { 1.00f, 0.65f, 0.25f };
		static constexpr float kDuskColor[3]  = { 1.00f, 0.50f, 0.20f };
		static constexpr float kMoonColor[3]  = { 0.50f, 0.55f, 0.80f };

		// Derive a 0..1 "daytime" weight from elevation.
		const float dayWeight  = Smoothstep(Clamp(m_state.sunElevation * 3.0f, 0.0f, 1.0f));
		const float dawnWeight = Smoothstep(Clamp(1.0f - std::abs(t - 6.0f)  / 2.5f, 0.0f, 1.0f));
		const float duskWeight = Smoothstep(Clamp(1.0f - std::abs(t - 18.0f) / 2.5f, 0.0f, 1.0f));
		const float nightWeight= 1.0f - Clamp(m_state.sunElevation + 0.1f, 0.0f, 1.0f);

		float lc[3];
		for (int i = 0; i < 3; ++i)
		{
			lc[i] = Lerp(kMoonColor[i], kNoonColor[i], dayWeight);
			lc[i] = Lerp(lc[i], kDawnColor[i], dawnWeight * 0.8f);
			lc[i] = Lerp(lc[i], kDuskColor[i], duskWeight * 0.8f);
			(void)nightWeight; // moonColor already from base
		}
		m_state.lightColor[0] = lc[0];
		m_state.lightColor[1] = lc[1];
		m_state.lightColor[2] = lc[2];

		// ── Light intensity: 1.0 day, 0.1 night (moonlight) ──────────────────
		// Clamp so the night floor is never pitch black.
		m_state.lightIntensity = Clamp(
			Lerp(0.10f, 1.0f, Smoothstep(Clamp(m_state.sunElevation + 0.15f, 0.0f, 1.0f))),
			0.10f, 1.0f);

		// ── Ambient colour: scales with sun elevation ─────────────────────────
		// Minimum at night is small so the scene isn't pitch black.
		const float ambScale = Lerp(0.02f, 0.20f,
			Smoothstep(Clamp(m_state.sunElevation + 0.2f, 0.0f, 1.0f)));
		m_state.ambientColor[0] = zenith[0] * ambScale;
		m_state.ambientColor[1] = zenith[1] * ambScale;
		m_state.ambientColor[2] = zenith[2] * ambScale;
	}

} // namespace engine::render
