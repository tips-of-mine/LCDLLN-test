#pragma once
// Wave 23 — Spell : INSTANCE de cast en cours. State machine :
//
//   Idle ──Begin──> Casting ──TickReady──> Casted ──Apply──> Resolved
//                      │
//                      └──Interrupt──> Interrupted (terminal)
//
// Le caller (combat handler / event tick) appelle Tick(deltaMs) chaque
// frame. Quand l'etat passe a Casted, le caller doit appeler Apply()
// (effet sur target) avant que Resolved soit atteint.
//
// Stateless cote SpellTemplate : Spell detient juste l'avancement du cast.

#include "src/shardd/spell/SpellTemplate.h"

#include <cstdint>

namespace engine::server::spell
{
	using TargetEntityId = uint64_t;

	enum class SpellState : uint8_t
	{
		Idle        = 0,
		Casting     = 1,
		Casted      = 2,   ///< cast time complete, en attente Apply()
		Resolved    = 3,   ///< Apply() appele, effet propage
		Interrupted = 4,   ///< terminal, kick durant Casting
	};

	inline const char* SpellStateToString(SpellState s) noexcept
	{
		switch (s)
		{
			case SpellState::Idle:        return "Idle";
			case SpellState::Casting:     return "Casting";
			case SpellState::Casted:      return "Casted";
			case SpellState::Resolved:    return "Resolved";
			case SpellState::Interrupted: return "Interrupted";
		}
		return "Unknown";
	}

	class Spell
	{
	public:
		/// \param tpl template du sort (lifetime garanti par caller)
		/// \param target entityId du target principal (peut etre 0 pour Self)
		Spell(const SpellTemplate& tpl, TargetEntityId target)
			: m_tpl(&tpl), m_target(target)
		{}

		const SpellTemplate& Template() const noexcept { return *m_tpl; }
		TargetEntityId       Target() const noexcept { return m_target; }
		SpellState           State() const noexcept { return m_state; }
		uint32_t             ElapsedMs() const noexcept { return m_elapsedMs; }

		/// Demarre le cast : transition Idle → Casting. No-op si pas Idle.
		bool Begin()
		{
			if (m_state != SpellState::Idle) return false;
			m_state = SpellState::Casting;
			m_elapsedMs = 0;
			return true;
		}

		/// Avance le timer. Si elapsedMs >= castTimeMs, transition vers
		/// Casted (cast complete, en attente d'Apply). No-op si pas Casting.
		void Tick(uint32_t deltaMs)
		{
			if (m_state != SpellState::Casting) return;
			m_elapsedMs += deltaMs;
			if (m_elapsedMs >= m_tpl->castTimeMs)
				m_state = SpellState::Casted;
		}

		/// Marque le sort comme resolu (effet propage via Aura ou damage).
		/// No-op si pas Casted.
		bool Apply()
		{
			if (m_state != SpellState::Casted) return false;
			m_state = SpellState::Resolved;
			return true;
		}

		/// Interrompt le cast (kick, mort, mouvement). Transition possible
		/// uniquement depuis Casting. No-op sinon.
		bool Interrupt()
		{
			if (m_state != SpellState::Casting) return false;
			m_state = SpellState::Interrupted;
			return true;
		}

		/// True si le sort est terminal (Resolved ou Interrupted) — peut etre
		/// retire de la liste active.
		bool IsTerminal() const noexcept
		{
			return m_state == SpellState::Resolved || m_state == SpellState::Interrupted;
		}

	private:
		const SpellTemplate* m_tpl;
		TargetEntityId       m_target;
		SpellState           m_state    = SpellState::Idle;
		uint32_t             m_elapsedMs = 0;
	};
}
