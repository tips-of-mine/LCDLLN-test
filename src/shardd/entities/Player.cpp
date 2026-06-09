// Player : translation unit. La plupart des methodes sont inline dans le header ;
// ApplyDerivedStats est out-of-line car elle depend du moteur de stats
// (CharacterStatsEngine) — on garde le header leger cote dependances.
#include "src/shardd/entities/Player.h"

namespace engine::server::entities
{
	bool Player::ApplyDerivedStats(const engine::server::gameplay::CharacterStatsTables& tables,
	                               const std::string& factionId,
	                               const std::string& classId,
	                               engine::server::gameplay::Sex sex)
	{
		auto d = engine::server::gameplay::ComputeStats(tables, factionId, classId, sex, GetLevel());
		if (!d) return false;

		SetMaxHealth(d->hp);
		SetHealth(d->hp);                    // plein a l'application (apres SetMaxHealth pour le clamp)
		SetMaxSecondaryResource(d->resource);
		SetSecondaryResource(d->resource);
		SetDamage(d->damage);
		SetAccuracy(d->accuracy);
		SetRange(d->range);
		SetCritRate(d->critRate);
		SetCritMult(d->critMult);
		SetSpeedWalk(d->speedWalk);
		SetSpeedRun(d->speedRun);
		SetSpeedSprint(d->speedSprint);
		SetMaxStamina(d->stamina);
		SetStamina(d->stamina);
		SetPerception(d->perception);
		SetStealth(d->stealth);
		return true;
	}
}
