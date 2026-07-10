#pragma once

// Catalogue d'objets — modèle de données partagé client/serveur (Chantier 2 SP-A).
// Décrit l'identité d'un objet : type, slot d'équipement, bonus de stats,
// apparence. Aucune logique lourde ici (POD + petites fonctions utilitaires).
// Le serveur (shardd) est autoritaire : il lit CE modèle depuis sa propre copie
// de game/data/items/items.json et n'accepte jamais slot/bonus fournis par le
// client (anti-triche).

#include <cstdint>
#include <string>

namespace engine::items
{
	// Slot d'équipement gameplay. None (0) = objet non équipable (consommable,
	// quête, divers). 7 slots visuels (portés sur l'avatar) + 3 non visuels
	// (accessoires : amulette, 2 anneaux). L'ordre est FIGÉ (persisté en DB et
	// transmis sur le réseau) : ne jamais réordonner, seulement ajouter à la fin.
	enum class EquipmentSlot : std::uint8_t
	{
		None = 0,
		Head,      // 1
		Chest,     // 2
		Legs,      // 3
		Feet,      // 4
		Hands,     // 5
		MainHand,  // 6  (arme principale)
		OffHand,   // 7  (bouclier / 2ᵈᵉ main)
		Amulet,    // 8  (non visuel)
		Ring1,     // 9  (non visuel)
		Ring2,     // 10 (non visuel)
		Count      // 11 borne
	};

	// Nombre de slots réellement équipables (hors None). Utile pour dimensionner
	// les tableaux d'équipement (indices 1..kEquipSlotCount).
	inline constexpr std::size_t kEquipSlotCount =
		static_cast<std::size_t>(EquipmentSlot::Count) - 1u;

	// Catégorie d'objet (tri / affichage). Pas de logique gameplay en SP-A.
	enum class ItemType : std::uint8_t
	{
		Misc = 0,
		Weapon,
		Armor,
		Accessory,
		Consumable,
		Quest
	};

	// Bonus additif appliqué aux stats dérivées quand l'objet est équipé.
	// Miroir des champs numériques de shardd DerivedStats : chaque champ non nul
	// s'ADDITIONNE à la stat calculée (interpolation classe/race/sexe), clampée ≥ 0.
	struct StatBonus
	{
		std::int32_t hp = 0;
		std::int32_t resource = 0;
		std::int32_t damage = 0;
		std::int32_t accuracy = 0;
		float        range = 0.0f;
		float        critRate = 0.0f;
		float        critMult = 0.0f;
		float        speedWalk = 0.0f;
		float        speedRun = 0.0f;
		float        speedSprint = 0.0f;
		std::int32_t stamina = 0;
		std::int32_t perception = 0;
		std::int32_t stealth = 0;

		// Additionne un autre bonus (utilisé pour sommer l'équipement complet).
		StatBonus& operator+=(const StatBonus& o)
		{
			hp += o.hp; resource += o.resource; damage += o.damage;
			accuracy += o.accuracy; range += o.range; critRate += o.critRate;
			critMult += o.critMult; speedWalk += o.speedWalk; speedRun += o.speedRun;
			speedSprint += o.speedSprint; stamina += o.stamina;
			perception += o.perception; stealth += o.stealth;
			return *this;
		}
	};

	// Définition complète d'un objet du catalogue.
	struct ItemDefinition
	{
		std::uint32_t id = 0;
		std::string   name;
		std::string   description;
		std::string   iconPath;
		ItemType      type = ItemType::Misc;
		EquipmentSlot slot = EquipmentSlot::None; // None => non équipable
		StatBonus     bonus;                      // additif quand équipé
		std::string   visualMesh;                 // apparence modulaire (SP-D) ; vide en SP-A

		bool IsEquippable() const { return slot != EquipmentSlot::None; }
	};

	// Conversions chaîne <-> enum (parsing JSON et diagnostics).
	EquipmentSlot EquipmentSlotFromString(const std::string& s); // défaut None si inconnu
	ItemType      ItemTypeFromString(const std::string& s);      // défaut Misc si inconnu
	const char*   ToString(EquipmentSlot slot);
	const char*   ToString(ItemType type);
}
