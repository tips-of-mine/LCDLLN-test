#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::server::combat
{
	/// Combat SP2 — résultat d'un jet d'attaque résolu par ResolveAttackRoll.
	struct AttackRollResult
	{
		bool miss = false;
		bool crit = false;
		uint32_t damage = 0;
	};

	/// Combat SP2 — résout un jet d'attaque de façon PURE et déterministe : les
	/// deux jets aléatoires [0,1) sont fournis par l'appelant (RNG injecté), ce
	/// qui rend toutes les branches testables sans graine ni mock.
	///
	/// Règles :
	/// - précision : raté si roll01Accuracy >= accuracy/100 (accuracy clampée
	///   [0,100] ; 100 = ne rate jamais, 0 = rate toujours). Raté → damage 0.
	/// - critique : touché ET roll01Crit < critRate/100 (critRate clampé
	///   [0,100]) → damage = baseDamage × critMult arrondi au plus proche
	///   (critMult plancher 1.0 — un crit ne réduit jamais les dégâts).
	/// - sinon touché normal → damage = baseDamage.
	///
	/// \param roll01Accuracy jet [0,1) consommé par le test de précision.
	/// \param roll01Crit jet [0,1) consommé par le test de critique.
	inline AttackRollResult ResolveAttackRoll(
		uint32_t baseDamage,
		float accuracy,
		float critRate,
		float critMult,
		float roll01Accuracy,
		float roll01Crit)
	{
		AttackRollResult result{};

		const float clampedAccuracy = std::clamp(accuracy, 0.0f, 100.0f);
		if (roll01Accuracy >= clampedAccuracy / 100.0f)
		{
			result.miss = true;
			result.damage = 0;
			return result;
		}

		const float clampedCritRate = std::clamp(critRate, 0.0f, 100.0f);
		if (roll01Crit < clampedCritRate / 100.0f)
		{
			result.crit = true;
			const float flooredMult = std::max(critMult, 1.0f);
			const double critDamage = static_cast<double>(baseDamage) * static_cast<double>(flooredMult);
			result.damage = static_cast<uint32_t>(std::llround(critDamage));
			return result;
		}

		result.damage = baseDamage;
		return result;
	}
}
