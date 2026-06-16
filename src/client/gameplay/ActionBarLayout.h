#pragma once

#include "src/client/gameplay/SpellKitCatalog.h"

#include <array>
#include <string>
#include <vector>

namespace engine::client
{
	/// Grimoire — résout le layout effectif des 10 slots de barre d'action.
	/// \param layout  layout persisté (slot i → spellId ; "" = vide).
	/// \param kit     kit du profil courant (SpellKitCatalog::FindKit).
	/// \return 10 spellId : si \p layout est entièrement vide → les sorts du kit
	///         dans l'ordre (slots au-delà du kit = "") ; sinon \p layout filtré
	///         (spellId absent du kit ou en doublon → slot vidé).
	std::array<std::string, 10> ResolveActionBarLayout(
		const std::array<std::string, 10>& layout,
		const std::vector<SpellDisplay>& kit);

	/// Retourne le SpellDisplay d'un spellId dans \p kit, ou nullptr.
	const SpellDisplay* FindSpellInKit(const std::vector<SpellDisplay>& kit, const std::string& spellId);
}
