// M100.16 — Implémentation du simulateur de hazard (machine à états pure).

#include "src/client/world/hazard/HazardSimulator.h"

namespace engine::world::hazard
{
	void HazardSimulator::Enter(const HazardVolume& volume)
	{
		m_volume = volume;
		m_state = HazardState::Sinking;
		m_depth = 0.0f;
		m_mashCount = 0;
		m_mashWindowElapsed = 0.0f;
		m_lateralAccum = 0.0f;
		m_lavaTimer = 0.0f;
	}

	void HazardSimulator::Exit()
	{
		if (m_state == HazardState::Sinking)
		{
			m_state = HazardState::Idle;
			m_depth = 0.0f;
		}
	}

	HazardOutput HazardSimulator::Update(const HazardInput& in)
	{
		HazardOutput out;
		out.state = m_state;
		out.currentDepth = m_depth;
		out.groundOffsetMeters = m_depth;
		out.slowdownMul = (m_state == HazardState::Sinking) ? m_volume.slowdownMul : 1.0f;

		if (m_state != HazardState::Sinking)
			return out;

		// LavaSurface : pas d'enfoncement ni d'évasion, mort après 3 s.
		if (m_volume.type == HazardType::LavaSurface)
		{
			m_lavaTimer += in.dtSeconds;
			if (m_lavaTimer >= kLavaDeathSeconds)
			{
				m_state = HazardState::Dead;
				out.state = m_state;
				out.deathReason = "lava_burning";
			}
			out.slowdownMul = m_volume.slowdownMul;
			return out;
		}

		// Enfoncement progressif.
		m_depth += m_volume.sinkRateMps * in.dtSeconds;

		// Tentatives d'évasion (vérifiées AVANT la mort).
		bool escaped = false;
		switch (m_volume.escapeMode)
		{
			case EscapeMode::MashButton:
			case EscapeMode::MashButtonRequireItem:
			{
				m_mashWindowElapsed += in.dtSeconds;
				if (m_mashWindowElapsed >= kMashWindowSeconds)
				{
					// Fenêtre glissante : on repart à zéro.
					m_mashWindowElapsed = 0.0f;
					m_mashCount = 0;
				}
				if (in.actionPressed) ++m_mashCount;
				const bool itemOk = (m_volume.escapeMode == EscapeMode::MashButton) || in.hasRequiredItem;
				if (m_mashCount >= kMashRequiredPresses && itemOk) escaped = true;
				break;
			}
			case EscapeMode::LateralMove:
			{
				m_lateralAccum += (in.lateralDeltaMeters < 0.0f ? -in.lateralDeltaMeters : in.lateralDeltaMeters);
				if (m_lateralAccum >= kLateralEscapeMeters) escaped = true;
				break;
			}
			case EscapeMode::None:
			default:
				break;
		}

		if (escaped)
		{
			m_state = HazardState::Escaped;
			out.state = m_state;
			out.justEscaped = true;
			out.currentDepth = m_depth;
			out.groundOffsetMeters = m_depth;
			return out;
		}

		if (m_depth >= m_volume.maxDepthMeters)
		{
			m_state = HazardState::Dead;
			out.state = m_state;
			out.deathReason = "hazard_drowning";
		}

		out.currentDepth = m_depth;
		out.groundOffsetMeters = m_depth;
		return out;
	}
}
