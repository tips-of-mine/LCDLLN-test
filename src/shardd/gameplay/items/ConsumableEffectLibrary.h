#pragma once
// ConsumableEffectLibrary — table FIXE des effets des objets CONSOMMABLES
// activables depuis la ceinture (Roadmap-3, 2026-07-19). Même philosophie
// que CakeBuffLibrary : mapping en code (peu d'entrées, stables), le
// catalogue items.json reste la source des noms/descriptions.
//
// V1 : deux familles d'effets — soin instantané (% PV max) et aura de buff
// (réutilise les SpellEffectType, dont MaxHealthPercent/MoveSpeedPercent
// livrés par Roadmap-2). Un objet consommé = pile décrémentée de 1.

#include "src/shardd/gameplay/spell/SpellKitLibrary.h"

#include <cstdint>

namespace engine::server
{
	/// Effet d'un consommable de ceinture.
	struct ConsumableEffectDef
	{
		uint32_t itemId = 0;
		/// Soin instantané en % des PV max (0 = pas de soin).
		float healPercentMaxHp = 0.0f;
		/// Aura appliquée au buveur (durée auraDurationMs ; ignorée si
		/// auraDurationMs == 0). spellId = identifiant d'aura répliqué (84).
		const char* auraSpellId = "";
		SpellEffectType auraType = SpellEffectType::BuffDamagePercent;
		float auraPercent = 0.0f;
		uint32_t auraDurationMs = 0;
		/// Notice chat FR envoyée au buveur.
		const char* noticeFr = "";
	};

	/// Retourne l'effet du consommable \p itemId, ou nullptr si cet objet
	/// n'est pas un consommable activable (les gâteaux ont leur propre
	/// chemin, cf. CakeBuffLibrary).
	const ConsumableEffectDef* FindConsumableEffect(uint32_t itemId);
}
