#pragma once
// UpdateFieldIndices : enums centralisees des indices UpdateField par classe.
// Valeurs STABLES (wire format en depend). Ne JAMAIS reassigner un indice
// existant ; ajouter de nouveaux indices a la fin de chaque enum.
//
// Convention : chaque sous-classe demarre ses propres indices APRES la fin
// du parent. Object 0..N-1, WorldObject N..M-1, Unit M..K-1, Player/Creature
// K..L-1. Player et Creature demarrent au meme index (apres Unit) car ce
// sont deux sous-classes paralleles ; chacune a son propre mask, pas de
// chevauchement reel.

#include <cstddef>

namespace engine::server::entities
{
	/// Indices pour la classe Object (base). Stable.
	enum ObjectFieldIdx : size_t
	{
		kObjectFieldGuid       = 0,  ///< ObjectGuid low 32 bits
		kObjectFieldGuidHigh   = 1,  ///< ObjectGuid high 32 bits
		kObjectFieldType       = 2,  ///< ObjectType (uint8 promu uint32)
		kObjectFieldEntry      = 3,  ///< template entry (creature/gameobject) ou 0
		kObjectFieldScaleX     = 4,  ///< scale visuel (float, slot 32-bit)

		kObjectFieldEnd        = 5
	};

	/// Nombre total de champs Object.
	static constexpr size_t kObjectFieldCount = kObjectFieldEnd;

	/// Indices pour WorldObject (extends Object). Demarre apres Object end.
	enum WorldObjectFieldIdx : size_t
	{
		kWorldObjectFieldMapId       = kObjectFieldEnd,      // 5
		kWorldObjectFieldZoneId      = kObjectFieldEnd + 1,  // 6
		kWorldObjectFieldPosX        = kObjectFieldEnd + 2,  // 7
		kWorldObjectFieldPosY        = kObjectFieldEnd + 3,  // 8
		kWorldObjectFieldPosZ        = kObjectFieldEnd + 4,  // 9
		kWorldObjectFieldOrientation = kObjectFieldEnd + 5,  // 10

		kWorldObjectFieldEnd         = kObjectFieldEnd + 6   // 11
	};

	static constexpr size_t kWorldObjectFieldCount = kWorldObjectFieldEnd;

	/// Indices pour Unit (extends WorldObject). Demarre apres WorldObject end.
	enum UnitFieldIdx : size_t
	{
		kUnitFieldHealth      = kWorldObjectFieldEnd,      // 11
		kUnitFieldMaxHealth   = kWorldObjectFieldEnd + 1,  // 12
		kUnitFieldMana        = kWorldObjectFieldEnd + 2,  // 13
		kUnitFieldMaxMana     = kWorldObjectFieldEnd + 3,  // 14
		kUnitFieldLevel       = kWorldObjectFieldEnd + 4,  // 15
		kUnitFieldFaction     = kWorldObjectFieldEnd + 5,  // 16
		// --- Stats étendues (Système de Personnages) ---
		kUnitFieldDamage              = kWorldObjectFieldEnd + 6,  // 17
		kUnitFieldAccuracy            = kWorldObjectFieldEnd + 7,  // 18
		kUnitFieldRange               = kWorldObjectFieldEnd + 8,  // 19
		kUnitFieldCritRate            = kWorldObjectFieldEnd + 9,  // 20
		kUnitFieldCritMult            = kWorldObjectFieldEnd + 10, // 21
		kUnitFieldSpeedWalk           = kWorldObjectFieldEnd + 11, // 22
		kUnitFieldSpeedRun            = kWorldObjectFieldEnd + 12, // 23
		kUnitFieldSpeedSprint         = kWorldObjectFieldEnd + 13, // 24
		kUnitFieldStamina             = kWorldObjectFieldEnd + 14, // 25
		kUnitFieldMaxStamina          = kWorldObjectFieldEnd + 15, // 26
		kUnitFieldPerception          = kWorldObjectFieldEnd + 16, // 27
		kUnitFieldStealth             = kWorldObjectFieldEnd + 17, // 28
		kUnitFieldSecondaryResource   = kWorldObjectFieldEnd + 18, // 29
		kUnitFieldMaxSecondaryResource= kWorldObjectFieldEnd + 19, // 30

		kUnitFieldEnd         = kWorldObjectFieldEnd + 20   // 31
	};

	static constexpr size_t kUnitFieldCount = kUnitFieldEnd;

	/// Indices pour Player (extends Unit). Demarre apres Unit end.
	enum PlayerFieldIdx : size_t
	{
		kPlayerFieldAccountId    = kUnitFieldEnd,      // 17
		kPlayerFieldCharacterId  = kUnitFieldEnd + 1,  // 18
		kPlayerFieldXp           = kUnitFieldEnd + 2,  // 19

		kPlayerFieldEnd          = kUnitFieldEnd + 3   // 20
	};

	static constexpr size_t kPlayerFieldCount = kPlayerFieldEnd;

	/// Indices pour Creature (extends Unit). Demarre apres Unit end (parallele a Player).
	enum CreatureFieldIdx : size_t
	{
		kCreatureFieldTemplateEntry = kUnitFieldEnd,      // 17
		kCreatureFieldSpawnId       = kUnitFieldEnd + 1,  // 18

		kCreatureFieldEnd           = kUnitFieldEnd + 2   // 19
	};

	static constexpr size_t kCreatureFieldCount = kCreatureFieldEnd;
}
