#include "src/shardd/spell/SpellFamilyRuntime.h"

namespace engine::server::spell
{
	/// Enregistre trois SpellInfo V1 hardcodees. Les spellIds sont choisis
	/// pour evoquer les sorts canoniques sans pretendre matcher un DBC
	/// reel (ce sera le boulot du loader DB) :
	///   - 133  "Fireball"      : Mage, mask bit 0 set sur parts[0]
	///   - 116  "Frostbolt"     : Mage, mask bit 1 set sur parts[0]
	///   - 12294 "Mortal Strike" : Warrior, mask bit 0 set sur parts[0]
	/// Le decoupage des bits dans parts[] est arbitraire pour V1 ; le
	/// loader DB definira la convention canonique (a aligner sur les
	/// scripts existants au moment du portage).
	void SpellFamilyRuntime::SeedV1Families()
	{
		m_spells.clear();

		{
			SpellInfo si;
			si.spellId    = 133;
			si.family     = SpellFamily::Mage;
			si.familyMask = SpellFamilyMask(0x00000001u);
			m_spells.emplace(si.spellId, si);
		}
		{
			SpellInfo si;
			si.spellId    = 116;
			si.family     = SpellFamily::Mage;
			si.familyMask = SpellFamilyMask(0x00000002u);
			m_spells.emplace(si.spellId, si);
		}
		{
			SpellInfo si;
			si.spellId    = 12294;
			si.family     = SpellFamily::Warrior;
			si.familyMask = SpellFamilyMask(0x00000001u);
			m_spells.emplace(si.spellId, si);
		}
	}

	const SpellInfo* SpellFamilyRuntime::Find(std::uint32_t spellId) const noexcept
	{
		auto it = m_spells.find(spellId);
		return (it == m_spells.end()) ? nullptr : &it->second;
	}

	bool SpellFamilyRuntime::HasFamily(std::uint32_t spellId,
		SpellFamily mustHaveFamily) const noexcept
	{
		const SpellInfo* si = Find(spellId);
		return si != nullptr && si->family == mustHaveFamily;
	}

	bool SpellFamilyRuntime::MatchesProc(std::uint32_t spellId,
		SpellFamily mustHaveFamily, const SpellFamilyMask& anyOfMask) const noexcept
	{
		const SpellInfo* si = Find(spellId);
		if (si == nullptr) return false;
		return SpellMatchesProcCriteria(*si, mustHaveFamily, anyOfMask);
	}
}
