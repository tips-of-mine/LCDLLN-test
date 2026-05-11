#pragma once
// Wave 23 — SpellTemplate : donnees statiques d'un sort (immuable apres
// load). Compose avec SpellFamilyMask (Wave 9 existant) pour le matching
// proc/aura cross-spell.
//
// Le runtime cree un Spell (instance de cast) qui pointe vers ce template
// via spellId. Les valeurs (basePoints, durationMs, cooldownMs) restent
// lisibles via le template ; chaque cast applique d'eventuels modifiers.

#include "src/shardd/spell/SpellFamilyMask.h"

#include <cstdint>
#include <string>

namespace engine::server::spell
{
	using SpellId = uint32_t;

	/// Type de target sur le sort. Determine comment les engagements
	/// d'eligibilite (range, LOS, faction) sont evalues par le caster.
	enum class SpellTargetType : uint8_t
	{
		Self          = 0,
		SingleEnemy   = 1,
		SingleAlly    = 2,
		AreaAroundSelf = 3,   ///< AOE rayon autour du caster
		AreaAtTarget  = 4,    ///< AOE rayon autour du target
		Cone          = 5,    ///< Cone devant le caster
	};

	/// Donnees statiques d'un sort. Chargees une fois au boot, immuables.
	struct SpellTemplate
	{
		SpellId         spellId        = 0;
		std::string     name;          ///< affichage UI/log uniquement
		SpellTargetType targetType     = SpellTargetType::SingleEnemy;
		uint32_t        castTimeMs     = 1500;  ///< 0 = instant
		uint32_t        cooldownMs     = 0;     ///< 0 = pas de cooldown
		int32_t         basePoints     = 0;     ///< dommages / soins / mod stat (positif/negatif)
		uint32_t        durationMs     = 0;     ///< 0 = instant ; > 0 = applique un Aura
		uint32_t        tickPeriodMs   = 0;     ///< 0 = pas de tick (Aura passive)
		float           rangeMeters    = 30.0f;
		SpellFamilyMask familyMask;             ///< grouping pour proc/dispel
	};
}
