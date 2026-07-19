#pragma once
// CakeItemToken — jeton « objet dans la barre d'action » du gâteau
// d'anniversaire (spec 2026-07-18, SP3). Partagé client + shard.
//
// Les slots de barre d'action (kinds UDP 88/89) transportent des chaînes
// (spellIds). Le gâteau y est représenté par le jeton "item:<itemId>"
// (ex. "item:5101") — espace de noms disjoint des spellIds textuels, aucun
// changement de wire ni de kProtocolVersion. Seuls les ids de gâteaux
// (5101..5110, cf. items.json) sont des jetons valides.

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::anniversary
{
	/// Plage d'ids des gâteaux d'anniversaire dans items.json.
	constexpr uint32_t kFirstCakeItemId  = 5101u;
	constexpr uint32_t kCakeVariantCount = 10u;

	/// true si \p itemId est un gâteau d'anniversaire (5101..5110).
	inline bool IsCakeItemId(uint32_t itemId)
	{
		return itemId >= kFirstCakeItemId && itemId < kFirstCakeItemId + kCakeVariantCount;
	}

	/// Roadmap-3 (2026-07-19) — jeton de slot GÉNÉRIQUE pour tout objet :
	/// "item:<itemId>" (ceinture : gâteaux, potions, nourriture…).
	inline std::string MakeItemToken(uint32_t itemId)
	{
		return "item:" + std::to_string(itemId);
	}

	/// Jeton de slot pour \p itemId : "item:<itemId>" (alias historique
	/// gâteau — même format que MakeItemToken).
	inline std::string MakeCakeToken(uint32_t itemId)
	{
		return MakeItemToken(itemId);
	}

	/// Roadmap-3 (2026-07-19) — Parse GÉNÉRIQUE d'un jeton "item:<id>" (tout
	/// id d'objet > 0 : gâteaux, potions, nourriture…). \return true si le
	/// format est strict (préfixe + chiffres) — l'appelant valide ensuite
	/// l'existence/l'activabilité de l'objet (catalogue, possession).
	inline bool ParseItemToken(std::string_view token, uint32_t& outItemId)
	{
		constexpr std::string_view kPrefix = "item:";
		if (token.size() <= kPrefix.size() || token.compare(0, kPrefix.size(), kPrefix) != 0)
			return false;
		uint32_t value = 0;
		for (size_t i = kPrefix.size(); i < token.size(); ++i)
		{
			const char c = token[i];
			if (c < '0' || c > '9') return false;
			value = value * 10u + static_cast<uint32_t>(c - '0');
			if (value > 1000000u) return false; // garde anti-overflow
		}
		if (value == 0u) return false;
		outItemId = value;
		return true;
	}

	/// Parse un jeton de slot GÂTEAU. \return true si \p token est
	/// "item:<id>" avec <id> = gâteau valide (5101..5110).
	inline bool ParseCakeToken(std::string_view token, uint32_t& outItemId)
	{
		uint32_t value = 0;
		if (!ParseItemToken(token, value) || !IsCakeItemId(value)) return false;
		outItemId = value;
		return true;
	}
}
