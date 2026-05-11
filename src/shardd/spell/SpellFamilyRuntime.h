#pragma once
// Wave 9 — Wrapper runtime SpellFamily : detient un registre statique de
// SpellInfo (spellId -> family + familyMask) seede au boot du shardd avec
// 2-3 sample spells hardcodees (Fireball, Frostbolt, Mortal Strike). V1
// : registre in-memory, lookup O(1). Future iteration : SeedFromDb() qui
// remplace SeedV1Families() en chargeant depuis MySQL (table spell_info
// avec famille + mask128).
//
// Pas de Tick() : SpellFamilyMask est une primitive de classification
// utilisee a la demande par les futures couches ProcEvent / SpellCast
// / AuraTrigger. Le runtime expose juste un Find(spellId) + HasFamily()
// + MatchesProc() pour que le code appelant ne touche pas directement
// au unordered_map.

#include "src/shardd/spell/SpellFamilyMask.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace engine::server::spell
{
	/// Wrapper minimaliste autour d'un registre spellId -> SpellInfo.
	/// Future iteration : ce registre sera charge depuis la DB et le
	/// runtime exposera Refresh() + un cache invalidation hook.
	class SpellFamilyRuntime
	{
	public:
		SpellFamilyRuntime() = default;

		/// Charge 2-3 SpellInfo V1 hardcodees pour exercer le wiring.
		/// Voir .cpp pour les valeurs (Fireball/Frostbolt en famille Mage,
		/// Mortal Strike en famille Warrior). Idempotent : peut etre
		/// rappele pour reset le registre (utile en tests, jamais en prod).
		void SeedV1Families();

		/// Cherche un spell par id. Retourne nullptr si inconnu. Pointeur
		/// non-owning : la lifetime suit celle du registre interne, ne pas
		/// le stocker au-dela d'un appel a Refresh()/SeedV1Families().
		const SpellInfo* Find(std::uint32_t spellId) const noexcept;

		/// True si \p spellId existe ET sa famille == \p mustHaveFamily.
		/// Convenance pour les codes appelants qui ne se soucient pas du
		/// mask 128.
		bool HasFamily(std::uint32_t spellId, SpellFamily mustHaveFamily) const noexcept;

		/// Wrapper sur SpellMatchesProcCriteria : true si \p spellId est
		/// connu ET match le critere ProcEvent (\p mustHaveFamily +
		/// \p anyOfMask).
		bool MatchesProc(std::uint32_t spellId, SpellFamily mustHaveFamily,
			const SpellFamilyMask& anyOfMask) const noexcept;

		/// Nombre de spells enregistres (sert pour le log boot
		/// "[SpellFamily] N spells registered at boot").
		std::size_t SpellCount() const noexcept { return m_spells.size(); }

	private:
		std::unordered_map<std::uint32_t, SpellInfo> m_spells;
	};
}
