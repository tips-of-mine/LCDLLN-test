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
}

int main()
{
	Test_LoadAndFind();
	Test_NonEquippable();
	Test_MissingIdReturnsNull();
	Test_BonusAccumulation();
	Test_InvalidJson();
	return g_failed == 0 ? 0 : 1;
}
