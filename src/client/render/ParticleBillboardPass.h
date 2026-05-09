#pragma once

#include "engine/math/Math.h"
#include "engine/render/Camera.h"
#include "engine/render/ParticleSystem.h"

#include <vector>

namespace engine::render
{
	/// One expanded CPU billboard quad ready for a transparent particle pass.
	struct ParticleBillboardQuad
	{
		engine::math::Vec3 corners[4]{};
		float normalizedAge = 0.0f;
		float distanceToCameraSq = 0.0f;
	};

	/// CPU billboard pass that expands particle centers into camera-facing quads for transparent rendering.
	class ParticleBillboardPass final
	{
	public:
		/// Construct an uninitialized billboard pass.
		ParticleBillboardPass() = default;

		/// Release any cached billboard output.
		~ParticleBillboardPass();

		/// Initialize the pass state.
		bool Init();

		/// Shutdown the pass and clear generated billboard quads.
		void Shutdown();

		/// Build camera-facing quads from a sorted billboard list.
		bool BuildTransparentPassData(const Camera& camera, const std::vector<ParticleBillboard>& billboards);

		/// Access the generated transparent billboard quads.
		const std::vector<ParticleBillboardQuad>& GetQuads() const { return m_quads; }

	private:
		std::vector<ParticleBillboardQuad> m_quads;
		bool m_initialized = false;
	};
}
