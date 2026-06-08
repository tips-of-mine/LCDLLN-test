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
			, m_damage(kUnitFieldDamage, &Mask())
			, m_accuracy(kUnitFieldAccuracy, &Mask())
			, m_range(kUnitFieldRange, &Mask())
			, m_critRate(kUnitFieldCritRate, &Mask())
			, m_critMult(kUnitFieldCritMult, &Mask())
			, m_speedWalk(kUnitFieldSpeedWalk, &Mask())
			, m_speedRun(kUnitFieldSpeedRun, &Mask())
			, m_speedSprint(kUnitFieldSpeedSprint, &Mask())
			, m_stamina(kUnitFieldStamina, &Mask())
			, m_maxStamina(kUnitFieldMaxStamina, &Mask())
			, m_perception(kUnitFieldPerception, &Mask())
			, m_stealth(kUnitFieldStealth, &Mask())
			, m_secondaryResource(kUnitFieldSecondaryResource, &Mask())
			, m_maxSecondaryResource(kUnitFieldMaxSecondaryResource, &Mask())
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

		// --- Stats étendues (Système de Personnages) ---
		void SetDamage(uint32_t v) { m_damage.Set(v); }
		uint32_t GetDamage() const noexcept { return m_damage.Get(); }
		void SetAccuracy(float v) { m_accuracy.Set(v); }
		float GetAccuracy() const noexcept { return m_accuracy.Get(); }
		void SetRange(float v) { m_range.Set(v); }
		float GetRange() const noexcept { return m_range.Get(); }
		void SetCritRate(float v) { m_critRate.Set(v); }
		float GetCritRate() const noexcept { return m_critRate.Get(); }
		void SetCritMult(float v) { m_critMult.Set(v); }
		float GetCritMult() const noexcept { return m_critMult.Get(); }
		void SetSpeedWalk(float v) { m_speedWalk.Set(v); }
		float GetSpeedWalk() const noexcept { return m_speedWalk.Get(); }
		void SetSpeedRun(float v) { m_speedRun.Set(v); }
		float GetSpeedRun() const noexcept { return m_speedRun.Get(); }
		void SetSpeedSprint(float v) { m_speedSprint.Set(v); }
		float GetSpeedSprint() const noexcept { return m_speedSprint.Get(); }
		void SetStamina(uint32_t v) { m_stamina.Set(v); }
		uint32_t GetStamina() const noexcept { return m_stamina.Get(); }
		void SetMaxStamina(uint32_t v) { m_maxStamina.Set(v); }
		uint32_t GetMaxStamina() const noexcept { return m_maxStamina.Get(); }
		void SetPerception(float v) { m_perception.Set(v); }
		float GetPerception() const noexcept { return m_perception.Get(); }
		void SetStealth(float v) { m_stealth.Set(v); }
		float GetStealth() const noexcept { return m_stealth.Get(); }
		void SetSecondaryResource(uint32_t v) { m_secondaryResource.Set(v); }
		uint32_t GetSecondaryResource() const noexcept { return m_secondaryResource.Get(); }
		void SetMaxSecondaryResource(uint32_t v) { m_maxSecondaryResource.Set(v); }
		uint32_t GetMaxSecondaryResource() const noexcept { return m_maxSecondaryResource.Get(); }

	private:
		UpdateField<uint32_t> m_health;
		UpdateField<uint32_t> m_maxHealth;
		UpdateField<uint32_t> m_mana;
		UpdateField<uint32_t> m_maxMana;
		UpdateField<uint32_t> m_level;
		UpdateField<uint32_t> m_faction;
		// --- Stats étendues (Système de Personnages) ---
		UpdateField<uint32_t> m_damage;
		UpdateField<float>    m_accuracy;
		UpdateField<float>    m_range;
		UpdateField<float>    m_critRate;
		UpdateField<float>    m_critMult;
		UpdateField<float>    m_speedWalk;
		UpdateField<float>    m_speedRun;
		UpdateField<float>    m_speedSprint;
		UpdateField<uint32_t> m_stamina;
		UpdateField<uint32_t> m_maxStamina;
		UpdateField<float>    m_perception;
		UpdateField<float>    m_stealth;
		UpdateField<uint32_t> m_secondaryResource;
		UpdateField<uint32_t> m_maxSecondaryResource;
	};
}
