// SND.2 — Pas du personnage (cf. Footsteps.h).

#include "src/client/audio/Footsteps.h"

#include <algorithm>

namespace engine::audio
{
	float FootstepCadence::StrideLength(float speed)
	{
		// Marche ~2 m/s → 0,75 m ; course ~6 m/s → 1,3 m ; sprint ~8 m/s → 1,6 m.
		return std::clamp(0.47f + 0.14f * speed, 0.6f, 1.7f);
	}

	FootstepEvent FootstepCadence::Update(float dt, float speed, bool grounded, bool swimming)
	{
		if (dt <= 0.0f)
			return FootstepEvent::None;
		if (swimming)
		{
			m_wasGrounded = false;
			m_airTime = 0.0f;
			m_distance = 0.0f;
			if (speed < kMinSpeed)
			{
				m_strokeTimer = 0.0f;
				return FootstepEvent::None;
			}
			m_strokeTimer += dt;
			if (m_strokeTimer >= kStrokeSeconds)
			{
				m_strokeTimer -= kStrokeSeconds;
				return FootstepEvent::Stroke;
			}
			return FootstepEvent::None;
		}
		m_strokeTimer = 0.0f;
		if (!grounded)
		{
			m_airTime += dt;
			m_wasGrounded = false;
			return FootstepEvent::None;
		}
		if (!m_wasGrounded)
		{
			const bool land = m_airTime >= kMinAirTime;
			m_wasGrounded = true;
			m_airTime = 0.0f;
			// Après une réception, le prochain pas tombe à une demi-foulée.
			m_distance = StrideLength(speed) * 0.5f;
			if (land)
				return FootstepEvent::Land;
		}
		if (speed < kMinSpeed)
		{
			// À l'arrêt, le prochain départ fait entendre un pas assez vite.
			m_distance = std::max(m_distance, StrideLength(speed) * 0.6f);
			return FootstepEvent::None;
		}
		m_distance += speed * dt;
		const float stride = StrideLength(speed);
		if (m_distance >= stride)
		{
			m_distance -= stride;
			if (m_distance > stride)
				m_distance = 0.0f; // grand saut d'une frame (chargement) : pas de rafale de pas
			return FootstepEvent::Step;
		}
		return FootstepEvent::None;
	}

	void FootstepCadence::Reset()
	{
		m_distance = 0.0f;
		m_airTime = 0.0f;
		m_strokeTimer = 0.0f;
		m_wasGrounded = true;
	}

	std::string_view FootstepSoundId(const FootstepContext& ctx)
	{
		if (ctx.swimming)
			return "swim_stroke";
		if (ctx.inShallowWater)
			return "step_water";
		if (ctx.heightAboveTerrain > 0.3f)
			return "step_wood"; // plancher d'un bâtiment, pont, quai
		switch (ctx.layer)
		{
		case GroundLayer::Grass: return "step_grass";
		case GroundLayer::Dirt: return "step_dirt";
		case GroundLayer::Rock: return "step_rock";
		case GroundLayer::Snow: return "step_snow_crunch";
		case GroundLayer::Unknown: break;
		}
		return "step_dirt";
	}

	float FootstepVolume(float speed)
	{
		return std::clamp(0.45f + 0.08f * speed, 0.45f, 1.0f);
	}
}
