#include "engine/render/WeatherSystem.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// WeatherSystem::Init
	// -------------------------------------------------------------------------

	void WeatherSystem::Init(const WeatherConfig& config)
	{
		if (m_initialized)
		{
			Shutdown();
		}

		m_cfg          = config;
		m_current      = WeatherState::Clear;
		m_target       = WeatherState::Clear;
		m_intensity    = 0.0f;
		m_transTimer   = 0.0f;
		m_transitioning = false;
		m_spawnAccRain  = 0.0f;
		m_spawnAccSnow  = 0.0f;
		m_rng           = 0x9E3779B9u;

		// Pre-allocate particle pools to avoid mid-frame allocations.
		m_rain.reserve(config.rainMaxParticles);
		m_rain.assign(config.rainMaxParticles, WeatherParticle{});
		m_snow.reserve(config.snowMaxParticles);
		m_snow.assign(config.snowMaxParticles, WeatherParticle{});

		m_initialized = true;

		LOG_INFO(Render, "[WeatherSystem] Init OK (rainPool={} snowPool={} transitionDuration={:.1f}s)",
		         config.rainMaxParticles, config.snowMaxParticles, config.transitionDuration);
	}

	// -------------------------------------------------------------------------
	// WeatherSystem::Shutdown
	// -------------------------------------------------------------------------

	void WeatherSystem::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_rain.clear();
		m_snow.clear();
		m_initialized = false;

		LOG_INFO(Render, "[WeatherSystem] Shutdown complete");
	}

	// -------------------------------------------------------------------------
	// WeatherSystem::SetWeather
	// -------------------------------------------------------------------------

	void WeatherSystem::SetWeather(WeatherState target)
	{
		if (target == m_target)
		{
			return;
		}

		m_target       = target;
		m_transitioning = true;
		m_transTimer   = 0.0f;
		m_intensity    = 0.0f; // Always fade in from zero on state change.

		LOG_INFO(Render, "[WeatherSystem] Weather change requested → state={}",
		         static_cast<uint8_t>(target));
	}

	// -------------------------------------------------------------------------
	// WeatherSystem::Tick
	// -------------------------------------------------------------------------

	void WeatherSystem::Tick(float deltaSeconds, float cameraX, float cameraY, float cameraZ)
	{
		if (!m_initialized)
		{
			return;
		}

		// ---- Transition / intensity update ----------------------------------

		if (m_transitioning)
		{
			m_transTimer += deltaSeconds;
			const float progress = m_cfg.transitionDuration > 0.0f
			                     ? m_transTimer / m_cfg.transitionDuration
			                     : 1.0f;

			m_intensity = std::min(progress, 1.0f);

			if (m_intensity >= 1.0f)
			{
				m_current       = m_target;
				m_transitioning = false;
				m_transTimer    = 0.0f;
				m_intensity     = 1.0f;
				LOG_INFO(Render, "[WeatherSystem] Transition complete → state={}",
				         static_cast<uint8_t>(m_current));
			}
		}

		// ---- Particle simulation --------------------------------------------

		// Compute effective spawn rates scaled by current intensity.
		const float rainRate = m_intensity * GetRainSpawnRate();
		const float snowRate = m_intensity * GetSnowSpawnRate();

		if (rainRate > 0.0f)
		{
			TickRain(deltaSeconds, cameraX, cameraY, cameraZ, rainRate);
		}
		else
		{
			// Deactivate all rain particles when not raining.
			for (auto& p : m_rain)
			{
				p.active = false;
			}
		}

		if (snowRate > 0.0f)
		{
			TickSnow(deltaSeconds, cameraX, cameraY, cameraZ, snowRate);
		}
		else
		{
			// Deactivate all snow particles when not snowing.
			for (auto& p : m_snow)
			{
				p.active = false;
			}
		}
	}

	// -------------------------------------------------------------------------
	// WeatherSystem::GetFogDensity
	// -------------------------------------------------------------------------

	float WeatherSystem::GetFogDensity() const
	{
		if (!m_initialized)
		{
			return 0.0f;
		}

		switch (m_target)
		{
		case WeatherState::Rain:
			// Rain adds moderate fog (half maximum density).
			return m_cfg.fogDensityMax * 0.5f * m_intensity;

		case WeatherState::Fog:
			// Fog state uses full density.
			return m_cfg.fogDensityMax * m_intensity;

		case WeatherState::Storm:
			// Storm: full density fog + heavy rain.
			return m_cfg.fogDensityMax * m_intensity;

		case WeatherState::Snow:
			// Snow adds light haze.
			return m_cfg.fogDensityMax * 0.25f * m_intensity;

		case WeatherState::Clear:
		default:
			return 0.0f;
		}
	}

	// -------------------------------------------------------------------------
	// WeatherSystem::GetAudioVolume
	// -------------------------------------------------------------------------

	float WeatherSystem::GetAudioVolume() const
	{
		if (!m_initialized)
		{
			return 0.0f;
		}

		// Only rain and storm produce a rain sound (spec step 6).
		switch (m_target)
		{
		case WeatherState::Rain:
			return m_intensity;

		case WeatherState::Storm:
			// Storm: louder than regular rain.
			return std::min(m_intensity * 1.5f, 1.0f);

		default:
			return 0.0f;
		}
	}

	// -------------------------------------------------------------------------
	// WeatherSystem private helpers
	// -------------------------------------------------------------------------

	/// Effective rain spawn rate for the current target state.
	float WeatherSystem::GetRainSpawnRate() const
	{
		switch (m_target)
		{
		case WeatherState::Rain:   return m_cfg.rainSpawnRate;
		case WeatherState::Storm:  return m_cfg.rainSpawnRate * 2.0f; // heavier
		default:                   return 0.0f;
		}
	}

	/// Effective snow spawn rate for the current target state.
	float WeatherSystem::GetSnowSpawnRate() const
	{
		switch (m_target)
		{
		case WeatherState::Snow: return m_cfg.snowSpawnRate;
		default:                 return 0.0f;
		}
	}

	void WeatherSystem::TickRain(float dt, float camX, float camY, float camZ, float spawnRate)
	{
		// ---- Advance existing rain particles --------------------------------
		for (auto& p : m_rain)
		{
			if (!p.active)
			{
				continue;
			}

			p.x   += p.vx * dt;
			p.y   += p.vy * dt;
			p.z   += p.vz * dt;
			p.age += dt;

			if (p.age >= p.lifetime)
			{
				p.active = false;
			}
		}

		// ---- Spawn new rain particles ---------------------------------------
		m_spawnAccRain += spawnRate * dt;
		const auto toSpawn = static_cast<uint32_t>(m_spawnAccRain);
		m_spawnAccRain -= static_cast<float>(toSpawn);

		for (uint32_t i = 0; i < toSpawn; ++i)
		{
			WeatherParticle* slot = AllocateParticle(m_rain, m_cfg.rainMaxParticles);
			if (!slot)
			{
				break; // Pool full.
			}

			// Random position within spawn radius around camera, at spawn height.
			slot->x = camX + RandomRange(-m_cfg.spawnRadius, m_cfg.spawnRadius);
			slot->y = camY + m_cfg.spawnHeight;
			slot->z = camZ + RandomRange(-m_cfg.spawnRadius, m_cfg.spawnRadius);

			// Vertical fast fall (spec: vertical, fast).
			slot->vx = RandomRange(-0.5f, 0.5f); // slight horizontal drift
			slot->vy = RandomRange(-20.0f, -10.0f); // fast downward
			slot->vz = RandomRange(-0.5f, 0.5f);

			slot->age      = 0.0f;
			slot->lifetime = m_cfg.rainLifetime; // 2 s (spec)
			slot->active   = true;
		}
	}

	void WeatherSystem::TickSnow(float dt, float camX, float camY, float camZ, float spawnRate)
	{
		// ---- Advance existing snow particles --------------------------------
		for (auto& p : m_snow)
		{
			if (!p.active)
			{
				continue;
			}

			p.x   += p.vx * dt;
			p.y   += p.vy * dt;
			p.z   += p.vz * dt;
			p.age += dt;

			if (p.age >= p.lifetime)
			{
				p.active = false;
			}
		}

		// ---- Spawn new snow particles ----------------------------------------
		m_spawnAccSnow += spawnRate * dt;
		const auto toSpawn = static_cast<uint32_t>(m_spawnAccSnow);
		m_spawnAccSnow -= static_cast<float>(toSpawn);

		for (uint32_t i = 0; i < toSpawn; ++i)
		{
			WeatherParticle* slot = AllocateParticle(m_snow, m_cfg.snowMaxParticles);
			if (!slot)
			{
				break; // Pool full.
			}

			// Random position within spawn radius around camera, at spawn height.
			slot->x = camX + RandomRange(-m_cfg.spawnRadius, m_cfg.spawnRadius);
			slot->y = camY + m_cfg.spawnHeight;
			slot->z = camZ + RandomRange(-m_cfg.spawnRadius, m_cfg.spawnRadius);

			// Slow fall with lateral drift (spec: slow, lateral drift).
			slot->vx = RandomRange(-1.5f, 1.5f); // gentle lateral drift
			slot->vy = RandomRange(-2.0f, -1.0f);  // slow downward
			slot->vz = RandomRange(-1.5f, 1.5f);

			slot->age      = 0.0f;
			slot->lifetime = m_cfg.snowLifetime; // 5 s (spec)
			slot->active   = true;
		}
	}

	WeatherParticle* WeatherSystem::AllocateParticle(std::vector<WeatherParticle>& pool, uint32_t maxCount)
	{
		// Cycle through the pool to find an inactive slot.
		const uint32_t sz = static_cast<uint32_t>(pool.size());
		const uint32_t limit = std::min(sz, maxCount);
		for (uint32_t i = 0; i < limit; ++i)
		{
			if (!pool[i].active)
			{
				return &pool[i];
			}
		}
		return nullptr; // Pool full.
	}

	float WeatherSystem::NextRandom()
	{
		// Linear congruential generator (same pattern as ParticleSystem).
		m_rng = m_rng * 1664525u + 1013904223u;
		const uint32_t mantissa = (m_rng >> 8u) & 0x00FFFFFFu;
		return static_cast<float>(mantissa) / static_cast<float>(0x01000000u);
	}

	float WeatherSystem::RandomRange(float lo, float hi)
	{
		return lo + (hi - lo) * NextRandom();
	}

} // namespace engine::render
