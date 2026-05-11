#pragma once
// Wave 23 — SpellMgr : catalogue des SpellTemplate. Indexe par spellId,
// lookup O(1). Immutable au runtime apres chargement initial (les
// templates sont des donnees DB read-only via SqlStorage Wave 18).
//
// Pas thread-safe en mutation : Register() doit etre appele uniquement
// pendant le boot ou un /reload manuel ; les lookups Find() sont
// concurrents safe (lecture seule).

#include "src/shardd/spell/SpellTemplate.h"

#include <cstddef>
#include <unordered_map>

namespace engine::server::spell
{
	class SpellMgr
	{
	public:
		SpellMgr() = default;

		/// Enregistre un template. Si un template avec le meme spellId existe
		/// deja, il est REMPLACE (hot-reload friendly).
		void Register(SpellTemplate tpl)
		{
			const SpellId id = tpl.spellId;
			m_templates[id] = std::move(tpl);
		}

		/// Cherche un template par id. Retourne nullptr si inconnu.
		const SpellTemplate* Find(SpellId id) const
		{
			auto it = m_templates.find(id);
			return (it != m_templates.end()) ? &it->second : nullptr;
		}

		/// Reset le catalogue. Utile pour les tests et le /reload spells.
		void Clear() noexcept { m_templates.clear(); }

		size_t TemplateCount() const noexcept { return m_templates.size(); }

	private:
		std::unordered_map<SpellId, SpellTemplate> m_templates;
	};
}
