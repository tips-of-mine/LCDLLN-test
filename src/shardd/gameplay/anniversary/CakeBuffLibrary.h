#pragma once
// CakeBuffLibrary — table FIXE des 10 buffs de gâteau d'anniversaire
// (spec 2026-07-18, SP3). Mapping itemId (5101..5110, items.json) → effet
// d'aura. Volontairement en code (pas en JSON) : 10 entrées stables, aucun
// besoin d'itération data, et les effets réutilisent les SpellEffectType
// EXISTANTS (aucune nouvelle plomberie de stats) :
//   - BuffDamagePercent      : +% dégâts infligés
//   - DamageReductionPercent : -% dégâts subis
//   - ThreatReducePercent    : -% menace générée
//
// L'aura est appliquée/rafraîchie par ServerApp::TickCakeBuffs aux membres
// du groupe/guilde à portée tant que le gâteau reste slotté (cf. ServerApp).

#include "src/shardd/gameplay/spell/SpellKitLibrary.h"

#include <cstdint>

namespace engine::server
{
	/// Définition d'un buff de gâteau (une aura, un effet).
	struct CakeBuffDef
	{
		uint32_t itemId = 0;
		/// Identifiant d'aura (répliqué au client par AuraUpdate, kind 84).
		const char* buffSpellId = "";
		/// Description courte FR (notice chat à l'activation).
		const char* noticeFr = "";
		SpellEffectType type = SpellEffectType::BuffDamagePercent;
		float percent = 0.0f;
	};

	/// Retourne la définition du buff pour \p itemId, ou nullptr si \p itemId
	/// n'est pas un gâteau (5101..5110).
	const CakeBuffDef* FindCakeBuff(uint32_t itemId);
}
