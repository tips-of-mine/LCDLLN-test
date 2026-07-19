// CakeBuffLibrary — implémentation. Voir le header. Les magnitudes sont
// volontairement modestes (buff de fête, pas un consommable de raid) et
// alignées sur les descriptions d'items.json (5101-5110).

#include "src/shardd/gameplay/anniversary/CakeBuffLibrary.h"

namespace engine::server
{
	namespace
	{
		constexpr CakeBuffDef kCakeBuffs[] = {
			{ 5101u, "gateau_braves",    "+6 % de dégâts infligés",   SpellEffectType::BuffDamagePercent,      6.0f },
			{ 5102u, "gateau_colosse",   "-8 % de dégâts subis",      SpellEffectType::DamageReductionPercent, 8.0f },
			// Roadmap-2 (2026-07-19) — les effets de VRAIES stats existent
			// désormais : le zéphyr retrouve sa vitesse, le sage donne des PV.
			{ 5103u, "gateau_zephyr",    "+8 % de vitesse de déplacement", SpellEffectType::MoveSpeedPercent,  8.0f },
			{ 5104u, "gateau_sage",      "+8 % de PV maximum",             SpellEffectType::MaxHealthPercent,  8.0f },
			{ 5105u, "gateau_guetteur",  "+4 % de dégâts infligés",   SpellEffectType::BuffDamagePercent,      4.0f },
			{ 5106u, "gateau_duelliste", "+8 % de dégâts infligés",   SpellEffectType::BuffDamagePercent,      8.0f },
			{ 5107u, "gateau_gardien",   "-10 % de dégâts subis",     SpellEffectType::DamageReductionPercent, 10.0f },
			{ 5108u, "gateau_erudit",    "-6 % de dégâts subis",      SpellEffectType::DamageReductionPercent, 6.0f },
			{ 5109u, "gateau_chasseur",  "+7 % de dégâts infligés",   SpellEffectType::BuffDamagePercent,      7.0f },
			{ 5110u, "gateau_vagabond",  "+5 % de dégâts infligés",   SpellEffectType::BuffDamagePercent,      5.0f },
		};
	}

	const CakeBuffDef* FindCakeBuff(uint32_t itemId)
	{
		for (const CakeBuffDef& def : kCakeBuffs)
		{
			if (def.itemId == itemId) return &def;
		}
		return nullptr;
	}
}
