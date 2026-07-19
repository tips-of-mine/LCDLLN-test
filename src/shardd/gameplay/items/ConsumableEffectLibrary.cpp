// ConsumableEffectLibrary — implémentation. Voir le header.

#include "src/shardd/gameplay/items/ConsumableEffectLibrary.h"

namespace engine::server
{
	namespace
	{
		constexpr ConsumableEffectDef kConsumables[] = {
			// 2002 Minor Potion — le grand classique : soin instantané 35 %.
			{ 2002u, 35.0f, "", SpellEffectType::BuffDamagePercent, 0.0f, 0u,
			  "Vous buvez la potion mineure : +35 % de PV." },
		};
	}

	const ConsumableEffectDef* FindConsumableEffect(uint32_t itemId)
	{
		for (const ConsumableEffectDef& def : kConsumables)
		{
			if (def.itemId == itemId) return &def;
		}
		return nullptr;
	}
}
