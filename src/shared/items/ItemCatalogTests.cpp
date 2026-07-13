#include "src/shared/items/ItemCatalog.h"

#include <cstdio>
#include <string>

using engine::items::EquipmentSlot;
using engine::items::ItemCatalog;
using engine::items::ItemDefinition;
using engine::items::ItemType;
using engine::items::StatBonus;

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	// Catalogue minimal en dur : 1 arme (mainhand, bonus dégât) + 1 consommable
	// (non équipable). Couvre parsing type/slot/bonus + IsEquippable.
	const char* kJson = R"JSON(
	{
	  "items": [
	    {
	      "id": 2004,
	      "name": "Epee courte",
	      "description": "Une lame courte.",
	      "icon": "items/epee.png",
	      "type": "weapon",
	      "slot": "mainhand",
	      "bonus": { "damage": 12, "accuracy": 3, "critRate": 0.02 }
	    },
	    {
	      "id": 3001,
	      "name": "Pomme",
	      "type": "consumable",
	      "slot": "none"
	    }
	  ]
	}
	)JSON";

	void Test_LoadAndFind()
	{
		ItemCatalog cat;
		REQUIRE(cat.LoadFromJson(kJson));
		REQUIRE(cat.Count() == 2);

		const ItemDefinition* epee = cat.Find(2004);
		REQUIRE(epee != nullptr);
		if (epee)
		{
			REQUIRE(epee->name == "Epee courte");
			REQUIRE(epee->type == ItemType::Weapon);
			REQUIRE(epee->slot == EquipmentSlot::MainHand);
			REQUIRE(epee->IsEquippable());
			REQUIRE(epee->bonus.damage == 12);
			REQUIRE(epee->bonus.accuracy == 3);
			REQUIRE(epee->bonus.critRate > 0.019f && epee->bonus.critRate < 0.021f);
			REQUIRE(epee->bonus.hp == 0); // champ absent => 0
		}
	}

	void Test_NonEquippable()
	{
		ItemCatalog cat;
		REQUIRE(cat.LoadFromJson(kJson));
		const ItemDefinition* pomme = cat.Find(3001);
		REQUIRE(pomme != nullptr);
		if (pomme)
		{
			REQUIRE(pomme->type == ItemType::Consumable);
			REQUIRE(pomme->slot == EquipmentSlot::None);
			REQUIRE(!pomme->IsEquippable());
		}
	}

	void Test_MissingIdReturnsNull()
	{
		ItemCatalog cat;
		REQUIRE(cat.LoadFromJson(kJson));
		REQUIRE(cat.Find(9999) == nullptr);
	}

	void Test_BonusAccumulation()
	{
		StatBonus a; a.hp = 15; a.damage = 5;
		StatBonus b; b.hp = 30; b.speedWalk = 0.2f;
		a += b;
		REQUIRE(a.hp == 45);
		REQUIRE(a.damage == 5);
		REQUIRE(a.speedWalk > 0.19f && a.speedWalk < 0.21f);
	}

	void Test_InvalidJson()
	{
		ItemCatalog cat;
		REQUIRE(!cat.LoadFromJson("{ ceci n'est pas du json"));
		REQUIRE(cat.Count() == 0);
	}

	// Garde anti-régression sur le VRAI catalogue (game/data/items/items.json).
	// CTest exécute depuis la racine du repo (WORKING_DIRECTORY=CMAKE_SOURCE_DIR),
	// donc le chemin relatif résout. Vérifie qu'aucune collision d'id ne rend un
	// objet de BUTIN équipable par erreur (bug 2026-07-13 : Minor Potion id 2002
	// héritait d'une armure torse à cause d'une collision d'id).
	void Test_RealCatalog_LootNotEquippable()
	{
		ItemCatalog cat;
		REQUIRE(cat.LoadFromFile("game/data/items/items.json"));
		REQUIRE(cat.Count() > 0);

		// Minor Potion (2002) = consommable de butin : JAMAIS équipable.
		const ItemDefinition* potion = cat.Find(2002);
		REQUIRE(potion != nullptr);
		if (potion)
		{
			REQUIRE(potion->type == ItemType::Consumable);
			REQUIRE(!potion->IsEquippable());
		}
		// Rusty Sword (2001) = arme (main droite).
		if (const ItemDefinition* sword = cat.Find(2001))
		{
			REQUIRE(sword->slot == EquipmentSlot::MainHand);
		}
	}
}

int main()
{
	Test_LoadAndFind();
	Test_NonEquippable();
	Test_MissingIdReturnsNull();
	Test_BonusAccumulation();
	Test_InvalidJson();
	Test_RealCatalog_LootNotEquippable();
	return g_failed == 0 ? 0 : 1;
}
