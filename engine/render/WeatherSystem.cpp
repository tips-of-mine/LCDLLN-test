#include "engine/render/WeatherSystem.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	// ── Static emitter definitions ─────────────────────────────────────────

	const ParticleEmitterDefinition& WeatherSystem::GetRainEmitterDefinition()
	{
		static const ParticleEmitterDefinition kRain = []
		{
			ParticleEmitterDefinition d{};
			d.id              = "weather_rain";
			d.atlasTexturePath = "textures/particles/rain_streak.texr";
			d.maxParticles    = 3000u;         ///< 1000/sec × 2s lifetime + margin
			d.emissionRate    = 1000.0f;       ///< spec: 1000 particles/sec
			d.lifetimeSeconds = 2.0f;          ///< spec: lifetime 2s
			d.startSize       = 0.04f;         ///< thin vertical streak
			d.endSize         = 0.02f;
			// Spawn around the camera (caller overrides spawnPosition each frame).
			d.spawnPosition   = { 0.0f, 8.0f, 0.0f };
			// Vertical, fast — rain falls straight down with slight variation.
			d.velocityMin     = { -0.5f, -18.0f, -0.5f };
			d.velocityMax     = {  0.5f, -14.0f,  0.5f };
			return d;
		}();
		return kRain;
	}

	const ParticleEmitterDefinition& WeatherSystem::GetSnowEmitterDefinition()
	{
		static const ParticleEmitterDefinition kSnow = []
		{
			ParticleEmitterDefinition d{};
			d.id              = "weather_snow";
			d.atlasTexturePath = "textures/particles/snow_flake.texr";
			d.maxParticles    = 3000u;         ///< 500/sec × 5s lifetime + margin
			d.emissionRate    = 500.0f;        ///< spec: 500 particles/sec
			d.lifetimeSeconds = 5.0f;          ///< spec: lifetime 5s
			d.startSize       = 0.12f;         ///< soft snowflake
			d.endSize         = 0.06f;
			d.spawnPosition   = { 0.0f, 8.0f, 0.0f };
			// Slow fall + lateral drift — spec: slow, lateral drift.
			d.velocityMin     = { -1.5f, -2.0f, -1.5f };
			d.velocityMax     = {  1.5f, -0.5f,  1.5f };
			return d;
		}();
		return kSnow;
	}

	const char* WeatherSystem::StateName(WeatherState state)
	{
		switch (state)
		{
		case WeatherState::Clear: return "Clear";
		case WeatherState::Rain:  return "Rain";
		case WeatherState::Snow:  return "Snow";
		case WeatherState::Fog:   return "Fog";
		case WeatherState::Storm: return "Storm";
		}
		return "Unknown";
	}

	// ── WeatherSystem ──────────────────────────────────────────────────────

	float WeatherSystem::NextRandom()
	{
		// LCG: fast pseudo-random in [0, 1)
		m_rng = m_rng * 1664525u + 1013904223u;
		return static_cast<float>(m_rng >> 8u) / static_cast<float>(1u << 24u);
	}

	WeatherState WeatherSystem::PickNextState()
	{
		// Weighted table: Clear 40%, Rain 25%, Snow 15%, Fog 15%, Storm 5%
		static constexpr float kWeights[] = { 0.40f, 0.25f, 0.15f, 0.15f, 0.05f };
		float roll = NextRandom();
		float cumulative = 0.0f;
		for (uint8_t i = 0; i < 5; ++i)
		{
			cumulative += kWeights[i];
			if (roll < cumulative)
			{
				const auto candidate = static_cast<WeatherState>(i);
				// Avoid picking the same state twice in a row.
				if (candidate != m_currentState)
					return candidate;
			}
		}
		// Fallback: Clear if we somehow land on the same state every pass.
		return (m_currentState == WeatherState::Clear)
		    ? WeatherState::Rain : WeatherState::Clear;
	}

	void WeatherSystem::Init(WeatherState initialState,
	                         float minStateSec,
	                         float maxStateSec,
	                         float transitionSec)
	{
		m_minStateSec   = (minStateSec   > 0.0f) ? minStateSec   : 300.0f;
		m_maxStateSec   = (maxStateSec   > 0.0f) ? maxStateSec   : 1200.0f;
		m_transitionSec = (transitionSec > 0.0f) ? transitionSec : 30.0f;

		m_currentState  = initialState;
		m_targetState   = initialState;
		m_inTransition  = false;
		m_stateTimer    = 0.0f;
		m_transitionTimer = 0.0f;

		// Pick a random initial state duration.
		m_stateDuration = m_minStateSec +
		    NextRandom() * (m_maxStateSec - m_minStateSec);

		ComputeSnapshot();
		m_initialized = true;

		LOG_INFO(Render, "[WeatherSystem] Init OK (state={}, duration={:.0f}s, transition={:.0f}s)",
		    StateName(m_currentState), m_stateDuration, m_transitionSec);
	}

	void WeatherSystem::Update(float deltaSeconds)
	{
		if (!m_initialized) return;

		AdvanceTimer(deltaSeconds);
		ComputeSnapshot();
	}

	void WeatherSystem::SetWeather(WeatherState state)
	{
		if (!m_initialized) return;
		if (state == m_currentState && !m_inTransition) return;

		LOG_INFO(Render, "[WeatherSystem] SetWeather: {} -> {}",
		    StateName(m_currentState), StateName(state));
		BeginTransition(state);
		ComputeSnapshot();
	}

	void WeatherSystem::AdvanceTimer(float deltaSeconds)
	{
		if (m_inTransition)
		{
			m_transitionTimer += deltaSeconds;
			if (m_transitionTimer >= m_transitionSec)
			{
				// Transition complete — commit to the new state.
				m_currentState    = m_targetState;
				m_inTransition    = false;
				m_transitionTimer = 0.0f;
				m_stateTimer      = 0.0f;
				m_stateDuration   = m_minStateSec +
				    NextRandom() * (m_maxStateSec - m_minStateSec);

				LOG_INFO(Render, "[WeatherSystem] Transitioned to {} (next duration={:.0f}s)",
				    StateName(m_currentState), m_stateDuration);
			}
		}
		else
		{
			m_stateTimer += deltaSeconds;
			if (m_stateTimer >= m_stateDuration)
			{
				// Current state expired — begin transition to a new one.
				BeginTransition(PickNextState());
			}
		}
	}

	void WeatherSystem::BeginTransition(WeatherState nextState)
	{
		m_targetState     = nextState;
		m_inTransition    = true;
		m_transitionTimer = 0.0f;
		LOG_DEBUG(Render, "[WeatherSystem] Transition started: {} -> {}",
		    StateName(m_currentState), StateName(m_targetState));
	}

	void WeatherSystem::ComputeSnapshot()
	{
		m_snapshot.previousState = m_currentState;
		m_snapshot.currentState  = m_inTransition ? m_targetState : m_currentState;

		// intensity: 0 at transition start, 1 at completion (or 1 if stable).
		m_snapshot.intensity = m_inTransition
		    ? std::min(m_transitionTimer / m_transitionSec, 1.0f)
		    : 1.0f;

		// Remaining time in state (approximate).
		m_snapshot.remainingStateSeconds = m_inTransition
		    ? (m_transitionSec - m_transitionTimer)
		    : (m_stateDuration  - m_stateTimer);

		// Determine which effects are active.
		// During transition, blend between previous and target.
		const WeatherState fromState = m_currentState;
		const WeatherState toState   = m_snapshot.currentState;
		const float t                = m_snapshot.intensity;

		// Helper: is a state "rainy"?
		auto isRainy = [](WeatherState s) {
			return s == WeatherState::Rain || s == WeatherState::Storm;
		};
		auto isSnowy = [](WeatherState s) {
			return s == WeatherState::Snow;
		};

		// Rain and snow active flags — enabled as soon as target state needs them.
		const float rainFrom  = isRainy(fromState) ? 1.0f : 0.0f;
		const float rainTo    = isRainy(toState)   ? 1.0f : 0.0f;
		const float rainBlend = rainFrom + (rainTo - rainFrom) * t;
		m_snapshot.rainActive       = (rainBlend > 0.05f);
		m_snapshot.rainEmissionRate = rainBlend * GetRainEmitterDefinition().emissionRate
		    * (toState == WeatherState::Storm ? 2.5f : 1.0f);  ///< Storm = 2.5× intensity.

		const float snowFrom  = isSnowy(fromState) ? 1.0f : 0.0f;
		const float snowTo    = isSnowy(toState)   ? 1.0f : 0.0f;
		const float snowBlend = snowFrom + (snowTo - snowFrom) * t;
		m_snapshot.snowActive       = (snowBlend > 0.05f);
		m_snapshot.snowEmissionRate = snowBlend * GetSnowEmitterDefinition().emissionRate;

		// Fog density for each state (at full intensity).
		auto fogDensityForState = [](WeatherState s) -> float
		{
			switch (s)
			{
			case WeatherState::Clear: return 0.000f;
			case WeatherState::Rain:  return 0.008f;
			case WeatherState::Snow:  return 0.010f;
			case WeatherState::Fog:   return 0.040f;
			case WeatherState::Storm: return 0.020f;
			}
			return 0.0f;
		};
		const float fogFrom = fogDensityForState(fromState);
		const float fogTo   = fogDensityForState(toState);
		m_snapshot.fogDensity = fogFrom + (fogTo - fogFrom) * t;

		// Fog colour: clear=warm grey, rain=cold grey, snow=white, fog=light grey, storm=dark.
		static constexpr float kFogColors[][3] = {
			{ 0.80f, 0.82f, 0.85f }, // Clear
			{ 0.55f, 0.58f, 0.65f }, // Rain
			{ 0.90f, 0.92f, 0.95f }, // Snow
			{ 0.75f, 0.77f, 0.80f }, // Fog
			{ 0.30f, 0.32f, 0.36f }, // Storm
		};
		const auto fi = static_cast<uint8_t>(fromState);
		const auto ti = static_cast<uint8_t>(toState);
		for (int c = 0; c < 3; ++c)
			m_snapshot.fogColor[c] = kFogColors[fi][c] + (kFogColors[ti][c] - kFogColors[fi][c]) * t;
	}

} // namespace engine::render
