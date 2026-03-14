#pragma once

#include "engine/core/Config.h"
#include "engine/math/Math.h"
#include "engine/render/Camera.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// One CPU particle emitter definition loaded from content JSON.
	struct ParticleEmitterDefinition
	{
		std::string id;
		std::string atlasTexturePath;
		uint32_t maxParticles = 0;
		float emissionRate = 0.0f;
		float lifetimeSeconds = 0.0f;
		float startSize = 0.0f;
		float endSize = 0.0f;
		engine::math::Vec3 spawnPosition{};
		engine::math::Vec3 velocityMin{};
		engine::math::Vec3 velocityMax{};
	};

	/// One CPU-simulated particle instance.
	struct ParticleInstance
	{
		engine::math::Vec3 position{};
		engine::math::Vec3 velocity{};
		float ageSeconds = 0.0f;
		float lifetimeSeconds = 0.0f;
		float startSize = 0.0f;
		float endSize = 0.0f;
		bool active = false;
	};

	/// One active emitter runtime holding a particle pool and spawn accumulator.
	struct ParticleEmitterRuntime
	{
		ParticleEmitterDefinition definition{};
		std::vector<ParticleInstance> particles;
		float spawnAccumulator = 0.0f;
		uint32_t totalSpawned = 0;
		bool active = false;
	};

	/// One camera-facing billboard generated from the CPU particle simulation.
	struct ParticleBillboard
	{
		engine::math::Vec3 center{};
		float size = 0.0f;
		float normalizedAge = 0.0f;
		float distanceToCameraSq = 0.0f;
	};

	/// CPU particle simulation runtime using pooled particles per emitter.
	class ParticleSystem final
	{
	public:
		/// Construct an uninitialized particle system.
		ParticleSystem() = default;

		/// Release emitter definitions and active particle pools.
		~ParticleSystem();

		/// Initialize the system and load emitter definitions from a content-relative JSON file.
		bool Init(const engine::core::Config& config);

		/// Shutdown the system and clear all particle state.
		void Shutdown();

		/// Spawn one emitter instance by id using its configured definition.
		bool SpawnEmitter(std::string_view emitterId);

		/// Advance the CPU simulation for all active emitters.
		bool Tick(float deltaSeconds);

		/// Build billboard instances for all active particles, sorted approximately back-to-front.
		void BuildBillboards(const engine::render::Camera& camera, std::vector<ParticleBillboard>& outBillboards) const;

	private:
		/// Load emitter definitions from the configured content-relative JSON file.
		bool LoadDefinitions();

		/// Emit new particles for one active emitter using its spawn accumulator.
		void EmitParticles(ParticleEmitterRuntime& emitter, float deltaSeconds);

		/// Advance existing particles for one active emitter.
		void UpdateParticles(ParticleEmitterRuntime& emitter, float deltaSeconds);

		/// Allocate one free particle slot from the emitter pool, or return null when full.
		ParticleInstance* AllocateParticle(ParticleEmitterRuntime& emitter);

		engine::core::Config m_config{};
		std::unordered_map<std::string, ParticleEmitterDefinition> m_definitions;
		std::vector<ParticleEmitterRuntime> m_emitters;
		std::string m_definitionsRelativePath;
		uint32_t m_rngState = 0x12345678u;
		bool m_initialized = false;
	};
}
