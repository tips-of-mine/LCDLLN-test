#pragma once
// Wave 23 — Aura : effet persistant sur Unit (buff ou debuff). Cree au
// moment du Spell::Apply() quand durationMs > 0.
//
// Cycle de vie :
//   Cree (caster, target, expiresAtMs) ──Tick──> Tick periodic ──Expire──> dead
//                                             └─Stack──> stackCount incremente
//
// Stack rules : pour l'instant, simple — meme spellId du meme caster = stack.
// Le moteur appelle TryStack(other) avant d'ajouter un Aura, et si OK,
// l'Aura existant absorbe (refresh + incremente stackCount).

#include "src/shardd/spell/SpellTemplate.h"

#include <cstdint>

namespace engine::server::spell
{
	using CasterEntityId = uint64_t;

	class Aura
	{
	public:
		/// \param tpl         template du sort source (lifetime caller)
		/// \param caster      caster (utile pour replication / dispel rules)
		/// \param target      target ou l'Aura est applique
		/// \param appliedAtMs timestamp de creation (epoch ms ou ms relatif)
		Aura(const SpellTemplate& tpl, CasterEntityId caster, TargetEntityId target,
			uint64_t appliedAtMs)
			: m_tpl(&tpl), m_caster(caster), m_target(target)
			, m_appliedAtMs(appliedAtMs)
			, m_expiresAtMs(appliedAtMs + tpl.durationMs)
			, m_nextTickAtMs(tpl.tickPeriodMs > 0 ? appliedAtMs + tpl.tickPeriodMs : 0)
		{}

		const SpellTemplate& Template() const noexcept { return *m_tpl; }
		CasterEntityId       Caster() const noexcept { return m_caster; }
		TargetEntityId       Target() const noexcept { return m_target; }
		uint64_t             AppliedAtMs() const noexcept { return m_appliedAtMs; }
		uint64_t             ExpiresAtMs() const noexcept { return m_expiresAtMs; }
		uint32_t             StackCount() const noexcept { return m_stackCount; }

		/// True si \p nowMs >= expiresAtMs. L'Aura doit etre retire.
		bool IsExpired(uint64_t nowMs) const noexcept { return nowMs >= m_expiresAtMs; }

		/// Avance les ticks. Retourne le nombre de ticks consommes durant
		/// cette frame (0 si pas encore atteint). Le caller applique l'effet
		/// du sort (basePoints) une fois par tick.
		uint32_t AdvanceTick(uint64_t nowMs)
		{
			if (m_tpl->tickPeriodMs == 0) return 0;
			uint32_t ticks = 0;
			while (m_nextTickAtMs > 0 && nowMs >= m_nextTickAtMs && m_nextTickAtMs < m_expiresAtMs)
			{
				++ticks;
				m_nextTickAtMs += m_tpl->tickPeriodMs;
			}
			return ticks;
		}

		/// True si \p other peut s'empiler sur cette Aura. Rule simple Wave
		/// 23 : meme spellId ET meme caster. Si oui, le caller appelle Stack()
		/// pour incrementer + refresh expiresAtMs.
		bool CanStackWith(const Aura& other) const noexcept
		{
			return m_tpl->spellId == other.m_tpl->spellId
				&& m_caster == other.m_caster;
		}

		/// Empile : incremente stackCount + refresh expiresAtMs. \p nowMs
		/// est le timestamp de la nouvelle application.
		void Stack(uint64_t nowMs)
		{
			++m_stackCount;
			m_expiresAtMs = nowMs + m_tpl->durationMs;
			// Repositionner les ticks restants depuis nowMs si periodique.
			if (m_tpl->tickPeriodMs > 0)
				m_nextTickAtMs = nowMs + m_tpl->tickPeriodMs;
		}

	private:
		const SpellTemplate* m_tpl;
		CasterEntityId       m_caster;
		TargetEntityId       m_target;
		uint64_t             m_appliedAtMs;
		uint64_t             m_expiresAtMs;
		uint64_t             m_nextTickAtMs;   ///< 0 = pas de tick (Aura non periodique)
		uint32_t             m_stackCount = 1;
	};
}
