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

	/// Jeton de slot pour \p itemId : "item:<itemId>".
	inline std::string MakeCakeToken(uint32_t itemId)
	{
		return "item:" + std::to_string(itemId);
	}

	/// Parse un jeton de slot. \return true si \p token est "item:<id>" avec
	/// <id> = gâteau valide (écrit dans \p outItemId). Tout autre contenu
	/// (spellId, vide, id hors plage) retourne false.
	inline bool ParseCakeToken(std::string_view token, uint32_t& outItemId)
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
		if (!IsCakeItemId(value)) return false;
		outItemId = value;
		return true;
	}
}
