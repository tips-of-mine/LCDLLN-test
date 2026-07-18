#include "src/client/gameplay/ActionBarLayout.h"

#include "src/shared/anniversary/CakeItemToken.h" // SP3 anniversaires (2026-07-18)

namespace engine::client
{
	const SpellDisplay* FindSpellInKit(const std::vector<SpellDisplay>& kit, const std::string& spellId)
	{
		for (const SpellDisplay& spell : kit)
		{
			if (spell.spellId == spellId)
			{
				return &spell;
			}
		}
		return nullptr;
	}

	std::array<std::string, 10> ResolveActionBarLayout(
		const std::array<std::string, 10>& layout,
		const std::vector<SpellDisplay>& kit)
	{
		std::array<std::string, 10> resolved{};

		const bool layoutEmpty = [&layout]() {
			for (const std::string& slot : layout)
			{
				if (!slot.empty())
				{
					return false;
				}
			}
			return true;
		}();

		if (layoutEmpty)
		{
			// Défaut : sorts du kit dans l'ordre, plafonné à 10.
			const size_t count = std::min<size_t>(kit.size(), resolved.size());
			for (size_t i = 0; i < count; ++i)
			{
				resolved[i] = kit[i].spellId;
			}
			return resolved;
		}

		// Layout custom : filtre les spellId hors-kit + doublons. Les jetons
		// de gâteau "item:<id>" (SP3 anniversaires) passent tels quels : ils
		// ne sont pas des sorts du kit mais le serveur les a validés (objet
		// possédé) — la barre les rend comme des objets utilisables.
		for (size_t i = 0; i < layout.size(); ++i)
		{
			const std::string& spellId = layout[i];
			uint32_t cakeItemId = 0u;
			const bool isCake = engine::anniversary::ParseCakeToken(spellId, cakeItemId);
			if (spellId.empty() || (!isCake && FindSpellInKit(kit, spellId) == nullptr))
			{
				continue;
			}
			bool duplicate = false;
			for (size_t prior = 0; prior < i; ++prior)
			{
				if (resolved[prior] == spellId)
				{
					duplicate = true;
					break;
				}
			}
			if (!duplicate)
			{
				resolved[i] = spellId;
			}
		}
		return resolved;
	}
}
