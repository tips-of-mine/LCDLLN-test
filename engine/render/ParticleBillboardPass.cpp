#include "engine/render/ParticleBillboardPass.h"

#include "engine/core/Log.h"

#include <cmath>

namespace engine::render
{
	namespace
	{
		engine::math::Vec3 Cross(const engine::math::Vec3& a, const engine::math::Vec3& b)
		{
			return engine::math::Vec3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x);
		}

		engine::math::Vec3 BuildForward(const Camera& camera)
		{
			const float cosPitch = std::cos(camera.pitch);
			return engine::math::Vec3(
				std::sin(camera.yaw) * cosPitch,
				std::sin(camera.pitch),
				-std::cos(camera.yaw) * cosPitch).Normalized();
		}
	}

	ParticleBillboardPass::~ParticleBillboardPass()
	{
		Shutdown();
	}

	bool ParticleBillboardPass::Init()
	{
		m_quads.clear();
		m_initialized = true;
		LOG_INFO(Render, "[ParticleBillboardPass] Init OK");
		return true;
	}

	void ParticleBillboardPass::Shutdown()
	{
		m_quads.clear();
		m_initialized = false;
		LOG_INFO(Render, "[ParticleBillboardPass] Shutdown complete");
	}

	bool ParticleBillboardPass::BuildTransparentPassData(const Camera& camera, const std::vector<ParticleBillboard>& billboards)
	{
		if (!m_initialized)
		{
			LOG_WARN(Render, "[ParticleBillboardPass] BuildTransparentPassData ignored: pass not initialized");
			return false;
		}

		m_quads.clear();
		if (billboards.empty())
		{
			LOG_INFO(Render, "[ParticleBillboardPass] No billboards to expand for transparent pass");
			return true;
		}

		const engine::math::Vec3 forward = BuildForward(camera);
		engine::math::Vec3 right = Cross(forward, engine::math::Vec3(0.0f, 1.0f, 0.0f)).Normalized();
		if (right.LengthSq() <= 0.0f)
		{
			right = engine::math::Vec3(1.0f, 0.0f, 0.0f);
		}
		engine::math::Vec3 up = Cross(right, forward).Normalized();
		if (up.LengthSq() <= 0.0f)
		{
			up = engine::math::Vec3(0.0f, 1.0f, 0.0f);
		}

		m_quads.reserve(billboards.size());
		for (const ParticleBillboard& billboard : billboards)
		{
			const float halfSize = billboard.size * 0.5f;
			const engine::math::Vec3 rightOffset = right * halfSize;
			const engine::math::Vec3 upOffset = up * halfSize;

			ParticleBillboardQuad quad{};
			quad.corners[0] = billboard.center - rightOffset - upOffset;
			quad.corners[1] = billboard.center + rightOffset - upOffset;
			quad.corners[2] = billboard.center + rightOffset + upOffset;
			quad.corners[3] = billboard.center - rightOffset + upOffset;
			quad.normalizedAge = billboard.normalizedAge;
			quad.distanceToCameraSq = billboard.distanceToCameraSq;
			m_quads.push_back(quad);
		}

		LOG_INFO(Render, "[ParticleBillboardPass] Built {} transparent quads", static_cast<uint32_t>(m_quads.size()));
		return true;
	}
}
