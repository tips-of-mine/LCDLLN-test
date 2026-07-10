#include "src/shared/items/ItemCatalog.h"

#include "src/shared/core/Config.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>

namespace engine::items
{
	namespace
	{
		// Minuscules ASCII (comparaison de tokens de type/slot insensible à la casse).
		std::string ToLowerAscii(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// Lit le bloc "bonus" d'un objet (préfixe = "items[i].bonus"). Chaque champ
		// est optionnel ; absent => 0. Les floats passent par GetDouble.
		StatBonus ReadBonus(const engine::core::Config& cfg, const std::string& p)
		{
			StatBonus b;
			b.hp          = static_cast<std::int32_t>(cfg.GetInt(p + ".hp", 0));
			b.resource    = static_cast<std::int32_t>(cfg.GetInt(p + ".resource", 0));
			b.damage      = static_cast<std::int32_t>(cfg.GetInt(p + ".damage", 0));
			b.accuracy    = static_cast<std::int32_t>(cfg.GetInt(p + ".accuracy", 0));
			b.range       = static_cast<float>(cfg.GetDouble(p + ".range", 0.0));
			b.critRate    = static_cast<float>(cfg.GetDouble(p + ".critRate", 0.0));
			b.critMult    = static_cast<float>(cfg.GetDouble(p + ".critMult", 0.0));
			b.speedWalk   = static_cast<float>(cfg.GetDouble(p + ".speedWalk", 0.0));
			b.speedRun    = static_cast<float>(cfg.GetDouble(p + ".speedRun", 0.0));
			b.speedSprint = static_cast<float>(cfg.GetDouble(p + ".speedSprint", 0.0));
			b.stamina     = static_cast<std::int32_t>(cfg.GetInt(p + ".stamina", 0));
			b.perception  = static_cast<std::int32_t>(cfg.GetInt(p + ".perception", 0));
			b.stealth     = static_cast<std::int32_t>(cfg.GetInt(p + ".stealth", 0));
			return b;
		}
	}

	bool ItemCatalog::LoadFromJson(const std::string& jsonText)
	{
		engine::core::Config cfg;
		if (!cfg.LoadFromString(jsonText))
			return false;

		// Config aplatit les tableaux JSON en notation crochets : items[0].id, etc.
		// On itère tant que "items[i].id" existe.
		for (std::size_t i = 0;; ++i)
		{
			const std::string p = std::format("items[{}]", i);
			if (!cfg.Has(p + ".id"))
				break;

			ItemDefinition def;
			def.id          = static_cast<std::uint32_t>(cfg.GetInt(p + ".id", 0));
			if (def.id == 0)
				continue; // id 0 réservé (= vide) : on ignore l'entrée invalide.
			def.name        = cfg.GetString(p + ".name", "");
			def.description = cfg.GetString(p + ".description", "");
			def.iconPath    = cfg.GetString(p + ".icon", "");
			def.type        = ItemTypeFromString(ToLowerAscii(cfg.GetString(p + ".type", "misc")));
			def.slot        = EquipmentSlotFromString(ToLowerAscii(cfg.GetString(p + ".slot", "none")));
			def.visualMesh  = cfg.GetString(p + ".visualMesh", "");
			if (cfg.Has(p + ".bonus.hp") || cfg.Has(p + ".bonus.damage") ||
				cfg.Has(p + ".bonus.resource") || cfg.Has(p + ".bonus.accuracy") ||
				cfg.Has(p + ".bonus.range") || cfg.Has(p + ".bonus.critRate") ||
				cfg.Has(p + ".bonus.critMult") || cfg.Has(p + ".bonus.speedWalk") ||
				cfg.Has(p + ".bonus.speedRun") || cfg.Has(p + ".bonus.speedSprint") ||
				cfg.Has(p + ".bonus.stamina") || cfg.Has(p + ".bonus.perception") ||
				cfg.Has(p + ".bonus.stealth"))
			{
				def.bonus = ReadBonus(cfg, p + ".bonus");
			}

			m_byId[def.id] = std::move(def);
		}
		return true;
	}

	bool ItemCatalog::LoadFromFile(const std::string& filePath)
	{
		std::ifstream in(filePath, std::ios::binary);
		if (!in)
			return false;
		std::ostringstream ss;
		ss << in.rdbuf();
		return LoadFromJson(ss.str());
	}

	const ItemDefinition* ItemCatalog::Find(std::uint32_t id) const
	{
		const auto it = m_byId.find(id);
		return it == m_byId.end() ? nullptr : &it->second;
	}

	// ---- Conversions chaîne <-> enum (déclarées dans ItemDefinition.h) ----

	EquipmentSlot EquipmentSlotFromString(const std::string& s)
	{
		if (s == "head")     return EquipmentSlot::Head;
		if (s == "chest")    return EquipmentSlot::Chest;
		if (s == "legs")     return EquipmentSlot::Legs;
		if (s == "feet")     return EquipmentSlot::Feet;
		if (s == "hands")    return EquipmentSlot::Hands;
		if (s == "mainhand") return EquipmentSlot::MainHand;
		if (s == "offhand")  return EquipmentSlot::OffHand;
		if (s == "amulet")   return EquipmentSlot::Amulet;
		if (s == "ring1")    return EquipmentSlot::Ring1;
		if (s == "ring2")    return EquipmentSlot::Ring2;
		return EquipmentSlot::None;
	}

	ItemType ItemTypeFromString(const std::string& s)
	{
		if (s == "weapon")     return ItemType::Weapon;
		if (s == "armor")      return ItemType::Armor;
		if (s == "accessory")  return ItemType::Accessory;
		if (s == "consumable") return ItemType::Consumable;
		if (s == "quest")      return ItemType::Quest;
		return ItemType::Misc;
	}

	const char* ToString(EquipmentSlot slot)
	{
		switch (slot)
		{
		case EquipmentSlot::Head:     return "head";
		case EquipmentSlot::Chest:    return "chest";
		case EquipmentSlot::Legs:     return "legs";
		case EquipmentSlot::Feet:     return "feet";
		case EquipmentSlot::Hands:    return "hands";
		case EquipmentSlot::MainHand: return "mainhand";
		case EquipmentSlot::OffHand:  return "offhand";
		case EquipmentSlot::Amulet:   return "amulet";
		case EquipmentSlot::Ring1:    return "ring1";
		case EquipmentSlot::Ring2:    return "ring2";
		default:                      return "none";
		}
	}

	const char* ToString(ItemType type)
	{
		switch (type)
		{
		case ItemType::Weapon:     return "weapon";
		case ItemType::Armor:      return "armor";
		case ItemType::Accessory:  return "accessory";
		case ItemType::Consumable: return "consumable";
		case ItemType::Quest:      return "quest";
		default:                   return "misc";
		}
	}
}
