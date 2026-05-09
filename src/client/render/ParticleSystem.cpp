#include "engine/render/ParticleSystem.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	namespace
	{
		bool HasIndexedKey(const engine::core::Config& cfg, const std::string& prefix, size_t index)
		{
			return cfg.Has(prefix + "[" + std::to_string(index) + "].id");
		}

		float NextUnitRandom(uint32_t& state)
		{
			state = state * 1664525u + 1013904223u;
			const uint32_t mantissa = (state >> 8u) & 0x00FFFFFFu;
			return static_cast<float>(mantissa) / static_cast<float>(0x01000000u);
		}

		float RandomRange(uint32_t& state, float minValue, float maxValue)
		{
			return minValue + (maxValue - minValue) * NextUnitRandom(state);
		}

		engine::math::Vec3 ReadVec3(const engine::core::Config& cfg, const std::string& prefix)
		{
			return engine::math::Vec3(
				static_cast<float>(cfg.GetDouble(prefix + "[0]", 0.0)),
				static_cast<float>(cfg.GetDouble(prefix + "[1]", 0.0)),
				static_cast<float>(cfg.GetDouble(prefix + "[2]", 0.0)));
		}
	}

	ParticleSystem::~ParticleSystem()
	{
		Shutdown();
	}

	bool ParticleSystem::Init(const engine::core::Config& config)
	{
		Shutdown();
		m_config = config;
		m_definitionsRelativePath = config.GetString("particles.definitions", "particles/explosion.json");
		if (!LoadDefinitions())
		{
			LOG_ERROR(Render, "[ParticleSystem] Init FAILED: unable to load '{}'", m_definitionsRelativePath);
			return false;
		}

		m_initialized = true;
		LOG_INFO(Render, "[ParticleSystem] Init OK (definitions='{}', emitters={})",
			m_definitionsRelativePath, static_cast<uint32_t>(m_definitions.size()));
		return true;
	}

	void ParticleSystem::Shutdown()
	{
		m_definitions.clear();
		m_emitters.clear();
		m_definitionsRelativePath.clear();
		m_initialized = false;
		LOG_INFO(Render, "[ParticleSystem] Shutdown complete");
	}

	bool ParticleSystem::SpawnEmitter(std::string_view emitterId)
	{
		if (!m_initialized)
		{
			LOG_WARN(Render, "[ParticleSystem] SpawnEmitter ignored: system not initialized");
			return false;
		}

		const auto it = m_definitions.find(std::string(emitterId));
		if (it == m_definitions.end())
		{
			LOG_WARN(Render, "[ParticleSystem] SpawnEmitter failed: unknown emitter '{}'", emitterId);
			return false;
		}

		ParticleEmitterRuntime runtime{};
		runtime.definition = it->second;
		runtime.particles.resize(runtime.definition.maxParticles);
		runtime.active = true;
		m_emitters.push_back(std::move(runtime));
		LOG_INFO(Render, "[ParticleSystem] Spawned emitter '{}' (max_particles={}, rate={:.2f})",
			emitterId, it->second.maxParticles, it->second.emissionRate);
		return true;
	}

	bool ParticleSystem::Tick(float deltaSeconds)
	{
		if (!m_initialized)
		{
			LOG_WARN(Render, "[ParticleSystem] Tick ignored: system not initialized");
			return false;
		}

		if (deltaSeconds <= 0.0f)
		{
			LOG_WARN(Render, "[ParticleSystem] Tick ignored: invalid dt={:.4f}", deltaSeconds);
			return false;
		}

		for (ParticleEmitterRuntime& emitter : m_emitters)
		{
			if (!emitter.active)
			{
				continue;
			}

			EmitParticles(emitter, deltaSeconds);
			UpdateParticles(emitter, deltaSeconds);
		}

		LOG_DEBUG(Render, "[ParticleSystem] Tick OK (emitters={}, dt={:.4f})",
			static_cast<uint32_t>(m_emitters.size()), deltaSeconds);
		return true;
	}

	void ParticleSystem::BuildBillboards(const engine::render::Camera& camera, std::vector<ParticleBillboard>& outBillboards) const
	{
		outBillboards.clear();
		if (!m_initialized)
		{
			LOG_WARN(Render, "[ParticleSystem] BuildBillboards ignored: system not initialized");
			return;
		}

		for (const ParticleEmitterRuntime& emitter : m_emitters)
		{
			if (!emitter.active)
			{
				continue;
			}

			for (const ParticleInstance& particle : emitter.particles)
			{
				if (!particle.active || particle.lifetimeSeconds <= 0.0f)
				{
					continue;
				}

				const float normalizedAge = std::clamp(particle.ageSeconds / particle.lifetimeSeconds, 0.0f, 1.0f);
				const float size = particle.startSize + (particle.endSize - particle.startSize) * normalizedAge;
				const engine::math::Vec3 toCamera = camera.position - particle.position;

				ParticleBillboard billboard{};
				billboard.center = particle.position;
				billboard.size = size;
				billboard.normalizedAge = normalizedAge;
				billboard.distanceToCameraSq = toCamera.LengthSq();
				outBillboards.push_back(billboard);
			}
		}

		std::sort(outBillboards.begin(), outBillboards.end(),
			[](const ParticleBillboard& a, const ParticleBillboard& b)
			{
				return a.distanceToCameraSq > b.distanceToCameraSq;
			});

		LOG_DEBUG(Render, "[ParticleSystem] Built {} billboards", static_cast<uint32_t>(outBillboards.size()));
	}

	bool ParticleSystem::LoadDefinitions()
	{
		m_definitions.clear();
		const std::string contentRoot = m_config.GetString("paths.content", "game/data");
		const std::string fullPath = contentRoot + "/" + m_definitionsRelativePath;

		engine::core::Config definitionsConfig;
		if (!definitionsConfig.LoadFromFile(fullPath))
		{
			LOG_ERROR(Render, "[ParticleSystem] Definition load FAILED: '{}'", fullPath);
			return false;
		}

		size_t index = 0;
		while (HasIndexedKey(definitionsConfig, "emitters", index))
		{
			const std::string prefix = "emitters[" + std::to_string(index) + "]";
			ParticleEmitterDefinition definition{};
			definition.id = definitionsConfig.GetString(prefix + ".id", "");
			definition.atlasTexturePath = definitionsConfig.GetString(prefix + ".atlasTexturePath", "");
			definition.maxParticles = static_cast<uint32_t>(definitionsConfig.GetInt(prefix + ".maxParticles", 0));
			definition.emissionRate = static_cast<float>(definitionsConfig.GetDouble(prefix + ".emissionRate", 0.0));
			definition.lifetimeSeconds = static_cast<float>(definitionsConfig.GetDouble(prefix + ".lifetimeSeconds", 0.0));
			definition.startSize = static_cast<float>(definitionsConfig.GetDouble(prefix + ".startSize", 0.0));
			definition.endSize = static_cast<float>(definitionsConfig.GetDouble(prefix + ".endSize", 0.0));
			definition.spawnPosition = ReadVec3(definitionsConfig, prefix + ".spawnPosition");
			definition.velocityMin = ReadVec3(definitionsConfig, prefix + ".velocityMin");
			definition.velocityMax = ReadVec3(definitionsConfig, prefix + ".velocityMax");

			if (definition.id.empty() || definition.maxParticles == 0u || definition.lifetimeSeconds <= 0.0f)
			{
				LOG_WARN(Render, "[ParticleSystem] Skipped invalid emitter definition at index {}", static_cast<uint32_t>(index));
				++index;
				continue;
			}

			m_definitions[definition.id] = definition;
			LOG_INFO(Render, "[ParticleSystem] Loaded emitter '{}' from '{}'", definition.id, m_definitionsRelativePath);
			++index;
		}

		if (m_definitions.empty())
		{
			LOG_WARN(Render, "[ParticleSystem] No valid emitter definitions in '{}'", fullPath);
		}

		return !m_definitions.empty();
	}

	void ParticleSystem::EmitParticles(ParticleEmitterRuntime& emitter, float deltaSeconds)
	{
		const ParticleEmitterDefinition& definition = emitter.definition;
		if (definition.emissionRate <= 0.0f)
		{
			return;
		}

		emitter.spawnAccumulator += definition.emissionRate * deltaSeconds;
		uint32_t spawnedCount = 0;
		while (emitter.spawnAccumulator >= 1.0f)
		{
			if (emitter.totalSpawned >= definition.maxParticles)
			{
				break;
			}

			ParticleInstance* particle = AllocateParticle(emitter);
			if (particle == nullptr)
			{
				LOG_WARN(Render, "[ParticleSystem] Emitter '{}' pool exhausted", definition.id);
				break;
			}

			particle->position = definition.spawnPosition;
			particle->velocity = engine::math::Vec3(
				RandomRange(m_rngState, definition.velocityMin.x, definition.velocityMax.x),
				RandomRange(m_rngState, definition.velocityMin.y, definition.velocityMax.y),
				RandomRange(m_rngState, definition.velocityMin.z, definition.velocityMax.z));
			particle->ageSeconds = 0.0f;
			particle->lifetimeSeconds = definition.lifetimeSeconds;
			particle->startSize = definition.startSize;
			particle->endSize = definition.endSize;
			particle->active = true;

			emitter.spawnAccumulator -= 1.0f;
			++emitter.totalSpawned;
			++spawnedCount;
		}

		if (spawnedCount > 0u)
		{
			LOG_DEBUG(Render, "[ParticleSystem] Emitter '{}' spawned {}", definition.id, spawnedCount);
		}
	}

	void ParticleSystem::UpdateParticles(ParticleEmitterRuntime& emitter, float deltaSeconds)
	{
		uint32_t activeCount = 0;
		for (ParticleInstance& particle : emitter.particles)
		{
			if (!particle.active)
			{
				continue;
			}

			particle.ageSeconds += deltaSeconds;
			if (particle.ageSeconds >= particle.lifetimeSeconds)
			{
				particle.active = false;
				continue;
			}

			particle.position += particle.velocity * deltaSeconds;
			++activeCount;
		}

		if (activeCount == 0u && emitter.totalSpawned >= emitter.definition.maxParticles)
		{
			emitter.active = false;
			LOG_INFO(Render, "[ParticleSystem] Emitter '{}' completed", emitter.definition.id);
		}
	}

	ParticleInstance* ParticleSystem::AllocateParticle(ParticleEmitterRuntime& emitter)
	{
		for (ParticleInstance& particle : emitter.particles)
		{
			if (!particle.active)
			{
				return &particle;
			}
		}

		return nullptr;
	}
}
