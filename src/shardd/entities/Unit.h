#pragma once
// Unit : WorldObject + stats combat (HP, MP, level, faction). Base pour
// Player et Creature. N'expose pas encore les hooks combat (Wave 19
// HostileRefManager) ni les Auras (Wave 23 SpellMgr).
//
// IsAlive : HP > 0. Pas de notion de spirit-of-redemption ou d'invulnerabilite
// a ce stade — c'est le travail des Auras (Wave 23).
//
// SetHealth : clamp [0, maxHealth]. Si on tente d'attribuer une valeur > max,
// la valeur stockee est plafonnee a max. Le mask reflete la valeur stockee,
// pas la valeur en entree.

#include "src/shardd/entities/WorldObject.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace engine::server::entities
{
	/// Unit : herite WorldObject, ajoute stats combat de base.
	class Unit : public WorldObject
	{
	public:
		Unit(ObjectGuid guid, size_t fieldCount = kUnitFieldCount)
			: WorldObject(guid, fieldCount)
			, m_health(kUnitFieldHealth, &Mask())
			, m_maxHealth(kUnitFieldMaxHealth, &Mask())
			, m_mana(kUnitFieldMana, &Mask())
			, m_maxMana(kUnitFieldMaxMana, &Mask())
			, m_level(kUnitFieldLevel, &Mask())
			, m_faction(kUnitFieldFaction, &Mask())
		{}

		~Unit() override = default;

		/// Set HP. Clamp [0, maxHealth]. Si maxHealth = 0 (pas encore initialise),
		/// HP est clampe a 0.
		void SetHealth(uint32_t hp)
		{
			const uint32_t maxHp = m_maxHealth.Get();
			m_health.Set(std::min(hp, maxHp));
		}
		uint32_t GetHealth() const noexcept { return m_health.Get(); }

		void SetMaxHealth(uint32_t maxHp) { m_maxHealth.Set(maxHp); }
		uint32_t GetMaxHealth() const noexcept { return m_maxHealth.Get(); }

		/// Set Mana. Clamp [0, maxMana].
		void SetMana(uint32_t mp)
		{
			const uint32_t maxMp = m_maxMana.Get();
			m_mana.Set(std::min(mp, maxMp));
		}
		uint32_t GetMana() const noexcept { return m_mana.Get(); }

		void SetMaxMana(uint32_t maxMp) { m_maxMana.Set(maxMp); }
		uint32_t GetMaxMana() const noexcept { return m_maxMana.Get(); }

		void SetLevel(uint32_t lvl) { m_level.Set(lvl); }
		uint32_t GetLevel() const noexcept { return m_level.Get(); }

		void SetFaction(uint32_t f) { m_faction.Set(f); }
		uint32_t GetFaction() const noexcept { return m_faction.Get(); }

		/// True si HP > 0.
		bool IsAlive() const noexcept { return m_health.Get() > 0; }

	private:
		UpdateField<uint32_t> m_health;
		UpdateField<uint32_t> m_maxHealth;
		UpdateField<uint32_t> m_mana;
		UpdateField<uint32_t> m_maxMana;
		UpdateField<uint32_t> m_level;
		UpdateField<uint32_t> m_faction;
	};
}
