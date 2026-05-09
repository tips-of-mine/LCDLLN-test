#pragma once

#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Enumeration of supported weather states (M38.2).
	enum class WeatherState : uint8_t
	{
		Clear = 0, ///< No precipitation, full visibility.
		Rain  = 1, ///< Vertical rain streaks, moderate fog.
		Snow  = 2, ///< Slow flakes with lateral drift, light fog.
		Fog   = 3, ///< Dense exponential fog, no precipitation.
		Storm = 4, ///< Heavy rain, strong wind drift, dense fog.
	};

	/// One CPU weather particle (rain drop or snow flake) (M38.2).
	struct WeatherParticle
	{
		float x  = 0.0f, y  = 0.0f, z  = 0.0f; ///< World-space position.
		float vx = 0.0f, vy = 0.0f, vz = 0.0f; ///< Velocity in m/s.
		float age      = 0.0f; ///< Elapsed lifetime in seconds.
		float lifetime = 0.0f; ///< Maximum lifetime in seconds.
		bool  active   = false;
	};

	/// Configuration for WeatherSystem::Init() (M38.2).
	struct WeatherConfig
	{
		/// Duration of a fade transition between states, in seconds (spec: 30 s).
		float transitionDuration = 30.0f;

		// ---- Rain (spec: 1000/sec spawn, 2 s lifetime, vertical, fast) ----
		float    rainSpawnRate     = 1000.0f; ///< Particles per second at full intensity.
		float    rainLifetime      = 2.0f;    ///< Seconds per rain drop.
		uint32_t rainMaxParticles  = 2048;    ///< Internal pool size.

		// ---- Snow (spec: 500/sec spawn, 5 s lifetime, slow, lateral drift) ----
		float    snowSpawnRate     = 500.0f;  ///< Particles per second at full intensity.
		float    snowLifetime      = 5.0f;    ///< Seconds per snow flake.
		uint32_t snowMaxParticles  = 1024;    ///< Internal pool size.

		// ---- Spatial parameters ----
		float spawnRadius = 40.0f; ///< Horizontal spawn radius around camera (m).
		float spawnHeight = 20.0f; ///< Height above camera at which particles spawn (m).

		// ---- Fog ----
		/// Maximum exponential fog density reached at full intensity
		/// for states Fog and Storm; Rain uses half this value.
		float fogDensityMax = 0.05f;
	};

	/// CPU-side weather state machine with rain/snow particle pools (M38.2).
	///
	/// Manages:
	///  - State machine (Clear / Rain / Snow / Fog / Storm) with 30-second linear fade.
	///  - Internal rain particle pool: 1000 particles/sec, 2 s lifetime, vertical fall.
	///  - Internal snow particle pool: 500 particles/sec, 5 s lifetime, lateral drift.
	///  - Exponential fog density output.
	///  - Audio volume output (rain intensity [0,1]) for driving audio buses.
	///
	/// Consumers:
	///  - Engine::Update() calls Tick() each frame.
	///  - Rendering layer reads GetRainParticles() / GetSnowParticles() for billboards.
	///  - Fog system reads GetFogDensity().
	///  - Audio system reads GetAudioVolume() to set the weather bus volume.
	class WeatherSystem final
	{
	public:
		WeatherSystem() = default;
		WeatherSystem(const WeatherSystem&) = delete;
		WeatherSystem& operator=(const WeatherSystem&) = delete;

		/// Initialise the system with the given configuration and allocate particle pools.
		/// @param config  Weather parameters (transition duration, spawn rates, etc.).
		void Init(const WeatherConfig& config = {});

		/// Release all particle pools and reset state to Clear.
		void Shutdown();

		/// Request a transition to \p target.
		/// If \p target differs from the current state the transition timer is reset
		/// and intensity is re-lerped from 0 → 1 over config.transitionDuration.
		void SetWeather(WeatherState target);

		/// Advance the simulation by \p deltaSeconds.
		///
		/// Updates the transition timer, intensity, and all active particle pools.
		/// \p cameraX/Y/Z is the current camera world position used as the particle
		/// spawn origin (particles appear around / above the camera).
		void Tick(float deltaSeconds, float cameraX, float cameraY, float cameraZ);

		// ---- State accessors ------------------------------------------------

		/// Currently displayed state (may still be fading in).
		WeatherState GetCurrentState() const { return m_current; }

		/// Requested target state (may equal current when transition is complete).
		WeatherState GetTargetState()  const { return m_target; }

		/// Transition intensity in [0, 1].
		/// 0 = just requested, 1 = fully in target state (transition complete).
		float GetIntensity() const { return m_intensity; }

		/// True while a transition is still in progress.
		bool IsTransitioning() const { return m_transitioning; }

		/// Exponential fog density for the active state (driven by intensity).
		/// Returns 0 for Clear and 0..fogDensityMax for Rain/Snow/Fog/Storm.
		float GetFogDensity() const;

		/// Rain audio volume [0, 1].
		/// Non-zero only for Rain and Storm states; 0 for Clear/Snow/Fog.
		float GetAudioVolume() const;

		// ---- Particle output ------------------------------------------------

		/// Read-only view of the active rain-drop particle pool.
		const std::vector<WeatherParticle>& GetRainParticles() const { return m_rain; }

		/// Read-only view of the active snow-flake particle pool.
		const std::vector<WeatherParticle>& GetSnowParticles() const { return m_snow; }

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Advance the rain particle pool for the current frame.
		void TickRain(float dt, float camX, float camY, float camZ, float spawnRate);

		/// Advance the snow particle pool for the current frame.
		void TickSnow(float dt, float camX, float camY, float camZ, float spawnRate);

		/// Return a pointer to a free particle slot (cycles through the pool).
		WeatherParticle* AllocateParticle(std::vector<WeatherParticle>& pool, uint32_t maxCount);

		/// Advance the LCG and return a pseudo-random float in [0, 1).
		float NextRandom();

		/// Pseudo-random float in [lo, hi).
		float RandomRange(float lo, float hi);

		/// Effective rain spawn rate for the active target state (0 if not raining).
		float GetRainSpawnRate() const;

		/// Effective snow spawn rate for the active target state (0 if not snowing).
		float GetSnowSpawnRate() const;

		WeatherConfig m_cfg{};

		WeatherState m_current      = WeatherState::Clear;
		WeatherState m_target       = WeatherState::Clear;
		float        m_intensity    = 0.0f;   ///< [0, 1] how far we are into the target state.
		float        m_transTimer   = 0.0f;   ///< Seconds elapsed in the current transition.
		bool         m_transitioning = false;

		float    m_spawnAccRain = 0.0f; ///< Fractional rain particle accumulator.
		float    m_spawnAccSnow = 0.0f; ///< Fractional snow particle accumulator.
		uint32_t m_rng          = 0x9E3779B9u; ///< LCG seed.

		std::vector<WeatherParticle> m_rain; ///< Rain drop pool.
		std::vector<WeatherParticle> m_snow; ///< Snow flake pool.

		bool m_initialized = false;
	};

} // namespace engine::render
