#pragma once

#include "engine/render/ParticleSystem.h"

#include <cstdint>
#include <string>

namespace engine::render
{
	/// Weather state identifiers (M38.2).
	enum class WeatherState : uint8_t
	{
		Clear = 0,  ///< No precipitation, no fog.
		Rain  = 1,  ///< Vertical rain streaks, slight fog.
		Snow  = 2,  ///< Slow drifting snowflakes.
		Fog   = 3,  ///< Dense ground fog, no precipitation.
		Storm = 4   ///< Heavy rain + wind + strong fog.
	};

	/// Per-frame weather snapshot produced by the system (M38.2).
	/// The caller (Engine) uses this to:
	///   - Enable/disable precipitation particle emitters.
	///   - Set fog density in the render pipeline.
	///   - Adjust ambient audio volume.
	struct WeatherSnapshot
	{
		WeatherState currentState  = WeatherState::Clear;
		WeatherState previousState = WeatherState::Clear;

		/// Transition progress from previousState to currentState [0, 1].
		/// 0 = fully previousState, 1 = fully currentState.
		float intensity = 1.0f;

		/// Whether rain particles should be active this frame.
		bool rainActive = false;
		/// Whether snow particles should be active this frame.
		bool snowActive = false;

		/// Exponential fog density coefficient.  0 = no fog.
		float fogDensity = 0.0f;
		/// Fog colour (RGB).
		float fogColor[3] = { 0.80f, 0.82f, 0.85f };

		/// Remaining seconds in the current state (before next state change).
		float remainingStateSeconds = 0.0f;

		/// Effective rain emission rate for the current frame (particles/sec).
		float rainEmissionRate = 0.0f;
		/// Effective snow emission rate for the current frame (particles/sec).
		float snowEmissionRate = 0.0f;
	};

	/// CPU-side weather state machine: manages state, transitions, and
	/// produces per-frame snapshots for rain/snow/fog parameters (M38.2).
	///
	/// Usage:
	///   - Call Init() once at startup.
	///   - Call Update(deltaSeconds) every frame.
	///   - Call SetWeather(state) to force a specific weather (e.g. /weather rain).
	///   - Read GetSnapshot() to drive particles, fog, and audio.
	class WeatherSystem final
	{
	public:
		WeatherSystem()                            = default;
		WeatherSystem(const WeatherSystem&)        = delete;
		WeatherSystem& operator=(const WeatherSystem&) = delete;

		/// Initialise the system with optional starting state and duration range.
		/// \param initialState  Starting weather state.
		/// \param minStateSec   Minimum duration per state in seconds (5 min default).
		/// \param maxStateSec   Maximum duration per state in seconds (20 min default).
		/// \param transitionSec Fade-in/fade-out duration in seconds (30s spec).
		void Init(WeatherState initialState = WeatherState::Clear,
		          float minStateSec   = 300.0f,
		          float maxStateSec   = 1200.0f,
		          float transitionSec = 30.0f);

		/// Advance the weather simulation by \p deltaSeconds real seconds.
		void Update(float deltaSeconds);

		/// Immediately begin transitioning to \p state (e.g. for a /weather command).
		void SetWeather(WeatherState state);

		/// Current per-frame snapshot (valid after Init and each Update call).
		const WeatherSnapshot& GetSnapshot() const { return m_snapshot; }

		/// Return the pre-configured rain ParticleEmitterDefinition (M38.2).
		/// spawn rate 1000/sec, lifetime 2s, vertical fast movement.
		static const ParticleEmitterDefinition& GetRainEmitterDefinition();

		/// Return the pre-configured snow ParticleEmitterDefinition (M38.2).
		/// spawn rate 500/sec, lifetime 5s, slow lateral drift.
		static const ParticleEmitterDefinition& GetSnowEmitterDefinition();

		/// Human-readable name for a WeatherState (for logging).
		static const char* StateName(WeatherState state);

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Compute and fill m_snapshot from current state.
		void ComputeSnapshot();

		/// Advance the state timer and trigger a transition when expired.
		void AdvanceTimer(float deltaSeconds);

		/// Start a transition to a new state.
		void BeginTransition(WeatherState nextState);

		/// Simple LCG pseudo-random in [0, 1).
		float NextRandom();

		/// Pick a random next state (different from current).
		WeatherState PickNextState();

		// ── Configuration ────────────────────────────────────────────────────
		float m_minStateSec   = 300.0f;
		float m_maxStateSec   = 1200.0f;
		float m_transitionSec = 30.0f;

		// ── Runtime state ────────────────────────────────────────────────────
		WeatherState m_currentState  = WeatherState::Clear;
		WeatherState m_targetState   = WeatherState::Clear;
		bool         m_inTransition  = false;

		/// Seconds elapsed in current state (resets on state change).
		float m_stateTimer       = 0.0f;
		/// Total seconds this state will last before transitioning.
		float m_stateDuration    = 600.0f;
		/// Seconds elapsed in current transition (resets on each transition start).
		float m_transitionTimer  = 0.0f;

		WeatherSnapshot m_snapshot{};

		uint32_t m_rng    = 0xDEADBEEFu;
		bool     m_initialized = false;
	};

} // namespace engine::render
