// src/client/world/hazard/HazardSimulator.cpp
#include "src/client/world/hazard/HazardSimulator.h"

#include <cmath>

namespace engine::world::hazard
{
	namespace
	{
		// Fenêtre de mash : compteur valide 5 s, reset après.
		constexpr float kMashWindowSec = 5.0f;
		constexpr int   kMashThreshold = 10;
		constexpr float kLateralThresholdMeters = 2.0f;
		constexpr float kLavaKillSec = 3.0f;
	}

	void HazardSimulator::Init(const HazardScene& scene, const HazardCallbacks& cb) noexcept
	{
		m_scene = &scene;
		m_cb = cb;
		m_state = HazardState{};
		m_hasLastPos = false;
	}

	HazardEffect HazardSimulator::Update(float dt, engine::math::Vec3 playerPos,
		bool /*actionPressed*/) noexcept
	{
		if (!m_scene) return HazardEffect{};

		// Détection entrée : si pas déjà dans un hazard, cherche un volume.
		if (!m_state.inHazard)
		{
			for (const auto& hz : m_scene->hazards)
			{
				if (PointInHazard(hz, playerPos))
				{
					m_state.inHazard = true;
					m_state.activeHazard = &hz;
					m_state.currentDepth = 0.0f;
					m_state.lateralTraveled = 0.0f;
					m_state.mashCount = 0;
					m_state.mashWindowSec = 0.0f;
					m_state.lavaTimer = 0.0f;
					if (m_cb.onEnter) m_cb.onEnter();
					break;
				}
			}
		}

		// Pas (ou plus) dans un hazard : reset et no-op.
		if (!m_state.inHazard)
		{
			m_lastPlayerPos = playerPos;
			m_hasLastPos = true;
			return HazardEffect{};
		}

		const HazardInstance& hz = *m_state.activeHazard;

		// LavaSurface : mort en 3 s, pas d'enfoncement ni d'escape.
		if (hz.type == HazardType::LavaSurface)
		{
			m_state.lavaTimer += dt;
			if (m_state.lavaTimer >= kLavaKillSec)
			{
				if (m_cb.die) m_cb.die("lava_burning");
				m_state = HazardState{};
				return HazardEffect{};
			}
			return HazardEffect{ false, 0.0f, hz.slowdownMul };
		}

		// Sinking progressif (Quicksand/Bog/Tar).
		m_state.currentDepth += hz.sinkRateMps * dt;

		// (Escape modes implémentés en Task 4.)

		// Mort si profondeur max atteinte sans escape.
		if (m_state.currentDepth >= hz.maxDepthMeters)
		{
			if (m_cb.die) m_cb.die("hazard_drowning");
			m_state = HazardState{};
			return HazardEffect{};
		}

		m_lastPlayerPos = playerPos;
		m_hasLastPos = true;
		return HazardEffect{ true, hz.sinkRateMps, hz.slowdownMul };
	}
}
