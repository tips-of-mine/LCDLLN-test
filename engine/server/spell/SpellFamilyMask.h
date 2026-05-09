#pragma once
// CMANGOS.26 (Phase 3.26a) — SpellFamilyMask : bitmask 128-bit pour
// classifier les sorts d'une famille (Mage / Warrior / etc). Utilise
// pour les ProcEvent (un sort qui trigger seulement sur "Fire spells
// du Mage" se filtre via mask intersection).
//
// Pure data + bitwise ops. Header-only.

#include <cstdint>

namespace engine::server::spell
{
	enum class SpellFamily : uint8_t
	{
		Generic   = 0,
		Mage      = 1,
		Warrior   = 2,
		Warlock   = 3,
		Priest    = 4,
		Druid     = 5,
		Rogue     = 6,
		Hunter    = 7,
		Paladin   = 8,
		Shaman    = 9,
		DeathKnight = 10,
	};

	/// Bitmask 128 bits decoupe en 4 uint32. La spec cmangos utilise
	/// 96 bits (3 × uint32) pour TBC ; on prevoit 128 pour extension.
	struct SpellFamilyMask
	{
		uint32_t parts[4] = {0, 0, 0, 0};

		constexpr SpellFamilyMask() = default;
		constexpr SpellFamilyMask(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) noexcept
			: parts{a, b, c, d} {}

		constexpr bool IsEmpty() const noexcept
		{
			return parts[0] == 0 && parts[1] == 0 && parts[2] == 0 && parts[3] == 0;
		}

		/// True si l'un des bits set dans \p other est aussi set dans this.
		constexpr bool IntersectsWith(const SpellFamilyMask& other) const noexcept
		{
			return (parts[0] & other.parts[0]) != 0
				|| (parts[1] & other.parts[1]) != 0
				|| (parts[2] & other.parts[2]) != 0
				|| (parts[3] & other.parts[3]) != 0;
		}

		/// True si TOUS les bits set dans \p subset sont aussi set dans this.
		constexpr bool Contains(const SpellFamilyMask& subset) const noexcept
		{
			return (parts[0] & subset.parts[0]) == subset.parts[0]
				&& (parts[1] & subset.parts[1]) == subset.parts[1]
				&& (parts[2] & subset.parts[2]) == subset.parts[2]
				&& (parts[3] & subset.parts[3]) == subset.parts[3];
		}

		constexpr SpellFamilyMask operator|(const SpellFamilyMask& o) const noexcept
		{
			return SpellFamilyMask(parts[0] | o.parts[0], parts[1] | o.parts[1],
				parts[2] | o.parts[2], parts[3] | o.parts[3]);
		}
		constexpr SpellFamilyMask operator&(const SpellFamilyMask& o) const noexcept
		{
			return SpellFamilyMask(parts[0] & o.parts[0], parts[1] & o.parts[1],
				parts[2] & o.parts[2], parts[3] & o.parts[3]);
		}
		constexpr bool operator==(const SpellFamilyMask& o) const noexcept = default;
	};

	/// Information minimale sur un sort cote serveur. Etendre avec
	/// damage / cooldown / effect_type au fil des besoins.
	struct SpellInfo
	{
		uint32_t        spellId = 0;
		SpellFamily     family  = SpellFamily::Generic;
		SpellFamilyMask familyMask;  ///< pour ProcEvent matching
	};

	/// Helper : true si \p triggeringSpell match les criteres ProcEvent
	/// \p mustHaveFamily + \p anyOfMask. Si anyOfMask est vide, tout sort
	/// de la famille match.
	inline bool SpellMatchesProcCriteria(const SpellInfo& triggeringSpell,
		SpellFamily mustHaveFamily, const SpellFamilyMask& anyOfMask) noexcept
	{
		if (triggeringSpell.family != mustHaveFamily) return false;
		if (anyOfMask.IsEmpty()) return true;
		return triggeringSpell.familyMask.IntersectsWith(anyOfMask);
	}
}
